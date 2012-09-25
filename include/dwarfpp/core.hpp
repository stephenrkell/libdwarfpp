/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * core.hpp: high-performance C++ wrappers around libdwarf C API.
 *
 * Copyright (c) 2008--12, Stephen Kell.
 */

#ifndef DWARFPP_CORE_HPP_
#define DWARFPP_CORE_HPP_

#include <iostream>
#include <utility>
#include <memory>
#include <cassert>
#include <map>
#include <queue>
#include <boost/optional.hpp>
#include <boost/smart_ptr/intrusive_ptr.hpp>
#include <boost/iterator/iterator_facade.hpp>
#include <libelf.h>
#include "spec.hpp"

namespace dwarf
{
	namespace lib
	{
		using std::unique_ptr;
		using std::pair;
		using std::string;
		using std::map;
		using std::deque;
		using boost::optional;
		using boost::intrusive_ptr;
		using std::dynamic_pointer_cast;
	
		extern "C"
		{
			// HACK: libdwarf.h declares struct Elf opaquely, and we don't
			// want it in the dwarf::lib namespace, so preprocess this.
			#define Elf Elf_opaque_in_libdwarf
			#include <libdwarf.h>
			#undef Elf
		}

#ifndef NO_TLS
		__thread Dwarf_Error current_dwarf_error;
#else
#warning "No TLS, so DWARF error reporting is not thread-safe."
		Dwarf_Error current_dwarf_error;
#endif
		
		// FIXME: temporary compatibility hack, please remove
		typedef ::dwarf::spec::abstract_def spec;
		
		// forward declarations
		struct root_die;
		struct basic_die; // the object that we heap-allocate when we want to share a refcounted DIE
		
		// template... typename DerefAs
		struct iterator_base; // our handle / ptr_type union
		
		// more pasted from lib
		struct No_entry {
			No_entry() {}
		};	
		
		/* FIXME: clean up errors properly. It's complicated. A Dwarf_Error is a handle
		 * that needs to be dwarf_dealloc'd, but there are two exceptions:
		 * errors returned by dwarf_init() and dwarf_elf_init() need to be free()d. 
		 * In other words, these errors need different deleters. 
		 * We should unique_ptr'ify each Dwarf_Error at the point where it arises,
		 * so that we can specify this alternate handling. */
		 
		/* The point of this class is to make a self-contained throwable object
		 * out of a Dwarf_Error handle. FIXME: why do we bundle the Ptr but not the
		 * error function? */
		struct Error {
			Dwarf_Error e;
			Dwarf_Ptr arg;
			Error(Dwarf_Error e, Dwarf_Ptr arg) : e(e), arg(arg) {}
			virtual ~Error() 
			{ /*dwarf_dealloc((Dwarf_Debug) arg, e, DW_DLA_ERROR); */ /* TODO: Fix segfault here */	}
		};
		
		/* What follows is a mechanical translation of libdwarf,
		 * plus destruction logic from the docs. */
		typedef struct Dwarf_Debug_s*      Dwarf_Debug;
		
		struct Debug
		{
			typedef Dwarf_Debug raw_handle_type;
			typedef Dwarf_Debug_s opaque_type;
			struct deleter
			{
				void operator ()(raw_handle_type arg) const;
			};
			typedef unique_ptr<opaque_type, deleter> handle_type;
			
			handle_type handle;
			
			// define constructors analogous to the libdwarf resource-acquisition functions
			Debug(int fd); /* FIXME: release Elf handle implicitly left open after dwarf_finish(). */
			Debug(Elf *elf);
		};

		void exception_error_handler(Dwarf_Error error, Dwarf_Ptr errarg);
		
		typedef struct Dwarf_Die_s*        Dwarf_Die;
		struct Die
		{
			typedef Dwarf_Die raw_handle_type;
			typedef Dwarf_Die_s opaque_type;
			struct deleter
			{
				void operator ()(raw_handle_type arg) const { /* nothing needed for DIE deletion? */ }
			};
			typedef unique_ptr<opaque_type, deleter> handle_type;
			
			handle_type handle;
			
