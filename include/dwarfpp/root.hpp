/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * root.hpp: root node of DIE tree, and basic_die base class.
 *
 * Copyright (c) 2008--17, Stephen Kell. For licensing information, see the
 * LICENSE file in the root of the libdwarfpp tree.
 */

#ifndef DWARFPP_ROOT_HPP_
#define DWARFPP_ROOT_HPP_

#include <iostream>
#include <utility>
#include <map>
#include <unordered_map>
#include <deque>
#include <boost/intrusive_ptr.hpp>
#include <srk31/selective_iterator.hpp>
#include <srk31/transform_iterator.hpp>
#include <srk31/concatenating_iterator.hpp>

#include "util.hpp"
#include "spec.hpp"
#include "abstract.hpp"
#include "libdwarf.hpp"
#include "libdwarf-handles.hpp"

namespace dwarf
{
	using std::string;
	using std::map;
	using std::unordered_map;
	using std::pair;
	using std::make_pair;
	using std::multimap;
	using std::deque;
	using std::dynamic_pointer_cast;
	using boost::intrusive_ptr;
	
	namespace core
	{
		struct FrameSection;
		// iterators: forward decls
		template <typename Iter> struct sequence;
		std::ostream& operator<<(std::ostream& s, const iterator_base& it);
		template <typename DerefAs /* = basic_die*/> struct iterator_df; // see attr.hpp
		template <typename DerefAs = basic_die> struct iterator_bf;
		template <typename DerefAs = basic_die> struct iterator_sibs;
		struct type_iterator_df;
		// children
		// so how do we iterate over "children satisfying predicate, derefAs'd X"? 
		template <typename Payload>
		struct is_a_t
		{
			inline bool operator()(const iterator_base& it) const;
			bool operator==(const is_a_t<Payload>&) const { return true; }
			bool operator!=(const is_a_t<Payload>&) const { return false; }
		}; // defined below, once we have factory
		// We want to partially specialize a function template, 
		// which we can't do. So pull out the core into a class
		// template which we call from the (non-specialised) function template.
		template <typename Pred, typename Iter>
		struct subseq_t;
		// specialization for is_a, adding downcast transformer
		template <typename Iter, typename Payload>
		struct subseq_t<Iter, is_a_t<Payload> >;
		template <typename Iter> 
		struct sequence;

		/* With the above, we should be able to write
		 * auto subps = subseq< is_a_t<subprogram_die> >(i_cu.children_here());
		 * ... but let's go one better:
		 * auto subps = i_cu.children_here().subseq_of<subprogram_die>();
		 * ... by defining a special "subsequent" wrapper of iterator-pairs.
		 */
		template <typename Iter> 
		struct sequence : public pair<Iter, Iter>
		{
			typedef pair<Iter, Iter> base;
			sequence(pair<Iter, Iter>&& arg) : base(std::move(arg)) {}
			sequence(const pair<Iter, Iter>& arg) : base(arg) {}

			template <typename Pred>
			sequence<
				srk31::selective_iterator<Pred, Iter>
			> subseq_with(const Pred& pred) 
			{
				return subseq_t<
					Iter,
					Pred
				>(pred).operator()(*this);
			}
			template <typename D>
			sequence<
				typename subseq_t<Iter, is_a_t<D> >::transformed_iterator
			> subseq_of() 
			{ 
				return subseq_t< 
					Iter, 
					is_a_t<D> 
				>().operator()(*this); 
			}
		};
		// specialization for is_a, no downcast
		template <typename Iter, typename Pred>
		struct subseq_t
		{
			/* See subseq_with to understand why this is a reference. */
			const Pred& m_pred;
			subseq_t(const Pred& pred) : m_pred(pred) {}
			
			typedef srk31::selective_iterator<Pred, Iter> filtered_iterator;

			inline pair<filtered_iterator, filtered_iterator> 
			operator()(const pair<Iter, Iter>& in_seq);

			inline pair<filtered_iterator, filtered_iterator> 
			operator()(pair<Iter, Iter>&& in_seq);
		};
		// specialization for is_a, adding downcast transformer
		template <typename Iter, typename Payload>
		struct subseq_t<Iter, is_a_t<Payload> >
		{
			typedef srk31::selective_iterator< is_a_t<Payload>, Iter> filtered_iterator;

