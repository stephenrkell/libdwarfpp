#ifndef __DWARFPP_SPEC_ADT_HPP
#define __DWARFPP_SPEC_ADT_HPP

#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <boost/iterator_adaptors.hpp>
#include <boost/iterator/filter_iterator.hpp>
#include <boost/iterator/transform_iterator.hpp>
#include <boost/icl/interval_map.hpp>
#include <srk31/concatenating_iterator.hpp>
#include "lib.hpp"
#include "expr.hpp"
#include "attr.hpp"
#include "opt.hpp"

namespace dwarf
{
	namespace tool { class cxx_compiler; }
	namespace spec 
	{
		using namespace lib;
		using std::string;
		using std::pair;
		using std::ostream;
		using std::make_pair;
		using srk31::concatenating_sequence;
		using srk31::concatenating_iterator;
		using std::clog;
		using boost::filter_iterator;
		using boost::transform_iterator;
		using boost::dynamic_pointer_cast;
		
		class compile_unit_die;
		class program_element_die;
		struct basic_die;
		struct file_toplevel_die;
		struct member_die;
		struct type_die;
		ostream& operator<<(ostream& s, const basic_die& d);
		ostream& operator<<(ostream& s, const abstract_dieset& ds);

		class abstract_dieset
		{
		public:
			/* This is all you need to denote a member of a dieset. */
			struct position
			{
				abstract_dieset *p_ds;
				Dwarf_Off off;
			}; // end class position
			typedef std::deque<Dwarf_Off> path_type; 
			struct iterator_base;
			
			struct order_policy
			{
				virtual int increment(iterator_base& pos) = 0;
				virtual int decrement(iterator_base& pos) = 0;
			protected:
				const int behaviour; // used for equality comparison
				
				enum
				{
					NO_BEHAVIOUR,
					PREDICATED_BEHAVIOUR,
					DEPTHFIRST_BEHAVIOUR, // FIXME: do I actually use the predicates in these?
					BREADTHFIRST_BEHAVIOUR,
					SIBLINGS_BEHAVIOUR
				};
				
				order_policy(int behaviour) : behaviour(behaviour) {}
			public:
				bool is_undefined() const { return behaviour == NO_BEHAVIOUR; }
				
				/* Equality of policies: 
				 * By default, policies are tested only for behavioural equality.
				 * Subtypes might want to narrow that to
				 * - all policies of the same class are equal
				 * or
				 * - policies must exhibit value-equality.
				 *
				 * Virtual dispatch asymmetry here:
				 * in the worst case this will get dispatched
				 * to a base class. But that only happens if
				 * the two policies are not of the same class.
				 * We don't want such to be equal anyway, so
				 * this is okay. 
				 */ 
				order_policy() : behaviour(0) {}
				
				virtual bool half_equal(const order_policy& arg) const
				{
					// this is the "most generous" equality relation
					return this->behaviour == arg.behaviour;
				}
				bool operator==(const order_policy& arg) const
				{
					return this->half_equal(arg) && arg.half_equal(*this);
				}
			}; // end class order_policy
			
			struct policy : order_policy//, die_pred 
			{
			protected:
				policy(int behaviour) : order_policy(behaviour) {}
			public:
				policy() : order_policy(PREDICATED_BEHAVIOUR) {}
			};
			/* Dummy policy: okay to assign over! */
			struct dummy_policy : policy
			{
				// depth-first iteration
				int increment(iterator_base& pos) { assert(false); }
				int decrement(iterator_base& pos) { assert(false); }
			public:
				dummy_policy() : policy(NO_BEHAVIOUR) {}

				// always true
				bool operator()(const spec::basic_die& d) const { return true; }
			};
			static dummy_policy dummy_policy_sg;
			
			/* Default policy: depth-first order, all match. */
			struct default_policy : policy
			{
				// depth-first iteration
				int increment(iterator_base& pos);
				int decrement(iterator_base& pos);
			protected:
				default_policy(int behaviour) : policy(behaviour) {}
			public:
				default_policy() : policy(DEPTHFIRST_BEHAVIOUR) {}
				
				// always true
				bool operator()(const spec::basic_die& d) const { return true; }
			};
			static default_policy default_policy_sg;
			
			/* Breadth-first policy: breadth-first order, all match. */
			struct bfs_policy : policy
			{
				// helper functions
			protected:
				void enqueue_children(iterator_base& base);
				void advance_to_next_sibling(iterator_base& base);
				int take_from_queue_or_terminate(iterator_base& base);
			
			public:
				// a queue of paths
				std::deque<path_type> m_queue;

				// breadth-first iteration
				int increment(iterator_base& pos);
				int decrement(iterator_base& pos);

				// special functions for tweaking the breadth-first exploration
				int increment_skipping_subtree(iterator_base& pos);
				int decrement_skipping_subtree(iterator_base& pos);
					
			protected:
				bfs_policy(int behaviour) : policy(behaviour), m_queue() {}
			public:
				bfs_policy() : policy(BREADTHFIRST_BEHAVIOUR), m_queue() {}
				
				bool half_equal(const order_policy& arg) const
				{
					return dynamic_cast<const bfs_policy*>(&arg)
						&& dynamic_cast<const bfs_policy*>(&arg)->m_queue == this->m_queue;
				}
				
				// always true
				bool operator()(const spec::basic_die& d) const { return true; }
			};
			// we *don't* create a bfs policy singleton because each BFS traversal
			// has to keep its own state (queue of nodes)

			/* Sibling policy: for children iterators */
			struct siblings_policy : policy 
			{
				int increment(iterator_base& pos);
				int decrement(iterator_base& pos);
			protected:
				siblings_policy(int behaviour) : policy(behaviour) {}
			public:
				siblings_policy() : policy(SIBLINGS_BEHAVIOUR) {}
				
				// always true
				bool operator()(const spec::basic_die& d) const { return true; }
			};
			static siblings_policy siblings_policy_sg;
			
			struct position_and_path : position
			{
				path_type path_from_root;
				//position_and_path(const position& pos): position(pos) {}
				position_and_path(abstract_dieset *p_ds, const path_type& path)
				 : position((position){p_ds, path.back()}), path_from_root(path) {}
				position_and_path(const position& pos, const path_type& path)
				: position(pos), path_from_root(path) {}
				position_and_path() : position((position){0, 0UL}) {}
			};
			