			// to avoid making exception handling compulsory, 
			// we provide static "maybe" constructor functions...
			static inline handle_type 
			try_construct(root_die& r, const iterator_base& die); /* siblingof */
			static inline handle_type 
			try_construct(root_die& r); /* siblingof with null die */
			static inline handle_type 
			try_construct(const iterator_base& die); /* child */
			static inline handle_type 
			try_construct(root_die& r, Dwarf_Off off); /* offdie */
			// ... and an "upgrade" constructor that is guaranteed not to fail
			inline Die(handle_type h);
			// ... then the "normal" constructors, that throw exceptions on failure
			inline Die(root_die& r, const iterator_base& die); /* siblingof */
			inline explicit Die(root_die& r); /* siblingof in the root case */
			inline explicit Die(const iterator_base& die); /* child */
			inline explicit Die(root_die& r, Dwarf_Off off); /* offdie */
		};
		
		/* Note: there are two ways of getting attributes out of libdwarf:
		 * dwarf_attr and dwarf_attrlist. The former returns individual attributes
		 * and is the one we tackle here.
		 * 
		 * It's problematic that we need dbg in order to do the dealloc.
		 * We justify copying the raw handle because Attributes are supposed to
		 * be transient. So there is not much point doing reference counting
		 * on the dbg -- the copy of the handle returned by dwarf_init
		 * should outlive this one. */
		 
		/* callign dwarf_attrlist, libdwarf allocates for us a block
		 * of Dwarf_Attributes. 
		 *
		 * This is problematic because we
		 * need to remember the length somewhere. Wherever the unique_ptr
		 * goes, its associated length needs to go too. This is handled by
		 * unique_ptr because deleters are allowed to have state. So we just
		 * put the count into the deleter and manually construct the deleter
		 * when constructing the unique_ptr. */

		typedef struct Dwarf_Attribute_s*  Dwarf_Attribute;
		struct Attribute
		{
			typedef Dwarf_Attribute raw_handle_type;
			typedef Dwarf_Attribute_s opaque_type;
			struct deleter
			{
				Debug::raw_handle_type dbg;
				deleter(Debug::raw_handle_type dbg) : dbg(dbg) {}
				void operator()(raw_handle_type arg) const
				{
					dwarf_dealloc(dbg, arg, DW_DLA_ATTR);
				}
			};
			typedef unique_ptr<opaque_type, deleter> handle_type;
			
			handle_type handle;
			static inline handle_type 
			try_construct(const iterator_base& it, Dwarf_Half attr);
			inline explicit Attribute(const iterator_base& it, Dwarf_Half attr);
		};
		
		/* NOTE: the "block" of attribute descriptors that libdwarf creates 
		 * is another kind of resource that needs the unique_ptr treatment.
		 * We save this (for now) until iterator_base::attributes_here. */
		
		/* FIXME: sim. for the other handle types:
		typedef struct Dwarf_Line_s*       Dwarf_Line;
		typedef struct Dwarf_Global_s*     Dwarf_Global;
		typedef struct Dwarf_Func_s*       Dwarf_Func;
		typedef struct Dwarf_Type_s*       Dwarf_Type;
		typedef struct Dwarf_Var_s*        Dwarf_Var;
		typedef struct Dwarf_Weak_s*       Dwarf_Weak;
		typedef struct Dwarf_Error_s*      Dwarf_Error;
		typedef struct Dwarf_Attribute_s*  Dwarf_Attribute;
		typedef struct Dwarf_Abbrev_s*       Dwarf_Abbrev;
		typedef struct Dwarf_Fde_s*         Dwarf_Fde;
		typedef struct Dwarf_Cie_s*         Dwarf_Cie;
		typedef struct Dwarf_Arange_s*       Dwarf_Arange;
		*/
		
		// placeholder until we retrofit all this onto adt
		class dummy_adt_basic_die
		{
			virtual const char *get_name() const { return nullptr; }
		};
		
		/* What's this? It's the object that we heap-allocate 
		 * when we want to share a refcounted DIE. It embeds a 
		 * Die, i.e. a handle wrapper. Subclasses of this 
		 * are allowed (and encouraged) to include extra fields
		 * which (say) cache useful information like field offsets
		 * (of children). 
		 * 
		 * It's libdwarf-specific! */
		class basic_die : public virtual dummy_adt_basic_die
		{
			friend struct iterator_base; // needed why
		protected:
			// we need to embed a refcount
			unsigned refcount;
			