			// transformer is just dynamic_cast, wrapped as a function
			struct transformer : std::function<Payload&(basic_die&)>
			{
				transformer() : std::function<Payload&(basic_die&)>([](basic_die& arg) -> Payload& {
					return dynamic_cast<Payload&>(arg);
				}) {}
			};
			typedef srk31::transform_iterator<transformer, filtered_iterator >
				transformed_iterator;

			pair<transformed_iterator, transformed_iterator> 
			operator()(const pair<Iter, Iter>& in_seq)
			{
				auto filtered_first = filtered_iterator(in_seq.first, in_seq.second);
				auto filtered_second = filtered_iterator(in_seq.second, in_seq.second);

				return make_pair(
					transformed_iterator(
						filtered_first
					),
					transformed_iterator(
						filtered_second
					)
				);
			}

			inline pair<transformed_iterator, transformed_iterator> 
			operator()(pair<Iter, Iter>&& in_seq);
		};
		
		class handle_with_position
		{
			root_die *p_root;
			Die handle;
			bool have_depth;
			bool have_offset;
			bool have_parent_offset;
			bool have_first_child_offset;
			bool have_next_sibling_offset;
			unsigned short m_depth;
			Dwarf_Off m_offset;
			Dwarf_Off m_parent_offset;
			Dwarf_Off m_first_child_offset;
			Dwarf_Off m_next_sibling_offset;
#define accessor(t, n) \
			opt<t> n() const { if (have_ ## n) return opt< t >(m_ ## n); else return opt< t >(); }
		public:
			accessor(unsigned short, depth)
			accessor(Dwarf_Off, offset)
			accessor(Dwarf_Off, parent_offset)
			accessor(Dwarf_Off, first_child_offset)
			accessor(Dwarf_Off, next_sibling_offset)
#undef accessor
			/* Constructors and assignment operators: how do we do this?
			 * Naively, we want every Die constructor to have a corresponding
			 * constructor here, except that we do cache maintenance.
			 * Problem: that is exposing the nastiness of the libdwarf API;
			 * better to have "move to" (non-copying) and "get" (copying)
			 * methods. Can we capture their commonality? Yes; either by
			 * saying "get" is a copy followed by a replacement "move to",
			 * or by saying a "move to" is a copying get followed by a 
			 * move-to-this. The latter is a better fit for libdwarf, since
			 * the whole point is that we are getting a *different* DIE.
			 * We can make this friendlier even in the libdwarf case
			 * by giving the try_construct cases proper names.
			 * So what constructors do we expose? Maybe just a Dwarf_Off one
			 * and a directional one. considered  */
			enum relation { first_child, next_sibling };
			inline handle_with_position(relation dir, const handle_with_position& hwp);
			inline handle_with_position(root_die& r, Dwarf_Off o, opt<unsigned short> depth = opt<unsigned short>());
			
			inline handle_with_position(const handle_with_position& hwp);
			inline handle_with_position(handle_with_position&& hwp);
			
			inline handle_with_position& operator=(const handle_with_position& hwp);
			inline handle_with_position& operator=(handle_with_position&& hwp);
		};
		
		class basic_die : public virtual abstract_die
		{
			friend struct iterator_base;
			friend class root_die;
			friend class Die; // FIXME: define handle_with_nav instead
		protected:
			// we need to embed a refcount
			unsigned refcount;
			
			// we need this, if we're libdwarf-backed; if not, it's null
			Die d;

			/* No other fields! This class is here to act as the base for
			 * state-holding subclasses that are optimised for particular
			 * purposes, e.g. fast local/parameter location.  */
			// remember! payload = handle + shared count + extra state
			
			// actually, cache some stuff
			opt<Dwarf_Off> cached_parent_off;
			opt<Dwarf_Off> cached_first_child_off;
			opt<Dwarf_Off> cached_next_sibling_off;
			
			/* We define an overridable *interface* for attribute access. */
			// helper
			static void left_merge_attrs(encap::attribute_map& m, const encap::attribute_map& arg);
			virtual bool has_attr(Dwarf_Half attr) const 
			{ assert(d.handle); return d.has_attr_here(attr); }
			// get all attrs in one go
			virtual encap::attribute_map all_attrs() const;
			// get a single attr
			virtual encap::attribute_value attr(Dwarf_Half a) const;
			// get all attrs in one go, seeing through abstract_origin / specification links
			virtual encap::attribute_map find_all_attrs() const;
			// get a single attr, seeing through abstract_origin / specification links
			virtual encap::attribute_value find_attr(Dwarf_Half a) const;
			virtual root_die& get_root() const // NOT defaulted!
			{
				assert(d.handle);
				return d.get_constructing_root();
			}
			inline iterator_base find_self() const;
			inline bool is_dummy() const;
			
			// protected constructor constructing dummy instances
			basic_die(spec& s); 
			// protested constructor constructing in-memory instances
			basic_die(spec& s, root_die &r);
			// protected constructor that is never actually used, but 
			// required to avoid special-casing in macros -- see begin_class() macro
			inline basic_die() : refcount(0), d(nullptr, nullptr)
			{ assert(false); }
			friend struct dwarf_current_factory_t;
		public:
			inline basic_die(spec& s, Die&& h);
			
			friend std::ostream& operator<<(std::ostream& s, const basic_die& d);
			friend void intrusive_ptr_add_ref(basic_die *p);
			friend void intrusive_ptr_release(basic_die *p);
			
			inline virtual ~basic_die();

			/* implement the abstract_die interface 
			 * -- note that has_attr is defined above */
			inline Dwarf_Off get_offset() const { assert(d.handle); return d.offset_here(); }
			inline Dwarf_Half get_tag() const { assert(d.handle); return d.tag_here(); }
			inline opt<string> get_name() const 
			{ 
				assert(d.handle); 
				if (d.name_here()) return opt<string>(string(d.name_here().get()));
				else return opt<string>();
			}
			inline unique_ptr<const char, string_deleter> get_raw_name() const
			{ assert(d.handle); return d.name_here(); }
			inline Dwarf_Off get_enclosing_cu_offset() const 
			{ assert(d.handle); return d.enclosing_cu_offset_here(); }
			/* The same as all_attrs, but comes from abstract_die. 
			 * We want to delegate from basic_die to abstract_die, so
			 * this function does the work and all_attrs delegates to it. */
			inline encap::attribute_map copy_attrs() const
			{
				return encap::attribute_map(AttributeList(d), d, get_root());
			}
			inline spec& get_spec(root_die& r) const 
			{ assert(d.handle); return d.spec_here(); }
			
			// get a "definition" DIE from a DW_AT_declaration DIE
			virtual iterator_base find_definition() const;

			/* The same as find_all_attrs. FIXME: do we really need this gather_ API? */
			inline encap::attribute_map gather_attrs() const
			{ return find_all_attrs(); }
			
			// get children
			inline
			sequence< iterator_sibs<basic_die> >
			children() const;

			template <typename Payload>
			using children_iterator = typename subseq_t<iterator_sibs<>, is_a_t<Payload> >::transformed_iterator;
			
			void print(std::ostream& s) const;
			void print_with_attrs(std::ostream& s) const;
		};	
		std::ostream& operator<<(std::ostream& s, const basic_die& d);
		inline void intrusive_ptr_add_ref(basic_die *p)
		{
			++(p->refcount);
		}
		inline void intrusive_ptr_release(basic_die *p)
		{
			--(p->refcount);
			if (p->refcount == 0) delete p;
		}
		
		struct is_visible_and_named;
		struct grandchild_die_at_offset;
		
		//template <typename Pred, typename DerefAs = basic_die> 
		//using iterator_sibs_where
		// = boost::filter_iterator< Pred, iterator_sibs<DerefAs> >;
		
		// FIXME: this is not libdwarf-agnostic! 
		// ** Could we use it for encap too, with a null Debug?
		// ** Can we abstract out a core base class
		// --- yes, where "Debug" just means "root resource", and encap doesn't have one
		// --- we are still leaking libdwarf design through our "abstract" interface
		// ------ can we do anything about this? 
		// --- what methods do handles provide? 
		struct root_die
		{
			/* Everything that calls a libdwarf constructor-style (resource-allocating)
			 * function needs to be our friend, so that it can get our raw
			 * Dwarf_Debug ptr. FIXME: hmm, maybe provide an accessor then. */
			friend struct iterator_base;
			friend struct Die;
			friend struct Attribute; // so it can get the raw Dwarf_Debug for its deleter
			friend struct AttributeList;
			friend struct Line;
			friend struct LineList;
			friend struct Global;
			friend struct GlobalList;
			friend struct Arange;
			friend struct ArangeList;
			
			friend struct basic_die;
			friend struct type_die; // for equal_to
			friend class factory; // for visible_named_grandchildren_is_complete
			
		protected: // was protected -- consider changing back
			typedef intrusive_ptr<basic_die> ptr_type;
			Debug dbg;
			
			/* live DIEs -- any basic DIE that is instantiated registers itself here,
			 * and deregisters itself when it is destructed.
			 * This must be destructed *after* the sticky set, i.e. declared before it,
			 * because the basic_die destructor manipulates this hash table, 
			 * so it must still be alive while sticky DIEs are being destroyed. */
			unordered_map<Dwarf_Off, basic_die* > live_dies;
			
			/* NOTE: sticky_dies must come after dbg, because all Dwarf_Dies are 
			 * destructed when a Dwarf_Debug is destructed. So our intrusive_ptrs
			 * will be invalid if we destruct the latter first, and bad results follow. */
			map<Dwarf_Off, ptr_type > sticky_dies; // compile_unit_die is always sticky
			
			/* Each of these caches also has an in-payload equivalent, in basic_die. */
			unordered_map<Dwarf_Off, Dwarf_Off> parent_of;
			unordered_map<Dwarf_Off, Dwarf_Off> first_child_of;
			unordered_map<Dwarf_Off, Dwarf_Off> next_sibling_of;
			
			map<pair<Dwarf_Off, Dwarf_Half>, Dwarf_Off> refers_to;
			map<Dwarf_Off, pair< Dwarf_Off, bool> > equal_to;
			map<Dwarf_Off, opt<uint32_t> > type_summary_code_cache; // FIXME: delete this after summary_code() uses SCCs
			opt<Dwarf_Off> synthetic_cu;

			multimap<string, Dwarf_Off> visible_named_grandchildren_cache;
			bool visible_named_grandchildren_is_complete;
			friend class in_memory_abstract_die::attribute_map;

			FrameSection *p_fs;
			Dwarf_Off current_cu_offset; // 0 means none
			::Elf *returned_elf;
		public:
			FrameSection&       get_frame_section()       { assert(p_fs); return *p_fs; }
			const FrameSection& get_frame_section() const { assert(p_fs); return *p_fs; }
		protected:
			virtual ptr_type make_payload(const iterator_base& it);
		public:
			virtual iterator_df<compile_unit_die> get_or_create_synthetic_cu();
			virtual iterator_base make_new(const iterator_base& parent, Dwarf_Half tag);
			virtual bool is_sticky(const abstract_die& d);
			
			void get_referential_structure(
				unordered_map<Dwarf_Off, Dwarf_Off>& parent_of,
				map<pair<Dwarf_Off, Dwarf_Half>, Dwarf_Off>& refers_to) const;

		public: // HMM
			virtual Dwarf_Off fresh_cu_offset();
			virtual Dwarf_Off fresh_offset_under(const iterator_base& pos);
		
		public:
			root_die() : dbg(), visible_named_grandchildren_is_complete(false), p_fs(nullptr),
				current_cu_offset(0), returned_elf(nullptr) {}
			root_die(int fd);
			virtual ~root_die();
		
			template <typename Iter = iterator_df<> >
			inline Iter begin(); 
			template <typename Iter = iterator_df<> >
			inline Iter end();
			template <typename Iter = iterator_df<> >
			inline pair<Iter, Iter> sequence();
			
			/* FIXME: instead of templating, maybe just return
			 * iterator_bases, and if you want a different iterator, use
			 * my_iter i = r.begin(); 
			 * ?
			 * (Just make sure my_iter has a move constructor!) */
			
			/* The implicit-conversion approach doesn't work so well for pairs -- 
			 * we can't just return pair<iterator_base, iterator_base>. 
			 */
			inline dwarf::core::sequence<
				typename subseq_t<iterator_sibs<>, is_a_t<compile_unit_die> >::transformed_iterator
			>
			children() const;
			
			typedef srk31::concatenating_iterator< iterator_sibs<basic_die>, basic_die, basic_die& >
				grandchildren_iterator;
			inline dwarf::core::sequence< grandchildren_iterator >
			grandchildren() const;
			
			friend struct is_visible_and_named;
			friend struct grandchild_die_at_offset;
			
			typedef srk31::selective_iterator< is_visible_and_named, grandchildren_iterator >
				visible_named_grandchildren_iterator;
			inline dwarf::core::sequence< visible_named_grandchildren_iterator >
			visible_named_grandchildren() const;
			
			/* 
			How to make an iterator over particular-named visible grandchildren?

			Easiest way: when we get a query for a particular name,
			ensure the cache is complete,
			then return a transform_iterator over the particular equal_range pair in the cache.

			This doesn't perform as well as incrementalising the cache-filling loop
			in resolve_all_visible.
			But it's probably fine for now.
			*/

			/* To iterate over visible grandchildren with a *particular* name,
			 * we use a cache. */
			//typedef srk31::transform_iterator<
			//	decltype(visible_named_grandchildren_cache)::iterator,
			//	grandchild_die_at_offset
			//> visible_grandchildren_with_name_iterator;
			
			//inline pair<
			//	visible_grandchildren_with_name_iterator,
			//	visible_grandchildren_with_name_iterator
			//>
			//visible_grandchildren_with_name(const string& name) const;
			
			// const versions... nothing interesting here
			template <typename Iter = iterator_df<> >
			Iter begin() const { return const_cast<root_die*>(this)->begin<Iter>(); } 
			template <typename Iter = iterator_df<> >
			Iter end() const { return const_cast<root_die*>(this)->end<Iter>(); } 
			template <typename Iter = iterator_df<> >
			pair<Iter, Iter> sequence() const 
			{ return const_cast<root_die*>(this)->sequence<Iter>(); }

			template <typename Iter = iterator_df<compile_unit_die> >
			inline Iter enclosing_cu(const iterator_base& it);
			
			/* This is the expensive version. */
			template <typename Iter = iterator_df<> >
			Iter find(Dwarf_Off off, 
				opt<pair<Dwarf_Off, Dwarf_Half> > referencer = opt<pair<Dwarf_Off, Dwarf_Half> >(),
				ptr_type maybe_ptr = ptr_type(nullptr));
			/* This is the cheap version -- must give a valid offset. */
			template <typename Iter = iterator_df<> >
			Iter pos(Dwarf_Off off, opt<unsigned short> opt_depth = opt<unsigned short>(),
				opt<Dwarf_Off> parent_off = opt<Dwarf_Off>(),
				opt<pair<Dwarf_Off, Dwarf_Half> > referencer = opt<pair<Dwarf_Off, Dwarf_Half> >());
			/* This is a synonym for "pos()". */
			template <typename Iter = iterator_df<> >
			Iter at(Dwarf_Off off, unsigned opt_depth = opt<unsigned short>(),
				opt<Dwarf_Off> parent_off = opt<Dwarf_Off>(),
				opt<pair<Dwarf_Off, Dwarf_Half> > referencer = opt<pair<Dwarf_Off, Dwarf_Half> >())
			{ return pos<Iter>(off, opt_depth, parent_off, referencer); }
			/* Convenience for getting CUs */
			template <typename Iter = iterator_df<compile_unit_die> >
			Iter cu_pos(Dwarf_Off off, opt<pair<Dwarf_Off, Dwarf_Half> > referencer = opt<pair<Dwarf_Off, Dwarf_Half> >())
			{ return pos<Iter>(off, 1, opt<Dwarf_Off>(), referencer); }
			
		private: // find() helpers
			template <typename Iter = iterator_df<> >
			Iter find_downwards(Dwarf_Off off);		
			template <typename Iter = iterator_df<> >
			Iter find_upwards(Dwarf_Off off, ptr_type maybe_ptr = nullptr);
			
		public:
			::Elf *get_elf(); // hmm: lib-only?
			Debug& get_dbg() { return dbg; }

			// iterator navigation primitives
			// note: want to avoid virtual dispatch on these
			bool move_to_parent(iterator_base& it);
			bool move_to_first_child(iterator_base& it);
			bool move_to_next_sibling(iterator_base& it);
			iterator_base parent(const iterator_base& it);
			iterator_base first_child(const iterator_base& it);
			iterator_base next_sibling(const iterator_base& it);
			/* 
			 * NOTE: we *don't* put named_child and move_to_named_child here, because
			 * we want to allow exploitation of in-payload data, which might support
			 * faster-than-linear search. So to do name lookups, we want to
			 * call a method on the *iterator* (which knows whether it has payload).
			 * It's okay to implement resolve() et al here, because they will call
			 * into the iterator method.
			 * BUT we do put a special find_named_child method, emphasising the linear
			 * search (slow). This is the fallback implementation used by the iterator.
			 */
			iterator_base find_named_child(const iterator_base& start, const string& name);
			/* This one is only for searches anchored at the root, so no need for "start". */
			iterator_base find_visible_grandchild_named(const string& name);
			std::vector<iterator_base> find_all_visible_grandchildren_named(const string& name);
			
			bool is_under(const iterator_base& i1, const iterator_base& i2);
			
			// libdwarf has this weird stateful CU API
			// FIXME: this belongs in a libdwarf abstraction layer somewhere
			opt<Dwarf_Off> first_cu_offset;
			opt<Dwarf_Unsigned> last_seen_cu_header_length;
			opt<Dwarf_Half> last_seen_version_stamp;
			opt<Dwarf_Unsigned> last_seen_abbrev_offset;
			opt<Dwarf_Half> last_seen_address_size;
			opt<Dwarf_Half> last_seen_offset_size;
			opt<Dwarf_Half> last_seen_extension_size;
			opt<Dwarf_Unsigned> last_seen_next_cu_header;
			bool advance_cu_context();
			bool clear_cu_context();
		protected:
			bool set_subsequent_cu_context(Dwarf_Off off); // helper
		public:
			bool set_cu_context(Dwarf_Off off);
			
			friend struct compile_unit_die; // redundant because we're struct, but future hint

			// print the whole lot
			friend std::ostream& operator<<(std::ostream& s, const root_die& d);

			/* Name resolution functions */
			template <typename Iter>
			inline iterator_base 
			resolve(const iterator_base& start, Iter path_pos, Iter path_end);
			template <typename Iter>
			inline void 
			resolve_all(const iterator_base& start, Iter path_pos, Iter path_end,
				std::vector<iterator_base >& results, unsigned max = 0);
			
			template <typename Iter>
			inline void 
			resolve_all_visible_from_root(Iter path_pos, Iter path_end,
				std::vector<iterator_base >& results, unsigned max = 0);
			inline iterator_base 
			resolve(const iterator_base& start, const std::string& name);

			template <typename Iter>
			inline iterator_base 
			scoped_resolve(const iterator_base& start, Iter path_pos, Iter path_end);
			inline iterator_base 
			scoped_resolve(const iterator_base& start, const string& name);
			
			template <typename Iter>
			inline void
			scoped_resolve_all(const iterator_base& start, Iter path_pos, Iter path_end, 
				std::vector<iterator_base >& results, unsigned max = 0);
			
			void print_tree(iterator_base&& begin, std::ostream& s) const;
		};	
		std::ostream& operator<<(std::ostream& s, const root_die& d);

		inline bool basic_die::is_dummy() const // dynamic_cast doesn't work til we're fully constructed
		{
			return !d.handle && !refcount && !dynamic_cast<const in_memory_abstract_die *>(this);
		}		
		inline basic_die::basic_die(spec& s, Die&& h)
		 : refcount(0), d(std::move(h))
		{
			assert(get_root().live_dies.find(get_offset()) == get_root().live_dies.end());
			get_root().live_dies.insert(make_pair(get_offset(), this));
		}

		inline basic_die::~basic_die()
		{
			if (!is_dummy()) get_root().live_dies.erase(get_offset());
			debug(5) << "Destructed basic DIE object at " << this << std::endl;
		}
		
		struct in_memory_root_die : public root_die
		{
			virtual bool is_sticky(const abstract_die& d) { return true; }
			
			in_memory_root_die() {}
			in_memory_root_die(int fd) : root_die(fd) {}
		};
	}
}

#endif