			struct iterator_base 
			: public position_and_path
			{
				/* This pointer points to the target DIE, or null if we are end(). */
				shared_ptr<basic_die> p_d;
				
				//typedef std::pair<Dwarf_Off, std::shared_ptr<spec::basic_die> > pair_type;

				bool operator==(const iterator_base& arg) const
				{ return this->off == arg.off && this->p_ds == arg.p_ds
					/*&& (off == std::numeric_limits<Dwarf_Off>::max() || // HACK: == end() works
						*this->p_policy == *arg.p_policy)*/;			   // for any policy
				}
				bool operator!=(const iterator_base& arg) const 
				{ return !(*this == arg); }

				// helper for constructing p_d
				static shared_ptr<basic_die> 
				die_from_offset(
					abstract_dieset& ds, Dwarf_Off off
				)
				{
					if (off == std::numeric_limits<Dwarf_Off>::max()) return shared_ptr<basic_die>();
					else return ds[off];
				}

				iterator_base(abstract_dieset& ds, Dwarf_Off off,
					const path_type& path_from_root,
					shared_ptr<basic_die> p_d = shared_ptr<basic_die>())
				: position_and_path((position){&ds, off}, path_from_root), 
				  p_d(p_d ? p_d : die_from_offset(ds, off))
				{ 
					//assert(!p_d || p_d->get_offset() == off);
				}

				iterator_base() // no dieset, never mind a die! path_from_root is empty
				: position_and_path((position){0, 0UL}, path_type()), 
				  p_d() {} 

				iterator_base(abstract_dieset *p_ds, const path_type& arg)
				: position_and_path(p_ds, arg), 
				  p_d(p_ds ? die_from_offset(*p_ds, arg.back()) : shared_ptr<basic_die>())
				{}

				iterator_base(const position_and_path& arg)
				: position_and_path(arg), 
				  p_d(arg.p_ds ? die_from_offset(*arg.p_ds, arg.off) : shared_ptr<basic_die>()) {}
				
				iterator_base(const iterator_base& arg)
				: position_and_path(arg),
				  p_d(arg.p_ds ? die_from_offset(*arg.p_ds, arg.off) : shared_ptr<basic_die>()) {}

				typedef std::bidirectional_iterator_tag iterator_category;
				typedef spec::basic_die value_type;
				typedef Dwarf_Off difference_type;
				typedef spec::basic_die *pointer;
				typedef spec::basic_die& reference;
				
				iterator_base& operator=(const iterator_base& arg)
				{
	//				assert(p_policy->is_undefined() || *p_policy == *arg.p_policy);
	//				// HMM: do we want to deep-copy the policy? Not for now
	//				if (p_policy->is_undefined()) p_policy = arg.p_policy;
					this->path_from_root = arg.path_from_root;
					*static_cast<position*>(this) = arg;
					this->p_d = arg.p_d;
					return *this;
				}
			}; // end iterator_base

			virtual Dwarf_Off highest_offset_upper_bound() 
			{ return std::numeric_limits<Dwarf_Off>::max(); }
			virtual Dwarf_Off get_last_monotonic_offset()
			{ return 0UL; }
			
			struct iterator;
			virtual iterator find(Dwarf_Off off) = 0;
			virtual iterator begin() = 0;
			virtual iterator end() = 0;
			virtual iterator begin(policy& pol);
			virtual iterator end(policy& pol);
			
			virtual bool move_to_first_child(iterator_base& arg) = 0;
			virtual bool move_to_parent(iterator_base& arg) = 0;
			virtual bool move_to_next_sibling(iterator_base& arg) = 0;
			// backlinks aren't necessarily stored, so support search for parent
			virtual Dwarf_Off find_parent_offset_of(Dwarf_Off off) = 0;

			template <typename Action>
			void
			for_all_identical_types(
				shared_ptr<type_die> p_t,
				const Action& action
			);
			
			virtual shared_ptr<basic_die> operator[](Dwarf_Off off) const = 0;

			shared_ptr<basic_die> operator[](Dwarf_Off off)
			{ return const_cast<const abstract_dieset&>(*this)[off]; }
		
			struct iterator
			: public boost::iterator_adaptor<iterator, // Derived
					iterator_base,		// Base
					shared_ptr<basic_die>, // Value
					boost::bidirectional_traversal_tag, // Traversal
					shared_ptr<basic_die> // Reference
				>
			{
				friend class abstract_dieset; // for mutating navigation functions
				typedef std::shared_ptr<spec::basic_die> Value;
				typedef iterator_base Base;
				
				// an iterator is an iterator_base + a policy
				/* We have to use a pointer because some clients 
				 * (like boost graph algorithms, where we are playing the edge_iterator)
				 * want to default-construct an iterator, then assign to it. Although
				 * we don't want to assign different policies, we do want to be able
				 * to construct with a dummy policy, then overwrite with a real policy. */
				policy *p_policy;
				
				iterator() : iterator::iterator_adaptor_(), p_policy(&default_policy_sg) {}
				
				iterator(Base p) : iterator::iterator_adaptor_(p), p_policy(&default_policy_sg) {}
				
				iterator(abstract_dieset& ds, Dwarf_Off off, 
					const path_type& path_from_root, 
					shared_ptr<basic_die> p_d = shared_ptr<basic_die>(),
					policy& pol = default_policy_sg)  
				: iterator::iterator_adaptor_(
					iterator_base(ds, off, path_from_root, p_d)), p_policy(&pol) {}

				iterator(const position& pos, 
					const path_type& path_from_root,
					shared_ptr<basic_die> p_d = shared_ptr<basic_die>(),
					policy& pol = default_policy_sg)  
				: iterator::iterator_adaptor_(iterator_base(
					*pos.p_ds, pos.off, path_from_root, p_d)), p_policy(&pol) {}

				 iterator(const position_and_path& pos, 
				 	shared_ptr<basic_die> p_d = shared_ptr<basic_die>(),
					policy& pol = default_policy_sg)  
				: iterator::iterator_adaptor_(iterator_base(
					*pos.p_ds, pos.off, pos.path_from_root, p_d)), p_policy(&pol) {}
			
				// copy-like constructor that changes policy
				iterator(const iterator& arg, policy& pol) : 
					iterator::iterator_adaptor_(iterator_base(
						*arg.base().p_ds, 
						arg.base().off, 
						arg.base().path_from_root, 
						arg.base().p_d)), p_policy(&pol) {}
								
				void increment()		
				{ p_policy->increment(base_reference()); }
				void decrement()		
				{ p_policy->decrement(base_reference()); }

// 				Value dereference() { return base().p_ds->operator[](base().off); }
// 				Value dereference() const { return base().p_ds->operator[](base().off); }
				/*Reference*/ Value dereference() { return base/*_reference*/().p_d; }
				/*const Value&*/ Value dereference() const { return base/*_reference*/().p_d; }

				position& pos() { return base_reference(); }
				const path_type& path() { return base_reference().path_from_root; }
				
				/* iterator_adaptor implements <, <=, >, >= using distance_to.
				 * However, that's more precise than we need: we can't cheaply
				 * compute distance_to, but we can cheaply compute a partial
				 * order among nodes sharing a parent. This is useful for our
				 * various children_iterator, in whose context it becomes a total
				 * order. We implement this partial order here. */
				bool shares_parent_pos(const iterator& i) const;
				bool operator<(const iterator& i) const;
				bool operator<=(const iterator& i) const
				{
					return this->shares_parent_pos(i)
					&&
					(i.base() == this->base()
						|| *this < i);
				}
				bool operator>(const iterator& i) const
				{ return this->shares_parent_pos(i) && !(*this <= i); }
				
				bool operator>=(const iterator& i) const
				{ return this->shares_parent_pos(i) && !(*this < i); }
				
					
			}; // end iterator