			// we obviously need this
			Die d;
			// these are necessary to make an iterator out of a basic_die
			// -- FIXME: where do we do this? 
			unsigned depth;
			//root_die *p_root; // FIXME: no! we want to remove this, to allow stacking diesets
			/* Nothing else! This class is here to act as the base for
			 * state-holding subclasses that are optimised for particular
			 * purposes, e.g. fast local/parameter location.  */
			
			// remember! payload = handle + shared count + extra state
			
		public:
			template <typename Iter>
			basic_die(root_die& r, const Iter& i);
			
			// begin() / end() / sequence() here?
			// to mean "sequence of DIEs under here"?
			// Or put those methods on iterator?
			
			unsigned get_depth() { return depth; }
			root_die& get_root() { assert(p_root); return *p_root; } // FAILS to compile because we commented out p_root above
						
			friend std::ostream& operator<<(std::ostream& s, const basic_die& d);
			friend void intrusive_ptr_add_ref(basic_die *p);
			friend void intrusive_ptr_release(basic_die *p);
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
		
		template <typename DerefAs = basic_die> struct iterator_df;
		template <typename DerefAs = basic_die> struct iterator_bf;
		template <typename DerefAs = basic_die> struct iterator_sibs;
		
		// FIXME: this is not libdwarf-agnostic! 
		// Why did we separate root_die and basic_root_die?
		// Anyway, they are now one and the same. But 
		// this is a root_die for libdwarf, not for encap. 
		// ** Could we use it for encap too, with a null Debug?
		// --- yes, where "Debug" just means "root resource", and encap doesn't have one
		// --- we are still leaking libdwarf design through our "abstract" interface
		// ------ can we do anything about this? 
		// --- what methods do handles provide? 
		// ** Is it different from a dieset? no, I don't think so
		// ** Can we abstract out a core base class?
		struct root_die
		{
			friend struct iterator_base;
			friend struct Die;
			friend struct Attribute; // so it can get the raw Dwarf_Debug for its deleter
		protected:
			typedef intrusive_ptr<basic_die> ptr_type;

			map<Dwarf_Off, Dwarf_Off> parent_of;
			map<Dwarf_Off, ptr_type > sticky_dies; // compile_unit_die is always sticky
			Debug dbg;
			Dwarf_Off current_cu_offset; // 0 means none

			virtual ptr_type make_payload(const iterator_base& it)/* = 0*/;
			virtual bool is_sticky(const iterator_base& it)/* = 0*/;
			
		public:
			root_die(int fd) : dbg(fd), current_cu_offset(0UL) {}
		
			// HMM -- how to make these policy-agnostic? 
			// Has to be template <typename Iter = iterator_df<> > Iter begin()
			// because we return an instance of the named type. 
			// OR we could go back to the policy-is-separate design.
			// That has the problem that we don't know where to store copied policies.
			// And that inlining is less likely to happen.
			template <typename Iter = iterator_df<> >
			Iter begin();
			template <typename Iter = iterator_df<> >
			Iter end();
			template <typename Iter = iterator_df<> >
			pair<Iter, Iter> sequence();
			
			template <typename Iter = iterator_df<> >
			Iter begin() const { return const_cast<root_die*>(this)->begin<Iter>(); } 
			template <typename Iter = iterator_df<> >
			Iter end() const { return const_cast<root_die*>(this)->end<Iter>(); } 
			template <typename Iter = iterator_df<> >
			pair<Iter, Iter> sequence() const 
			{ return const_cast<root_die*>(this)->sequence<Iter>(); } 
			
			/* This is the expensive version. */
			template <typename Iter = iterator_df<> >
			Iter find(Dwarf_Off off);
			/* This is the cheap version -- must give a valid offset. */
			template <typename Iter = iterator_df<> >
			Iter pos(Dwarf_Off off);
			
			::Elf *get_elf(); // hmm: lib-only?

			// NOTE: want to avoid virtual dispatch on these
			bool move_to_parent(iterator_base& it);
			bool move_to_first_child(iterator_base& it);
			bool move_to_next_sibling(iterator_base& it);
			
			// this is libdwarf-specific stuff.
			// libdwarf has this weird stateful CU API
			optional<Dwarf_Unsigned> last_seen_cu_header_length;
			optional<Dwarf_Half> last_seen_version_stamp;
			optional<Dwarf_Unsigned> last_seen_abbrev_offset;
			optional<Dwarf_Half> last_seen_address_size;
			optional<Dwarf_Half> last_seen_offset_size;
			optional<Dwarf_Half> last_seen_extension_size;
			optional<Dwarf_Unsigned> last_seen_next_cu_header;
			bool advance_cu_context();
			bool clear_cu_context();
		protected:
			bool set_subsequent_cu_context(Dwarf_Off off); // helper
		public:
			bool set_cu_context(Dwarf_Off off);
			