			/* Navigation conveniences */
			bool move_to_first_child(iterator& arg)
			{ return move_to_first_child(arg.base_reference()); }
			bool move_to_parent(iterator& arg)
			{ return move_to_parent(arg.base_reference()); }
			bool move_to_next_sibling(iterator& arg)
			{ return move_to_next_sibling(arg.base_reference()); }
			
			virtual shared_ptr<file_toplevel_die> toplevel() = 0; /* NOT const */
			virtual const spec::abstract_def& get_spec() const = 0;
			
			// we return the host address size by default
			virtual Dwarf_Half get_address_size() const { return sizeof (void*); }
		};		
		// overloads moved outside struct definition above....
		inline bool operator==(
			const abstract_dieset::position& arg1, 
			const abstract_dieset::position& arg2
		) 
		{ return arg1.p_ds == arg2.p_ds && arg1.off == arg2.off; }
		
		inline bool operator!=(
			const abstract_dieset::position& arg1, 
			const abstract_dieset::position& arg2
		)
		{ return !(arg1 == arg2); }
		
		inline bool operator<=(
			const abstract_dieset::position& arg1, 
			const abstract_dieset::position& arg2
		)
		{ return arg1.p_ds < arg2.p_ds || (arg1.p_ds == arg2.p_ds && arg1.off <= arg2.off); }
		
		inline bool operator<(
			const abstract_dieset::position& arg1, 
			const abstract_dieset::position& arg2
		)
		{ return arg1 <= arg2 && arg1 != arg2; }
		
		inline bool operator>(
			const abstract_dieset::position& arg1, 
			const abstract_dieset::position& arg2
		)
		{ return !(arg1 <= arg2); }
		
		inline bool operator>=(
			const abstract_dieset::position& arg1, 
			const abstract_dieset::position& arg2
		)
		{ return arg1 == arg2 || arg1 > arg2; }

		/* Key interface class. */
		struct basic_die : public std::enable_shared_from_this<basic_die>
		{
			friend std::ostream& operator<<(std::ostream& s, const basic_die& d);
			string to_string() const;
			void print_to_stderr() const; // debugging
			string summary() const;
		
			virtual Dwarf_Off get_offset() const = 0;
			virtual Dwarf_Half get_tag() const = 0;
			
			virtual opt<std::string> get_name() const = 0;
			virtual const spec::abstract_def& get_spec() const = 0;

			// recover a shared_ptr to this DIE, from a plain this ptr
			// -- this is okay, because basic_dies are always refcounted
			std::shared_ptr<basic_die> get_this();
			std::shared_ptr<basic_die> get_this() const;

			// children
			// FIXME: iterator pair //virtual std::pair< > get_children() = 0;
			abstract_dieset::iterator children_begin() 
			{
				if (first_child_offset()) 
				{
					return this->get_first_child()->iterator_here(
						abstract_dieset::siblings_policy_sg);
				}
				else
				{
					return this->get_ds().end(abstract_dieset::siblings_policy_sg);
				}
			}
			abstract_dieset::iterator children_end() 
			{ return this->get_ds().end(abstract_dieset::siblings_policy_sg); }
			
			pair<abstract_dieset::iterator, abstract_dieset::iterator>
			children()
			{ return make_pair(children_begin(), children_end()); }

		protected: // public interface is to downcast
			virtual std::map<Dwarf_Half, encap::attribute_value> get_attrs() = 0;
			
		public:
			/* Navigation API. This is SLOW and therefore deprecated.
			 * The right place to do navigation is in iterators. */
			
			/* get_parent, normal and const */
			virtual std::shared_ptr<basic_die> get_parent() __attribute__((deprecated)) = 0;
			std::shared_ptr<basic_die> get_parent() const __attribute__((deprecated))
			{ return const_cast<basic_die *>(this)->get_parent(); }		   
			
			/* get_first_child, normal and const */
			virtual std::shared_ptr<basic_die> get_first_child() __attribute__((deprecated)) = 0;
			std::shared_ptr<basic_die> get_first_child() const __attribute__((deprecated))
			{ return const_cast<basic_die *>(this)->get_first_child(); }

			/* Functions prefixed "get_" and returning offsets throw No_entry exceptions.
			 * Those without the prefix return an optional.
			 * Those returning pointers return a null pointer. */
			 
			/* get_first_child_offset, normal and nothrow.
			 * There is no need for a non-const version, because we return by value. */
			virtual Dwarf_Off get_first_child_offset() const __attribute__((deprecated)) = 0;
			opt<Dwarf_Off> first_child_offset() const __attribute__((deprecated))
			{ 	try { return this->get_first_child_offset(); } 
				catch (No_entry) { return opt<Dwarf_Off>(); } }
			
			/* get_next_sibling, normal and onst */
			virtual std::shared_ptr<basic_die> get_next_sibling() __attribute__((deprecated)) = 0;
			std::shared_ptr<basic_die> get_next_sibling() const __attribute__((deprecated))
			{ return const_cast<basic_die *>(this)->get_next_sibling(); }

			/* get_next_sibling_offset, normal and nothrow */
			virtual Dwarf_Off get_next_sibling_offset() const __attribute__((deprecated)) = 0;
			opt<Dwarf_Off> next_sibling_offset() const __attribute__((deprecated))
			{ 	try { return this->get_next_sibling_offset(); } 
				catch (No_entry) { return opt<Dwarf_Off>(); } }
			
			/* arguably this also should be deprecated */
			virtual const abstract_dieset& get_ds() const __attribute__((deprecated))
			{ return const_cast<basic_die *>(this)->get_ds(); }
			virtual abstract_dieset& get_ds()  __attribute__((deprecated))= 0;

			/* this is deprecated too! it's inherently slow. */
			abstract_dieset::iterator 
			iterator_here(abstract_dieset::policy& pol = abstract_dieset::default_policy_sg)
			 __attribute__((deprecated));

			// these also belong in navigation
			opt<std::vector<std::string> >
			ident_path_from_root() const __attribute__((deprecated));

			opt<std::vector<std::string> >
			ident_path_from_cu() const __attribute__((deprecated));

			// These two are similar, but still return a path even when
			// some nameless elements are missing. These therefore can't be
			// resolved, and need not be unique within a dieset, 
			// but can be useful for identifying corresponding
			// elements (e.g. shared type DIEs) across multiple diesets.
			std::vector< opt<std::string> >
			opt_ident_path_from_root() const __attribute__((deprecated));

			std::vector < opt<std::string> >
			opt_ident_path_from_cu() const __attribute__((deprecated));

			/* These, and other resolve()-style functions, are not const 
			 * because they need to be able to return references to mutable
			 * found DIEs. FIXME: provide const overloads. */
			std::shared_ptr<basic_die> 
			nearest_enclosing(Dwarf_Half tag) const __attribute__((deprecated));
			std::shared_ptr<spec::basic_die> 
			nearest_enclosing(Dwarf_Half tag) __attribute__((deprecated)); /* non-const version */
			virtual std::shared_ptr<compile_unit_die> 
			enclosing_compile_unit() __attribute__((deprecated));
			std::shared_ptr<const compile_unit_die> 
			enclosing_compile_unit() const  __attribute__((deprecated))
			{ return std::dynamic_pointer_cast<const compile_unit_die>(
					const_cast<basic_die *>(this)->enclosing_compile_unit()); 
			}