			friend struct compile_unit_die; // redundant because we're struct, but future hint
			
			// print the whole lot
			friend std::ostream& operator<<(std::ostream& s, const root_die& d);
		};	
		std::ostream& operator<<(std::ostream& s, const root_die& d);
		
		struct compile_unit_die : public basic_die
		{
			/* Like all payload types, we are constructed from an iterator.
			 * At the point when we are called, 
			 * the Die handle in i is the result of a call to first_die, 
			 * made when the CU state is pointing at the relevant CU.  */
			// FIXME: generify iterator_df -- this can be a template instead
			compile_unit_die(root_die& r, const iterator_df<>& i) : basic_die(r, i)
			{
				cu_header_length = *r.last_seen_cu_header_length;
				version_stamp = *r.last_seen_version_stamp;
				abbrev_offset = *r.last_seen_abbrev_offset;
				address_size = *r.last_seen_address_size;
				offset_size = *r.last_seen_offset_size;
				extension_size = *r.last_seen_extension_size;
				next_cu_header = *r.last_seen_next_cu_header;
			}
			
			/* We define fields and getters for the per-CU info (NOT attributes) 
			 * available from libdwarf. */
			Dwarf_Unsigned cu_header_length;
			Dwarf_Half version_stamp;
			Dwarf_Unsigned abbrev_offset;
			Dwarf_Half address_size;
			Dwarf_Half offset_size;
			Dwarf_Half extension_size;
			Dwarf_Unsigned next_cu_header;
			
			Dwarf_Unsigned get_cu_header_length() const { return cu_header_length; }
			Dwarf_Half get_version_stamp() const { return version_stamp; }
			Dwarf_Unsigned get_abbrev_offset() const { return abbrev_offset; }
			Dwarf_Half get_address_size() const { return address_size; }
			Dwarf_Half get_offset_size() const { return offset_size; }
			Dwarf_Half get_extension_size() const { return extension_size; }
			Dwarf_Unsigned get_next_cu_header() const { return next_cu_header; }
			
			/* We *could* define ADT getters too. Just remember that
			 * these are different from the CU info.
			 * ADT attrs are more friendly than the basic attribute
			 * because the ADT defines a friendly representation for
			 * each attribute.*/
			
			/* Integrating with ADT: now we have two kinds of iterators.
			 * Core iterators, defined here, are fast, and when dereferenced
			 * yield references to reference-counted instances. 
			 * ADT iterators are slow, and yield shared_ptrs to instances. 
			 * Q. When is it okay to save these pointers? 
			 * A. Only when they are sticky! This is true in both ADT and
			 *    core cases. If we save a ptr/ref to a non-sticky DIE, 
			 *    in the core case it might go dangly, and in the ADT case
			 *    it might become disconnected (i.e. next time we get the 
			 *    same DIE, we will get a different instance, and if we make
			 *    changes, they needn't be reflected).
			 * To integrate these, is it as simple as
			 * - s/shared_ptr/intrusive_ptr/ in ADT;
			 * - include a refcount in every basic_die (basic_die_core?);
			 * - redefine abstract_dieset::iterator to be like core, but
			 *   returning the intrusive_ptr (not raw ref) on dereference? 
			 * Ideally I would separate out the namespaces so that
			 * - lib contains only libdwarf definitions
			 * - spec contains only spec-related things
			 * - "core" to contain the stuff in here
			 *   ... even though it is libdwarf-specific? HMM. 
			 *   ... perhaps reflect commonality by core::root_die, lib::root_die etc.?
			 *   ... here lib::root_die is : public core::root_die, but not polymorphic.
			 * - "adt" to contain spec:: ADT stuff
			 * - encap can stay as it is
			 * - lib ADT stuff should be migrated to core? 
			 * - encap ADT stuff should become an always-sticky variant?
			 * ... This involves unifying dieset/file_toplevel_die with root_die.
			 *  */
			
			/* How can we justify all this libdwarf-specificness?
			 *
			 * Answer: providing iteration etc. is inherently impl-specific.
			 * If we want implementation-agnostic clients, 
			 * they can be templates.
			 * If we want *dynamic* implementation-agnostic clients, 
			 * we can create an indirection layer for a fixed composition of impls.
			 * -- FIXME: clarify this. I think it means template instantiation
			 * Note that we can pull out non-polymorphic base classes
			 * for basic_die, root_die, iterator_base. How useful are these?
			 *
			 * Can we abstract all the libdwarf stuff out just by 
			 * parameterising basic_die, root_die, iterator_base
			 * on a HandleType and a PayloadType?
			 * After all, encap:: is just the case where
			 * every DIE is sticky so 
			 * we always have payloads and never handles.
			 * YES. I think so. Please interleave the core:: with lib:: for now, 
			 * so we can see how it splits.
			 * i.e. merge this file with lib.hpp!
			 *  */
		};
		
		// NOTE: this is 
		
		//template <typename DerefAs /* = basic_die */> // FIXME: reinstate this
		struct iterator_base
		{
			friend class Die;
			friend class Attribute;
			friend class root_die;
			//friend class root_die;
		private:
			/* This stuff is the Rep that can be abstracted out. */
			// union
			// {
				/* These guys are mutable because they need to be modifiable 
				 * even by, e.g., the copy constructor modifying its argument.
				 * The abstract value of the iterator isn't changed in such
				 * operations, but its representation must be. */
				mutable Die cur_handle; // to copy this, have to upgrade it
				mutable root_die::ptr_type cur_payload; // payload = handle + shared count + extra state
			// };
			mutable enum { HANDLE_ONLY, WITH_PAYLOAD } state;
			/* ^-- this is the absolutely key design point that makes this code fast. 
			 * An iterator can either be a libdwarf handle, or a pointer to some
			 * refcounted state (including such a handle, and maybe other cached stuff). */
			
			// normalization function for private use
			Die::raw_handle_type get_raw_handle() const
			{
				if (!is_real_die_position()) return Die::raw_handle_type(nullptr);
				switch (state)
				{
					case HANDLE_ONLY: return cur_handle.handle.get();
					case WITH_PAYLOAD: return cur_payload->d.handle.get();
					default: assert(false);
				}
			}
			// converse is iterator_base(handle) constructor (see below)
			
			/* This is more general-purpose stuff. */
			unsigned short m_depth; // HMM -- good value? does no harm space-wise atm
			root_die *p_root; // HMM -- necessary? alternative is: caller passes root& to some calls
			
		public:
			// we like to be default-constructible, BUT 
			// we are in an unusable state after this constructor
			// -- the same state as end()!
			iterator_base()
			 : cur_handle(Die(nullptr)), state(HANDLE_ONLY), m_depth(0), p_root(nullptr) {}
			
			static const iterator_base END; // sentinel definition
			bool is_root_position() const { return p_root && !cur_handle.handle.get(); }  
			bool is_end_position() const { return !p_root && !cur_handle.handle.get(); }
			bool is_real_die_position() const { return !is_root_position() && !is_end_position(); } 
			
			// this constructor sets us up at begin(), i.e. the root DIE position
			explicit iterator_base(root_die& r)
			 : cur_handle(nullptr), state(HANDLE_ONLY), m_depth(0), p_root(&r) {}
			
			// this constructor sets us up using a handle -- 
			// this does the exploitation of the sticky set
			iterator_base(Die::handle_type& h, unsigned depth, root_die& r)
			 : cur_handle(Die(nullptr)) // will be replaced in function body...
			{
				Dwarf_Off off; 
				dwarf_dieoffset(h.get(), &off, &current_dwarf_error);
				auto found = r.sticky_dies.find(off);
				if (found != r.sticky_dies.end())
				{
					// sticky
					cur_handle = Die(nullptr);
					state = WITH_PAYLOAD;
					cur_payload = found->second;
					m_depth = found->second->get_depth(); assert(depth == m_depth);
					p_root = &found->second->get_root();
				}
				else
				{
					// not sticky
					cur_handle = std::move(h);
					state = HANDLE_ONLY;
					m_depth = depth;
					p_root = &r;
				}
			}

			// this constructor sets us up using a payload ptr
			explicit iterator_base(root_die::ptr_type p)
			 : cur_handle(nullptr), state(WITH_PAYLOAD), 
			   m_depth(p->get_depth()), p_root(&p->get_root()) {}
			