			std::shared_ptr<basic_die>
			find_sibling_ancestor_of(std::shared_ptr<basic_die> d) __attribute__((deprecated));
		};
		
		struct with_static_location_die : public virtual basic_die
		{
			struct sym_binding_t 
			{ 
				Dwarf_Off file_relative_start_addr; 
				Dwarf_Unsigned size;
			};
			virtual encap::loclist get_static_location() const;
			opt<Dwarf_Off> contains_addr(
				Dwarf_Addr file_relative_addr,
				sym_binding_t (*sym_resolve)(const std::string& sym, void *arg) = 0, 
				void *arg = 0) const;
			virtual boost::icl::interval_map<Dwarf_Addr, Dwarf_Unsigned> 
			file_relative_intervals(
				sym_binding_t (*sym_resolve)(const std::string& sym, void *arg), 
				void *arg /* = 0 */) const;
			/*virtual std::vector<std::pair<Dwarf_Addr, Dwarf_Addr> >
			file_relative_extents(
				sym_binding_t (*sym_resolve)(const std::string& sym, void *arg) = 0, 
				void *arg = 0) const;*/
		};
	    struct with_named_children_die : public virtual basic_die
        {
            virtual 
            std::shared_ptr<basic_die>
            named_child(const std::string& name);

            template <typename Iter>
            std::shared_ptr<basic_die> 
            resolve(Iter path_pos, Iter path_end);

            std::shared_ptr<basic_die> 
            resolve(const std::string& name);

            template <typename Iter>
            std::shared_ptr<basic_die> 
            scoped_resolve(Iter path_pos, Iter path_end);

            template <typename Iter>
            void
            scoped_resolve_all(Iter path_pos, Iter path_end, 
            	std::vector<std::shared_ptr<basic_die> >& results, int max = 0) 
            {
            	std::shared_ptr<basic_die> found_from_here = resolve(path_pos, path_end);
            	if (found_from_here) 
                { 
                	results.push_back(found_from_here); 
                    if (max != 0 && results.size() == max) return;
                }
                if (this->get_tag() == 0) return;
                else // find our nearest encloser that has named children
                {
                	std::shared_ptr<spec::basic_die> p_encl = this->get_parent();
                    while (std::dynamic_pointer_cast<with_named_children_die>(p_encl) == 0)
                    {
                    	if (p_encl->get_tag() == 0) return;
                    	p_encl = p_encl->get_parent();
                    }
                    // we've found an encl that has named children
                    dynamic_cast<with_named_children_die&>(*p_encl)
                    	.scoped_resolve_all(path_pos, path_end, results);
                    return;
                }
			}            

            std::shared_ptr<basic_die> 
            scoped_resolve(const std::string& name);
        };
        
        class abstract_mutable_dieset : public abstract_dieset
        {
        public:
        	virtual 
            std::shared_ptr<spec::basic_die> 
            insert(Dwarf_Off key, std::shared_ptr<spec::basic_die> val) = 0;
        };

		// still in namespace spec!

/****************************************************************/
/* begin generated ADT includes                                 */
/****************************************************************/
#define forward_decl(t) class t ## _die;
#define declare_base(base) virtual base ## _die
#define base_fragment(base) base ## _die(ds, p_d) {} /* unused */
#define initialize_base(fragment) fragment ## _die(ds, p_d)
#define constructor(fragment) 
#define declare_bases(...) __VA_ARGS__
#define begin_class(fragment, base_inits, ...) \
	struct fragment ## _die : declare_bases(__VA_ARGS__) { \
    	constructor(fragment)
#define base_initializations(...) __VA_ARGS__
#define end_class(fragment) \
	};

#define stored_type_string std::string
#define stored_type_flag bool
#define stored_type_unsigned Dwarf_Unsigned
#define stored_type_signed Dwarf_Signed
#define stored_type_offset Dwarf_Off
#define stored_type_half Dwarf_Half
#define stored_type_ref Dwarf_Off
#define stored_type_tag Dwarf_Half
#define stored_type_loclist dwarf::encap::loclist
#define stored_type_address dwarf::encap::attribute_value::address
#define stored_type_refdie std::shared_ptr<spec::basic_die> 
#define stored_type_refdie_is_type std::shared_ptr<spec::type_die> 
#define stored_type_rangelist dwarf::encap::rangelist

#define attr_optional(name, stored_t) \
	virtual opt<stored_type_ ## stored_t> get_ ## name() const = 0; \
  	opt<stored_type_ ## stored_t> name() const { return get_ ## name(); }

#define super_attr_optional(name, stored_t)

#define attr_mandatory(name, stored_t) \
	virtual stored_type_ ## stored_t get_ ## name() const = 0; \
  	stored_type_ ## stored_t name() const { return get_ ## name(); } 

#define super_attr_mandatory(name, stored_t)

template<Dwarf_Half Tag>
struct has_tag : public std::unary_function<std::shared_ptr<spec::basic_die>, bool>
{
	bool operator()(const std::shared_ptr<spec::basic_die> arg) const
    { 
    	//std::cerr << "testing whether die at " << std::hex << arg->get_offset() << std::dec 
        //	<< " has tag " << Tag << std::endl;
    	return arg->get_tag() == Tag; 
    }
};

template <class Iter>
struct with_iterator_partial_order : public Iter
{
	typedef with_iterator_partial_order self;
	typedef signed long difference_type;
	
	/* HACK: add back in the comparison operators from abstract_dieset::iterator. 
	 * We need this because transform_iterator and
	 * filter_iterator and iterator_adaptor all hide our operator< (et al)
	 * with one based on distance_to(), which we can't define efficiently 
	 * (noting that we have only a partial order anyway, so it'd be a partial
	 * function at best). */
	bool operator<(const self& i) const { return this->base().base() < i.base().base(); }
	bool operator<=(const self& i) const { return this->base().base() <= i.base().base(); }
	bool operator>(const self& i) const { return this->base().base() > i.base().base(); }
	bool operator>=(const self& i) const { return this->base().base() >= i.base().base(); }
	