			// iterators must be copyable
			iterator_base(const iterator_base& arg)
			 : cur_handle(nullptr), 
			   cur_payload(arg.is_real_die_position() ? arg.get_root().make_payload(arg) : nullptr),
			   state(WITH_PAYLOAD), 
			   m_depth(arg.depth()), 
			   p_root(arg.is_end_position() ? nullptr : &arg.get_root())
			// FIXME: make sure root_die does the sticky set, then implement this
			// -- we assert(false) just because in test cases, we want to *avoid* copying
			{ if (arg.is_real_die_position()) assert(false); }
			
			// ... but we prefer to move them
			iterator_base(iterator_base&& arg)
			 : cur_handle(std::move(arg.cur_handle)), 
			   cur_payload(arg.cur_payload),
			   state(arg.state), 
			   m_depth(arg.depth()), 
			   p_root(arg.is_end_position() ? nullptr : &arg.get_root())
			{}
			
			// copy assignment			
			iterator_base& operator=(const iterator_base& arg) // does the upgrade...
			{ assert(false); } // ... which we initially want to avoid (zero-copy)
			
			// move assignment
			iterator_base& operator=(iterator_base&& arg) = default; // does the upgrade...
			
			
			/*DerefAs&*/ basic_die& dereference() { assert(false); }
		
			// convenience
			root_die& root() { assert(p_root); return *p_root; }
			root_die& root() const { assert(p_root); return *p_root; }
			root_die& get_root() { assert(p_root); return *p_root; }
			root_die& get_root() const { assert(p_root); return *p_root; }
		
			Dwarf_Off offset_here() const;
			Dwarf_Half tag_here() const;
			
			spec& spec_here() const
			{
				// HACK: avoid creating payload for now
				// NOTE that the code below only creates payload for CU DIEs, so is mostly harmless
				return ::dwarf::spec::dwarf3;
				
				if (tag_here() == DW_TAG_compile_unit)
				{
					if (state == HANDLE_ONLY)
					{
						p_root->make_payload(*this);
					}
					assert(cur_payload && state == WITH_PAYLOAD);
					auto p_cu = dynamic_pointer_cast<compile_unit_die>(cur_payload);
					assert(p_cu);
					switch(p_cu->version_stamp)
					{
						case 2: return ::dwarf::spec::dwarf3;
						default: assert(false);
					}
				}
				else return nearest_enclosing(DW_TAG_compile_unit).spec_here();
			}
			
			// getting _all_ attributes
// 					Dwarf_Unsigned atcnt;
// 					for (int i = 0; i < atcnt; ++i) 
// 					{
// 						dwarf_dealloc(dbg, atlist[i], DW_DLA_ATTR);
// 					}
// 					// NOTE: we use the 0-deleter for the nullptr (error) handle case,
// 					// so we have to be null-safe.
// 					// FIXME: we assume that libdwarf never returns DW_DLV_OK
// 					// with a zero-length attribute list. Is this correct?
// 					if (atcnt > 0) dwarf_dealloc(dbg, atlist, DW_DLA_LIST);
// 				}
// 			};
			
			Attribute::handle_type attribute_here(Dwarf_Half attr)
			{
				return Attribute::try_construct(*this, attr);
			}
			Attribute::handle_type attr_here(Dwarf_Half attr) { return attribute_here(attr); }
			
			bool has_attribute_here(Dwarf_Half attr)
			{
				Dwarf_Bool returned;
				int ret = dwarf_hasattr(get_raw_handle(), attr, &returned, &current_dwarf_error);
				assert(ret == DW_DLV_OK);
				return returned;
			}
			
			// want an iterators-style interface?
			// or an associative-style operator[] interface?
			// or both?
			// make payload include a copy of the attrs state? HMM, yes, this feels correct
			// -- so just put attribute dictionary into basic_die? seems to work
			root_die::ptr_type payload_here();
			
			template<
				Dwarf_Half Tag, 
				dwarf::spec::abstract_def *Spec = dwarf::spec::DEFAULT_DWARF_SPEC
			>
			root_die::ptr_type 
			payload_here_as(); // SPECIALIZE THIS?
			
			typedef Dwarf_Signed difference_type;
			
			iterator_base nearest_enclosing(Dwarf_Half tag) const;
			
			unsigned depth() const { return m_depth; }
			unsigned get_depth() const { return m_depth; }
			
			iterator_base named_child(const string& name) const;
			// + resolve, children<>()?
			
			// we're just the base, so we don't have dereference(), increment(), decrement()
			//virtual void increment() = 0; // needs root reference -- one reason for retaining p_root
			//virtual void decrement() = 0;
			
			bool operator==(const iterator_base& arg) const
			{
				if (!p_root && !arg.p_root) return true; // invalid/end
				if (state == HANDLE_ONLY)
				{
					// if we both point to the root, we're equal
					if (!cur_handle.handle.get() && !arg.cur_handle.handle.get()) return true;
					// otherwise we can't be equal because we should be unique
					else return false;
				}
				else
				{
					return this->p_root == arg.p_root
						&& this->offset_here() == arg.offset_here();
				}
			}
			bool operator!=(const iterator_base& arg) const	
			{ return !(*this == arg); }
			
			basic_die& dereference() const 
			{
				return *get_root().make_payload(*this);
			}
		};
		
		/* Make sure we can construct any iterator from an iterator_base. 
		 * In the case of BFS it may be expensive. */
		template <typename DerefAs /* = basic_die */>
		struct iterator_df : public iterator_base,
		                     public boost::iterator_facade<
		                       iterator_df<DerefAs>
		                     , basic_die
		                     , boost::forward_traversal_tag
		                     >
		{
			typedef iterator_df<DerefAs> self;
			friend class boost::iterator_core_access;
			
			iterator_base& base_reference()
			{ return static_cast<iterator_base&>(*this); }
			const iterator_base& base() const
			{ return static_cast<const iterator_base&>(*this); }
			
			iterator_df() : iterator_base()
			{}

			iterator_df(const iterator_base& arg)
			 : iterator_base(arg)
			{}
			
			iterator_df& operator=(const iterator_base& arg) 
			{ this->base_reference() = arg; }
			
			void increment()
			{
				if (this->base_reference().get_root().move_to_first_child(this->base_reference())) return;
				do
				{
					if (this->base_reference().get_root().move_to_next_sibling(this->base_reference())) return;
				} while (this->base_reference().get_root().move_to_parent(this->base_reference()));

				// if we got here, there is nothing left in the tree...
				// ... so set us to the end sentinel
				this->base_reference() = this->base_reference().get_root().end/*<self>*/();
			}
			
			void decrement()
			{
				assert(false);
			}

			//Value dereference() 
			//{
			//	return base_ref().get_root().make_payload(*this)
			//}
			
			bool equal(const self& arg) const { return this->base() == arg.base(); }
			
			// HACK: forward stuff from iterator_base
			// FIXME: avoid this somehow
// 			Dwarf_Off offset_here() const { return this->base().offset_here(); }
// 			Dwarf_Half tag_here() const { return this->base().tag_here(); }
// 			dwarf::spec::abstract_def& spec_here() const { return this->base().spec_here(); }
// 			// HACK: iterator_base
// 			operator iterator_base() { return this->base_reference(); }
// 			operator iterator_base() const { return this->base(); }

		};
		
		template <typename DerefAs /* = basic_die */>
		struct iterator_bf : public iterator_base
		{
			// extra state needed!
			deque< Dwarf_Off > m_queue;
			
			// + copy constructor
			iterator_bf(const iterator_base& arg) : iterator_base(arg)
			{ /* Starts bfs iteration from the argument position. */ }
			
			iterator_bf(const iterator_bf<DerefAs>& arg)
			 : iterator_base(arg), m_queue(arg.m_queue)
			{ /* Continues bfs iteration using the argument state */ }

			// + assignment operators
			iterator_bf& operator=(const iterator_base& arg)
			{ static_cast<iterator_base&>(*this) = arg; 
			  /* Starts bfs iteration from the argument position. */ 
			  return *this; }
			
			iterator_bf& operator=(const iterator_bf<DerefAs>& arg)
			{ static_cast<iterator_base&>(*this) = arg;
			  this->m_queue = arg.m_queue; 
			  /* Continues bfs iteration using the argument state */ 
			  return *this; }
		};
		
		template <typename DerefAs /* = basic_die*/>
		struct iterator_sibs : public iterator_base
		{
			iterator_sibs(const iterator_base& arg) : iterator_base(arg) {}
			iterator_sibs& operator=(const iterator_base& arg)
			{ static_cast<iterator_base&>(*this) = arg; }
		};
		