	// let's try this fancy C++0x constructor forwarding then
	// using Iter::Iter;
	// HACK: the above doesn't yet work in g++, so do "perfect forwarding" instead...
	template <typename... Args> 
	with_iterator_partial_order(Args&&... args)
	// : Iter(args...) {}
	: Iter(std::forward<Args>(args)...) {}
};

//typedef std::unary_function<std::shared_ptr<spec::basic_die>, std::shared_ptr<spec:: arg ## _die > > type_of_dynamic_pointer_cast;
#define child_tag(arg) \
	typedef std::shared_ptr<spec:: arg ## _die >(*type_of_dynamic_pointer_cast_ ## arg)(std::shared_ptr<spec::basic_die> const&); \
	typedef boost::filter_iterator<has_tag< DW_TAG_ ## arg >, spec::abstract_dieset::iterator> arg ## _filter_iterator; \
	typedef boost::transform_iterator<type_of_dynamic_pointer_cast_ ## arg, arg ## _filter_iterator> arg ## _transform_iterator; \
	typedef with_iterator_partial_order<arg ## _transform_iterator> arg ## _iterator; \
    arg ## _iterator arg ## _children_begin() \
	{ return \
		arg ## _iterator( \
		arg ## _transform_iterator(\
		arg ## _filter_iterator(children_begin(), children_end()), \
		 std::dynamic_pointer_cast<spec:: arg ## _die> \
		)); } \
    arg ## _iterator arg ## _children_end() \
	{ return \
		arg ## _iterator(\
		arg ## _transform_iterator(\
		arg ## _filter_iterator(children_end(), children_end()), \
		std::dynamic_pointer_cast<spec:: arg ## _die> \
		)); }

		struct file_toplevel_die : public virtual with_named_children_die
		{
			file_toplevel_die()
			 : vg_cache_is_exhaustive_up_to_offset(0UL),
			   vg_max_offset_on_last_complete_search(0UL)
			{}
			
			struct is_visible
			{
				bool operator()(std::shared_ptr<spec::basic_die> p) const;
			};
			
			child_tag(compile_unit)
			
			typedef concatenating_sequence<abstract_dieset::iterator, std::shared_ptr<basic_die>, std::shared_ptr<basic_die> > grandchildren_sequence_t;
			typedef concatenating_iterator<abstract_dieset::iterator, std::shared_ptr<basic_die>, std::shared_ptr<basic_die> > grandchildren_iterator;
			shared_ptr<grandchildren_sequence_t> grandchildren_sequence();
			
			typedef filter_iterator<is_visible, grandchildren_iterator> 
			visible_grandchildren_iterator;
			
			struct visible_grandchildren_sequence_t;
			shared_ptr<visible_grandchildren_sequence_t> visible_grandchildren_sequence();

			template <typename Iter>
			std::shared_ptr<basic_die>
			resolve_visible(Iter path_pos, Iter path_end/*,
				optional<visible_grandchildren_iterator> start_here
					= optional<visible_grandchildren_iterator>()*/
			);
			template <typename Iter>
			vector< std::shared_ptr<basic_die> >
			resolve_all_visible(Iter path_pos, Iter path_end);

			virtual std::shared_ptr<basic_die>
			visible_named_grandchild(const std::string& name);
			
		private:
			// we cache all the found matching DIEs, 
			// and also record not-founds as optional<...>()
			typedef pair< abstract_dieset::position_and_path, unsigned > vg_cache_rec_t;

			// the function we use for resolve_visible
			optional<vg_cache_rec_t>
			visible_named_grandchild_pos(const std::string& name,
				optional<vg_cache_rec_t> start_here
					= optional<vg_cache_rec_t>(),
				shared_ptr<visible_grandchildren_sequence_t> p_seq 
					= shared_ptr<visible_grandchildren_sequence_t>()); 
		protected:
			virtual
			pair<Dwarf_Off, visible_grandchildren_iterator>
			next_visible_grandchild_with_name(
				const string& name, 
				visible_grandchildren_iterator begin, 
				visible_grandchildren_iterator end
			);
		private:
			std::map<string, optional< vector< vg_cache_rec_t > > > visible_grandchildren_cache;
			Dwarf_Off vg_cache_is_exhaustive_up_to_offset;
			Dwarf_Off vg_max_offset_on_last_complete_search;
			void vg_cache_stamp_reset()
			{ vg_cache_is_exhaustive_up_to_offset = vg_max_offset_on_last_complete_search = 0UL; }
		public:
			void clear_vg_cache() { vg_cache_stamp_reset(); visible_grandchildren_cache.clear(); }
			int clear_vg_cache(const string& key) { vg_cache_stamp_reset(); return visible_grandchildren_cache.erase(key); }
			
			struct visible_grandchildren_sequence_t
			 : /* private */ public grandchildren_sequence_t 
			 // should be private, but make_shared won't work if it is
			{
				/* make_shared is our friend */
				friend shared_ptr<visible_grandchildren_sequence_t> 
				file_toplevel_die::visible_grandchildren_sequence();
				
				visible_grandchildren_sequence_t(const grandchildren_sequence_t& arg)
				: grandchildren_sequence_t(arg) {}
				
				visible_grandchildren_iterator begin() 
				{ 
					return visible_grandchildren_iterator(
						//this->grandchildren_sequence_t::begin(),
						this->grandchildren_sequence_t::begin(), // i.e. concatenating_sequence begin
						this->grandchildren_sequence_t::end()    // i.e. concatenating_sequence end
						);
				}
				visible_grandchildren_iterator end() 
				{ 
					return visible_grandchildren_iterator(
						this->grandchildren_sequence_t::end(),
						//this->grandchildren_sequence_t::begin(),
						this->grandchildren_sequence_t::end()
						);
				}
				
				visible_grandchildren_iterator at(
					const abstract_dieset::position_and_path& pos_and_path,
					unsigned currently_in
				)
				{
					abstract_dieset::iterator cu_child_iterator(
						pos_and_path, 
						abstract_dieset::siblings_policy_sg
					);
					return visible_grandchildren_iterator(
							this->grandchildren_sequence_t::at(
								cu_child_iterator, 
								currently_in),
							this->grandchildren_sequence_t::end()
						);
						
				}
			}; /* end visible_grandchildren_sequence_t */
			
		/* Now some functions for nondecl */
// 		private:
// 			map<vector<string>, position_and_path> first_defn_cache; 
// 		public:
// 			template <typename Iter>
// 			std::shared_ptr<basic_die>
// 			resolve_visible_definition(Iter path_pos, Iter path_end/*,
// 				optional<visible_grandchildren_iterator> start_here
// 					= optional<visible_grandchildren_iterator>()*/
// 			);
// 			
		};
// 		
// 		template <typename Iter>
// 		std::shared_ptr<basic_die>
// 		resolve_visible_definition(Iter path_pos, Iter path_end/*,
// 			optional<visible_grandchildren_iterator> start_here
// 				= optional<visible_grandchildren_iterator>()*/
// 		)
// 		{
// 			/* Naive operation is to resolve_all_visible, and take
// 			 * the first definition. */
// 		}
		
// define additional virtual dies first -- note that
// some virtual DIEs are defined manually (above)
/* program_element_die */
begin_class(program_element, base_initializations(initialize_base(basic)), declare_base(basic))
        attr_optional(decl_file, unsigned)
        attr_optional(decl_line, unsigned)
        attr_optional(decl_column, unsigned)
        attr_optional(prototyped, flag)
        attr_optional(declaration, flag)
        attr_optional(external, flag)
        attr_optional(visibility, unsigned)
        attr_optional(artificial, flag)
end_class(program_element)
/* with_type_describing_layout_die */		
		struct with_type_describing_layout_die : public virtual program_element_die
		{
			virtual opt<std::shared_ptr<spec::type_die> > get_type() const = 0;
		};