		/* Now define the handle quasi-constructors and Die constructors 
		 * that we declared earlier. */
		inline Die::handle_type 
		Die::try_construct(root_die& r, const iterator_base& die) /* siblingof */
		{
			raw_handle_type returned;
			int ret
			 = dwarf_siblingof(r.dbg.handle.get(), die.get_raw_handle(), &returned, &current_dwarf_error);
			if (ret == DW_DLV_OK) return handle_type(returned);
			else return handle_type(nullptr);
		}
		inline Die::handle_type 
		Die::try_construct(root_die& r) /* siblingof in root case */
		{
			raw_handle_type returned;
			int ret
			 = dwarf_siblingof(r.dbg.handle.get(), nullptr, &returned, &current_dwarf_error);
			if (ret == DW_DLV_OK) return handle_type(returned);
			else return handle_type(nullptr);
		}
		inline Die::handle_type 
		Die::try_construct(const iterator_base& die) /* child */
		{
			raw_handle_type returned;
			int ret = dwarf_child(die.get_raw_handle(), &returned, &current_dwarf_error);
			if (ret == DW_DLV_OK) return handle_type(returned);
			else return handle_type(nullptr);

		}
		inline Die::handle_type 
		Die::try_construct(root_die& r, Dwarf_Off off) /* offdie */
		{
			raw_handle_type returned;
			int ret = dwarf_offdie(r.dbg.handle.get(), off, &returned, &current_dwarf_error);
			if (ret == DW_DLV_OK) return handle_type(returned);
			else return handle_type(nullptr);
		}

		inline Die::Die(handle_type h) { this->handle = std::move(h); }
		inline Die::Die(root_die& r, const iterator_base& die) /* siblingof */
		 : handle(try_construct(r, die))
		{ 
			if (!this->handle) throw Error(current_dwarf_error, 0); 
		} 
		inline Die::Die(root_die& r) /* siblingof */
		 : handle(try_construct(r))
		{ 
			if (!this->handle) throw Error(current_dwarf_error, 0); 
		} 
		inline Die::Die(root_die& r, Dwarf_Off off) /* siblingof */
		 : handle(try_construct(r, off))
		{ 
			if (!this->handle) throw Error(current_dwarf_error, 0); 
		} 
		inline Die::Die(const iterator_base& die) /* child */
		 : handle(try_construct(die))
		{
			if (!this->handle) throw Error(current_dwarf_error, 0); 
		}
		
		/* Same for attribute. */
		inline Attribute::handle_type 
		Attribute::try_construct(const iterator_base& it, Dwarf_Half attr)
		{
			raw_handle_type returned;
			int ret = dwarf_attr(it.get_raw_handle(), attr, &returned, &current_dwarf_error);
			if (ret == DW_DLV_OK) return handle_type(returned, deleter(it.root().dbg.handle.get()));
			else return handle_type(nullptr, deleter(nullptr)); // could be ERROR or NO_ENTRY
		}
		inline Attribute::Attribute(const iterator_base& it, Dwarf_Half attr)
		 : handle(try_construct(it, attr))
		{
			if (!this->handle) throw Error(current_dwarf_error, 0);
		}
		
		// FIXME: this is wrong -- 
		// somebody needs to do the heap allocation / factory logic
		// Rather than this constructor trying to copy the handle itself
		// we need the copy to be passed in by iterator_base
		// with the count already set up (using intrusive_ptr utility functions?)
		//template <typename Iter = iterator_df>
		//inline basic_die(root_die& r, const Iter& i);
		// : d(i.copy_handle()), p_root(&r), depth(i.get_depth()) {} 

		/* What about libdwarf API functions? How do we methodify them?
		 * Can they all go on the iterator?
		 * I think so. There are not many of them. 
		 * 
		 * What about expanded-ADT functions? 
		 * Do we still split them into per-DIE navigation-free methods
		 * and navigation-needing functions that should be toplevel? 
		 * Yes.
		 *
		 * What about stackable DIEsets? Do we need dynamic dispatch?
		 * Can we support it?
		 * Certainly we need a generic iterator. This seems inherently
		 * slow. Why? 
		 * How does a filesystem work?
		 * */
		
	}
	
}

#endif