/* with_dynamic_location_die */
		struct with_dynamic_location_die : public virtual with_type_describing_layout_die
		{
			virtual opt<Dwarf_Off> contains_addr(
					Dwarf_Addr absolute_addr,
					Dwarf_Signed instantiating_instance_addr,
					Dwarf_Off dieset_relative_ip,
					dwarf::lib::regs *p_regs = 0) const = 0;
			/* We define two variants of the contains_addr logic: 
			 * one suitable for stack-based locations (fp/variable)
			 * and another for object-based locations (member/inheritance)
			 * and each derived class should pick one! */
		protected:
			opt<Dwarf_Off> contains_addr_on_stack(
					Dwarf_Addr absolute_addr,
					Dwarf_Signed instantiating_instance_addr,
					Dwarf_Off dieset_relative_ip,
					dwarf::lib::regs *p_regs = 0) const;
			opt<Dwarf_Off> contains_addr_in_object(
					Dwarf_Addr absolute_addr,
					Dwarf_Signed instantiating_instance_addr,
					Dwarf_Off dieset_relative_ip,
					dwarf::lib::regs *p_regs = 0) const;
		public:
			virtual std::shared_ptr<spec::program_element_die> 
			get_instantiating_definition() const;
			
			virtual Dwarf_Addr calculate_addr(
				Dwarf_Addr instantiating_instance_location,
				Dwarf_Off dieset_relative_ip,
				dwarf::lib::regs *p_regs = 0) const = 0;
			
			/** This gets a location list describing the location of the thing, 
			    assuming that the instantiating_instance_location has been pushed
			    onto the operand stack. */
			virtual encap::loclist get_dynamic_location() const = 0;
		protected:
			/* ditto */
			virtual Dwarf_Addr calculate_addr_on_stack(
				Dwarf_Addr instantiating_instance_location,
				Dwarf_Off dieset_relative_ip,
				dwarf::lib::regs *p_regs = 0) const;
			virtual Dwarf_Addr calculate_addr_in_object(
				Dwarf_Addr instantiating_instance_location,
				Dwarf_Off dieset_relative_ip,
				dwarf::lib::regs *p_regs = 0) const;
		public:
			
			/* virtual Dwarf_Addr calculate_addr(
				Dwarf_Signed frame_base_addr,
				Dwarf_Off dieset_relative_ip,
				dwarf::lib::regs *p_regs = 0) const;*/
		};
 /* type_die */
begin_class(type, base_initializations(initialize_base(program_element)), declare_base(program_element))
        attr_optional(byte_size, unsigned)
        virtual opt<Dwarf_Unsigned> calculate_byte_size() const;
        virtual bool is_rep_compatible(std::shared_ptr<type_die> arg) const;
		virtual std::shared_ptr<type_die> get_concrete_type() const;
		virtual std::shared_ptr<type_die> get_unqualified_type() const;
        std::shared_ptr<type_die> get_concrete_type();
end_class(type)
/* type_chain_die */
begin_class(type_chain, base_initializations(initialize_base(type)), declare_base(type))
        attr_optional(type, refdie_is_type)
        opt<Dwarf_Unsigned> calculate_byte_size() const;
        std::shared_ptr<type_die> get_concrete_type() const;
end_class(type_chain)
/* type_describing_subprogram_die */
begin_class(type_describing_subprogram, base_initializations(initialize_base(type)), declare_base(type))
        attr_optional(type, refdie_is_type)
        virtual bool is_variadic() const;
end_class(type_describing_subprogram)
/* address_holding_type_die */
begin_class(address_holding_type, base_initializations(initialize_base(type_chain)), declare_base(type_chain))
        attr_optional(address_class, unsigned)
        opt<Dwarf_Unsigned> calculate_byte_size() const;
        std::shared_ptr<type_die> get_concrete_type() const;
end_class(type_chain)
/* qualified_type_die */
begin_class(qualified_type, base_initializations(initialize_base(type_chain)), declare_base(type_chain))
        virtual std::shared_ptr<type_die> get_unqualified_type() const;
        std::shared_ptr<type_die> get_unqualified_type();
end_class(qualified_type)
/* with_data_members_die */
begin_class(with_data_members, base_initializations(initialize_base(type), initialize_base(with_named_children)), declare_base(type), declare_base(with_named_children))
        child_tag(member)
		shared_ptr<type_die> find_my_own_definition() const; // for turning declarations into defns
end_class(with_data_members)

#define has_stack_based_location \
	opt<Dwarf_Off> contains_addr( \
                    Dwarf_Addr aa, \
                    Dwarf_Signed fb, \
                    Dwarf_Off dr_ip, \
                    dwarf::lib::regs *p_regs) const \
					{ return contains_addr_on_stack(aa, fb, dr_ip, p_regs); } \
	Dwarf_Addr calculate_addr( \
				Dwarf_Addr fb, \
				Dwarf_Off dr_ip, \
				dwarf::lib::regs *p_regs = 0) const \
				{ return calculate_addr_on_stack(fb, dr_ip, p_regs); } \
	encap::loclist get_dynamic_location() const;
#define has_object_based_location \
	opt<Dwarf_Off> contains_addr( \
                    Dwarf_Addr aa, \
                    Dwarf_Signed io, \
                    Dwarf_Off dr_ip, \
                    dwarf::lib::regs *p_regs) const \
					{ return contains_addr_in_object(aa, io, dr_ip, p_regs); } \
	Dwarf_Addr calculate_addr( \
				Dwarf_Addr io, \
				Dwarf_Off dr_ip, \
				dwarf::lib::regs *p_regs = 0) const \
				{ return calculate_addr_in_object(io, dr_ip, p_regs); } \
	encap::loclist get_dynamic_location() const;
#define extra_decls_compile_unit \
		encap::rangelist normalize_rangelist(const encap::rangelist& rangelist) const; \
		opt<Dwarf_Unsigned> implicit_array_base() const; \
		shared_ptr<type_die> implicit_enum_base_type() const; \
		virtual Dwarf_Half get_address_size() const { return this->get_ds().get_address_size(); } \
		virtual std::string source_file_name(unsigned o) const = 0; \
		virtual unsigned source_file_count() const = 0; \
		abstract_dieset::iterator children_begin(); /* faster than the norm */ \
		abstract_dieset::iterator children_end(); 
#define extra_decls_subprogram \
        opt< std::pair<Dwarf_Off, std::shared_ptr<spec::with_dynamic_location_die> > > \
        contains_addr_as_frame_local_or_argument( \
                    Dwarf_Addr absolute_addr, \
                    Dwarf_Off dieset_relative_ip, \
                    Dwarf_Signed *out_frame_base, \
                    dwarf::lib::regs *p_regs = 0) const; 
#define extra_decls_variable \
        bool has_static_storage() const; \
		has_stack_based_location
#define extra_decls_formal_parameter \
		has_stack_based_location
#define extra_decls_array_type \
		opt<Dwarf_Unsigned> element_count() const; \
        opt<Dwarf_Unsigned> calculate_byte_size() const; \
        bool is_rep_compatible(std::shared_ptr<type_die> arg) const; \
		shared_ptr<type_die> ultimate_element_type() const; \
		opt<Dwarf_Unsigned> ultimate_element_count() const;  \
		std::shared_ptr<type_die> get_concrete_type() const;
#define extra_decls_pointer_type \
         bool is_rep_compatible(std::shared_ptr<type_die> arg) const;
#define extra_decls_reference_type \
        bool is_rep_compatible(std::shared_ptr<type_die> arg) const;
#define extra_decls_base_type \
		bool is_rep_compatible(std::shared_ptr<type_die> arg) const;
#define extra_decls_structure_type \
		opt<Dwarf_Unsigned> calculate_byte_size() const; \
		bool is_rep_compatible(std::shared_ptr<type_die> arg) const; 
#define extra_decls_union_type \
		bool is_rep_compatible(std::shared_ptr<type_die> arg) const; 
#define extra_decls_class_type \
		bool is_rep_compatible(std::shared_ptr<type_die> arg) const; 
#define extra_decls_enumeration_type \
		bool is_rep_compatible(std::shared_ptr<type_die> arg) const;
#define extra_decls_subroutine_type \
		bool is_rep_compatible(std::shared_ptr<type_die> arg) const; 
#define extra_decls_member \
		opt<Dwarf_Unsigned> byte_offset_in_enclosing_type() const; \
		has_object_based_location
#define extra_decls_inheritance \
		opt<Dwarf_Unsigned> byte_offset_in_enclosing_type() const; \
		has_object_based_location

#include "dwarf3-adt.h"

#undef extra_decls_inheritance
#undef extra_decls_member
#undef extra_decls_subroutine_type
#undef extra_decls_enumeration_type
#undef extra_decls_class_type
#undef extra_decls_union_type
#undef extra_decls_structure_type
#undef extra_decls_base_type
#undef extra_decls_reference_type
#undef extra_decls_pointer_type
#undef extra_decls_array_type
#undef extra_decls_variable
#undef extra_decls_formal_parameter
#undef extra_decls_compile_unit

#undef forward_decl
#undef declare_base
#undef base_fragment
#undef initialize_base
#undef constructor
#undef begin_class
#undef base_initializations
#undef declare_base
#undef declare_bases
#undef end_class
#undef stored_type_string
#undef stored_type_flag
#undef stored_type_unsigned
#undef stored_type_signed
#undef stored_type_offset
#undef stored_type_half
#undef stored_type_ref
#undef stored_type_tag
#undef stored_type_loclist
#undef stored_type_address
#undef stored_type_refdie
#undef stored_type_refdie_is_type
#undef stored_type_rangelist
#undef attr_optional
#undef attr_mandatory
#undef super_attr_optional
#undef super_attr_mandatory
#undef child_tag

/****************************************************************/
/* end generated ADT includes                                   */
/****************************************************************/

        template <typename Iter>
        std::shared_ptr<basic_die> 
        with_named_children_die::resolve(Iter path_pos, Iter path_end)
        {
            /* We can't return "this" because it's not a shared_ptr, and we don't
             * want to create a duplicate count by creating a new shared ptr from it.
             * So we ask our containing dieset to make a new one for us. In the case
             * of lib::die, this will create an entirely new Die. In the case of
             * encap::die, it will just retrieve the stored shared_ptr and use that. */
            this->get_offset();
            this->get_ds();
            if (path_pos == path_end) return this->get_ds().operator[](this->get_offset());
            Iter cur_plus_one = path_pos; cur_plus_one++;
            if (cur_plus_one == path_end) return named_child(*path_pos);
            else
            {
                auto found = named_child(*path_pos);
                if (!found) return shared_ptr<basic_die>();
                auto p_next_hop =
                    std::dynamic_pointer_cast<with_named_children_die>(found);
                if (!p_next_hop) return shared_ptr<basic_die>();
                else return p_next_hop->resolve(++path_pos, path_end);
            }
        }
        
        template <typename Iter>
        std::shared_ptr<basic_die> 
        with_named_children_die::scoped_resolve(Iter path_pos, Iter path_end)
        {
            if (resolve(path_pos, path_end)) return this->get_ds().operator[](this->get_offset());
            if (this->get_tag() == 0) return shared_ptr<basic_die>();
            else // find our nearest encloser that has named children
            {
                std::shared_ptr<spec::basic_die> p_encl = this->get_parent();
                while (std::dynamic_pointer_cast<with_named_children_die>(p_encl) == 0)
                {
                    if (p_encl->get_tag() == 0) return shared_ptr<basic_die>();
                    p_encl = p_encl->get_parent();
                }
                // we've found an encl that has named children
                return std::dynamic_pointer_cast<with_named_children_die>(p_encl)
                    ->scoped_resolve(path_pos, path_end);
            }
		}
		
		template <typename Iter>
		std::shared_ptr<basic_die>
		file_toplevel_die::resolve_visible(Iter path_pos, Iter path_end/*,
			optional<visible_grandchildren_iterator> opt_start_here*/)
		{
			if (path_pos == path_end) return get_this();
			else 
			{
				auto p_next_hop = visible_named_grandchild(*path_pos/*, opt_start_here*/);
				auto p_next_hop_with_children
				 = dynamic_pointer_cast<with_named_children_die>(p_next_hop);
				if (!p_next_hop) return p_next_hop;
				else
				{
					/* NOTE: the wart with the recursion here is that 
					 * we can't call resolve() on all DIEs that we might want
					 * to terminate on, because the last hop needn't be a
					 * with_named_children DIE. */
					if (!p_next_hop_with_children)
					{
						// it's okay if we're terminating here
						if (path_pos + 1 == path_end) return p_next_hop;
						else return shared_ptr<basic_die>();
					}
					else
					{
						assert(p_next_hop_with_children);
						return p_next_hop_with_children->resolve(++path_pos, path_end);
					}
				}
			}
		}
	
// 			
//             is_visible visible;
//             std::shared_ptr<basic_die> found;
//             for (auto i_cu = this->compile_unit_children_begin();
//                     i_cu != this->compile_unit_children_end(); i_cu++)
//             {
//                 if (path_pos == path_end) { found = this->get_this(); break; }
//                 auto found_under_cu = (*i_cu)->named_child(*path_pos);
// 
//                 Iter cur_plus_one = path_pos; ++cur_plus_one;
//                 if (cur_plus_one == path_end && found_under_cu
//                         && visible(found_under_cu))
//                 { found = found_under_cu; break; }
//                 else
//                 {
//                     if (!found_under_cu || 
//                             !visible(found_under_cu)) continue;
//                     auto p_next_hop =
//                         std::dynamic_pointer_cast<with_named_children_die>(found_under_cu);
//                     if (!p_next_hop) continue; // try next compile unit
//                     else 
//                     { 
//                         auto found_recursive = p_next_hop->resolve(++path_pos, path_end);
//                         if (found_recursive) { found = found_recursive; break; }
//                         // else continue
//                     }
//                 }
//             }
//             if (found) return found; else return std::shared_ptr<basic_die>();
//         }
		
        template <typename Iter>
        vector< std::shared_ptr<basic_die> >
        file_toplevel_die::resolve_all_visible(Iter path_pos, Iter path_end)
        {
			//if (path_pos == path_end) return vectorget_this();
			
			// since we don't recurse (we call down to plain old resolve() instead),
			// this shouldn't happen
			assert(path_pos != path_end);
			
			/* This is like resolve_visible but we push results into a vector
			 * and keep going. */
			optional<vg_cache_rec_t> next_start_pos;
			shared_ptr<basic_die> last_resolved;
			vector< std::shared_ptr<basic_die> > all_resolved;
			auto vg_seq = visible_grandchildren_sequence();
			
			auto pos_is_end = [this](const vg_cache_rec_t& arg) {
				return abstract_dieset::iterator(arg.first) == this->get_ds().end();
			};
			
			do
			{
				auto prev_start_pos = next_start_pos;
				assert(!prev_start_pos || !pos_is_end(*prev_start_pos));
				next_start_pos = visible_named_grandchild_pos(
					*path_pos,
					next_start_pos,
					vg_seq);
				assert(next_start_pos);
				if (next_start_pos == prev_start_pos)
				{
					// fail! debug
					abstract_dieset::iterator stuck_here(next_start_pos->first);
					clog << "Resolving " << *path_pos << ", stuck here: " << (*stuck_here)->summary() << endl;
					assert(false);
				}
				if (!pos_is_end(*next_start_pos))
				{
					// means we resolved the first name component... a potential match
					
					/* This means we have a position which matches the name, so 
					 * launch the recursive case. */
					auto i_next_hop = abstract_dieset::iterator(next_start_pos->first);
					if (i_next_hop != get_ds().end())
					{
						auto p_next_hop = *i_next_hop;
						auto p_next_hop_with_children
						 = dynamic_pointer_cast<with_named_children_die>(p_next_hop);
						/* NOTE: the wart with the recursion here is that 
						 * we can't call resolve() on all DIEs that we might want
						 * to terminate on, because the last hop needn't be a
						 * with_named_children DIE. */
						if (p_next_hop && !p_next_hop_with_children
							&& (path_pos+1) == path_end) last_resolved = p_next_hop;
						else
						{
							assert(p_next_hop_with_children);
							last_resolved = p_next_hop_with_children->resolve(path_pos + 1, path_end);
						}
						// either way, we wrote to last_resolved
						if (last_resolved) all_resolved.push_back(last_resolved);
					}
				}
				
				assert(next_start_pos);
				
			} while (!pos_is_end(*next_start_pos));
			
			return all_resolved;
			
// 			while (!start_here || last_resolved)
// 			{
// 				if (start_here)
// 				{
// 					/* This means we have a position which matches the name, so 
// 					 * launch the recursive case. */
// 					auto i_next_hop = abstract_dieset::iterator(start_here->first);
// 					if (i_next_hop != get_ds().end())
// 					{
// 						auto p_next_hop = *i_next_hop;
// 						auto p_next_hop_with_children
// 						 = dynamic_pointer_cast<with_named_children_die>(p_next_hop);
// 						/* NOTE: the wart with the recursion here is that 
// 						 * we can't call resolve() on all DIEs that we might want
// 						 * to terminate on, because the last hop needn't be a
// 						 * with_named_children DIE. */
// 						if (p_next_hop && !p_next_hop_with_children
// 							&& (path_pos+1) == path_end) last_resolved = p_next_hop;
// 						else
// 						{
// 							assert(p_next_hop_with_children);
// 							last_resolved = p_next_hop_with_children->resolve(path_pos + 1, path_end);
// 						}
// 					}
// 				}
// 				if (last_resolved) all_resolved.push_back(last_resolved);
// 				
// 				// now move on to the next visible grandchild with matching name
// 				start_here = visible_named_grandchild_pos(*path_pos,
// 					start_here,
// 					vg_seq);
// 			}
// 			return all_resolved;
		}
// 			if (path_pos == path_end) return get_this();
// 			else 
// 			{
// 				auto p_next_hop = visible_named_grandchild(*path_pos);
// 				auto p_next_hop_with_children
// 				 = dynamic_pointer_cast<with_named_children_die>(p_next_hop);
// 				if (!p_next_hop) return p_next_hop;
// 				else
// 				{
// 					/* NOTE: the wart with the recursion here is that 
// 					 * we can't call resolve() on all DIEs that we might want
// 					 * to terminate on, because the last hop needn't be a
// 					 * with_named_children DIE. */
// 					if (p_next_hop && !p_next_hop_with_children
// 						&& ++path_pos == path_end) return p_next_hop;
// 					assert(p_next_hop_with_children);
// 					return p_next_hop_with_children->resolve(++path_pos, path_end);
// 				}
// 			}
			
//             is_visible visible;
//             vector< std::shared_ptr<basic_die> > found;
//             for (auto i_cu = this->compile_unit_children_begin();
//                     i_cu != this->compile_unit_children_end(); i_cu++)
//             {
//                 if (path_pos == path_end) { found.push_back(this->get_this()); continue; }
//                 auto found_under_cu = (*i_cu)->named_child(*path_pos);
// 
//                 Iter cur_plus_one = path_pos; ++cur_plus_one;
//                 if (cur_plus_one == path_end && found_under_cu
//                         && visible(found_under_cu))
//                 { found.push_back(found_under_cu); continue; }
//                 else
//                 {
//                     if (!found_under_cu || 
//                             !visible(found_under_cu)) continue;
//                     auto p_next_hop =
//                         std::dynamic_pointer_cast<with_named_children_die>(found_under_cu);
//                     if (!p_next_hop) continue; // try next compile unit
//                     else 
//                     { 
//                         auto found_recursive = p_next_hop->resolve(++path_pos, path_end);
//                         if (found_recursive) 
// 						{ 
// 							//std::copy(found_recursive.begin(), found_recursive.end(),
// 							//std::back_inserter(found)); 
// 							found.push_back(found_recursive);
// 						}
//                         // else continue
//                     }
//                 }
//             }
//             return found;
//         }		
// 		class factory
// 		{
// 		public:
// 			template<class Rep>  // specialise this on a per-rep basis
// 			static typename Rep::factory_type& 
// 				get_factory(const dwarf::spec::abstract_def& spec) //__attribute__((no_return))
// 			{ throw Bad_spec(); }
// 		};

		// HACK: declared in util.hpp
		template <typename Action>
		void
		abstract_dieset::for_all_identical_types(
			shared_ptr<type_die> p_t,
			const Action& action
		)
		{
			auto opt_ident_path = p_t->ident_path_from_cu();
			vector< shared_ptr<type_die> > ts;
			if (!opt_ident_path) ts.push_back(p_t);
			else
			{
				for (auto i_cu = this->toplevel()->compile_unit_children_begin();
					i_cu != this->toplevel()->compile_unit_children_end(); ++i_cu)
				{
					auto candidate = (*i_cu)->resolve(opt_ident_path->begin(), opt_ident_path->end());
					if (dynamic_pointer_cast<type_die>(candidate))
					{
						ts.push_back(dynamic_pointer_cast<type_die>(candidate));
					}
				}
			}
			for (auto i_t = ts.begin(); i_t != ts.end(); ++i_t)
			{
				action(*i_t);
			}
		}

	}
}

#endif
