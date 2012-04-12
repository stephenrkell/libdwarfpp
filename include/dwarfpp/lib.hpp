/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * lib.hpp: basic C++ wrapping of libdwarf C API.
 *
 * Copyright (c) 2008--9, Stephen Kell.
 */

#ifndef __DWARFPP_LIB_HPP
#define __DWARFPP_LIB_HPP

#include <iostream>
#include <utility>
#include <memory>
#include <stack>
#include <vector>
#include <queue>
#include <cassert>
#include <boost/optional.hpp>
#include <boost/smart_ptr/intrusive_ptr.hpp>
#include <boost/iterator/iterator_facade.hpp>
#include <libelf.h>
#include "spec.hpp"

namespace dwarf
{
	using std::string;
	using std::vector;
	using std::stack;
	using std::endl;
	using std::ostream;
	using std::cerr;

	// forward declarations, for "friend" declarations
	namespace encap
	{
		class die;
        class dieset;
        struct loclist;
	}
	namespace lib
	{
		//using namespace ::dwarf::spec;
		extern "C"
		{
			// HACK: libdwarf.h declares struct Elf opaquely, and we don't
			// want it in the dwarf::lib namespace, so preprocess this.
			#define Elf Elf_opaque_in_libdwarf
			#include <libdwarf.h>
			#undef Elf
		}
	}
	
	namespace core
	{
		using std::unique_ptr;
		using std::pair;
		using std::string;		
		using std::map;
		using std::deque;
		using boost::optional;
		using boost::intrusive_ptr;
		using boost::dynamic_pointer_cast;
		
		using namespace dwarf::lib;
		
		
#ifndef NO_TLS
		extern __thread Dwarf_Error current_dwarf_error;
#else
#warning "No TLS, so DWARF error reporting is not thread-safe."
		extern Dwarf_Error current_dwarf_error;
#endif
		
		// FIXME: temporary compatibility hack, please remove
		typedef ::dwarf::spec::abstract_def spec;
		
		// forward declarations
		struct basic_root_die;
		//struct root_die;
		typedef basic_root_die root_die;
		struct basic_die;
		//template <typename DerefAs = basic_die>
		struct iterator_base;
		
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
			
			operator raw_handle_type() { return handle.get(); }
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
			try_construct(basic_root_die& r, const iterator_base& die); /* siblingof */
			static inline handle_type 
			try_construct(basic_root_die& r); /* siblingof with null die */
			static inline handle_type 
			try_construct(const iterator_base& die); /* child */
			static inline handle_type 
			try_construct(basic_root_die& r, Dwarf_Off off); /* offdie */
			
			// ... and an "upgrade" constructor that is guaranteed not to fail
			inline Die(handle_type h);
			
			// ... then the "normal" constructors, that throw exceptions on failure
			inline Die(basic_root_die& r, const iterator_base& die); /* siblingof */
			inline explicit Die(basic_root_die& r); /* siblingof in the root case */
			inline explicit Die(const iterator_base& die); /* child */
			inline explicit Die(basic_root_die& r, Dwarf_Off off); /* offdie */
			
			// move constructor
			inline Die(Die&& d) { handle = std::move(d.handle); }
			// move assignment
			inline Die& operator=(Die&& d) { handle = std::move(d.handle); return *this; }
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
		// Also there are some other kinds of libdwarf resource.
		struct string_deleter
		{
			Debug::raw_handle_type dbg; 
			string_deleter(Debug::raw_handle_type dbg) : dbg(dbg) {}
			// we supply a default constructor, creating a deleter
			// that can only "deallocate" null pointers (noop)
			string_deleter() : dbg(nullptr) {}
			
			void operator()(const char *arg)
			{ if (dbg) dwarf_dealloc(dbg, const_cast<void*>(static_cast<const void *>(arg)), DW_DLA_STRING);
			  else assert(!arg); }
		};
		
		
		// placeholder until we retrofit all this onto adt
		class dummy_adt_basic_die
		{
			virtual const char *get_name() const { return nullptr; }
		};
		
		class basic_die : public virtual dummy_adt_basic_die
		{
			friend struct iterator_base;
			friend class basic_root_die;
		protected:
			// we need to embed a refcount
			unsigned refcount;
			
			// we obviously need this
			Die d;
			// these are necessary to make an iterator out of a basic_die
			unsigned depth;
			basic_root_die *p_root; // FIXME: no! we want to remove this, to allow stacking diesets
			/* Nothing else! This class is here to act as the base for
			 * state-holding subclasses that are optimised for particular
			 * purposes, e.g. fast local/parameter location.  */
			
			// remember! payload = handle + shared count + extra state
			
		public:
			// FIXME: this is wrong
			// somebody needs to do the heap allocation / factory logic
			// Rather than this constructor trying to copy the handle itself
			// we need the copy to be passed in by iterator_base
			// with the count already set up (using intrusive_ptr utility functions?)
			//template <typename Iter = iterator_df>
			//inline basic_die(root_die& r, const Iter& i);
			// : d(i.copy_handle()), p_root(&r), depth(i.get_depth()) {} 
			//template <typename Iter>
			inline basic_die(root_die& r/*, const Iter& i*/);
			
			// begin() / end() / sequence() here?
			// to mean "sequence of DIEs under here"?
			// Or put those methods on iterator?
			
			unsigned get_depth() { return depth; }
			basic_root_die& get_root() { assert(p_root); return *p_root; }
			
			friend std::ostream& operator<<(std::ostream& s, const basic_die& d);
			friend void intrusive_ptr_add_ref(basic_die *p);
			friend void intrusive_ptr_release(basic_die *p);
			
			virtual ~basic_die() {}
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
		struct basic_root_die
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
			basic_root_die(int fd) : dbg(fd), current_cu_offset(0UL) {}
			// we don't provide this constructor because sharing the CU state is a bad idea
			//basic_root_die(lib::file& f) : dbg(f.dbg), current_cu_offset
		
			// HMM: want to avoid virtual dispatch on these
			//bool move_to_parent(iterator_base& it);
			//bool move_to_first_child(iterator_base& it);
			//bool move_to_next_sibling(iterator_base& it);
			
			// HMM -- how to make these policy-agnostic? 
			// Has to be template <typename Iter = iterator_df<> > Iter begin()
			// because we return an instance of the named type. 
			// OR we could go back to the policy-is-separate design.
			// That has the problem that we don't know where to store copied policies.
			// And that inlining is less likely to happen.
			template <typename Iter = iterator_df<> >
			inline Iter begin(); 
			
			template <typename Iter = iterator_df<> >
			inline Iter end();
			
			template <typename Iter = iterator_df<> >
			inline pair<Iter, Iter> sequence();

			template <typename Iter = iterator_df<> >
			Iter begin() const { return const_cast<basic_root_die*>(this)->begin<Iter>(); } 
			template <typename Iter = iterator_df<> >
			Iter end() const { return const_cast<basic_root_die*>(this)->end<Iter>(); } 
			template <typename Iter = iterator_df<> >
			pair<Iter, Iter> sequence() const 
			{ return const_cast<basic_root_die*>(this)->sequence<Iter>(); } 
			
			/* This is the expensive version. */
			template <typename Iter = iterator_df<> >
			Iter find(Dwarf_Off off);
			/* This is the cheap version -- must give a valid offset. */
			template <typename Iter = iterator_df<> >
			Iter pos(Dwarf_Off off, unsigned depth);
			
			::Elf *get_elf(); // hmm: lib-only?
			Debug& get_dbg() { return dbg; }
		// -- begin pasted from root_die
			bool move_to_parent(iterator_base& it);
			bool move_to_first_child(iterator_base& it);
			bool move_to_next_sibling(iterator_base& it);
			
			// libdwarf has this weird stateful CU API
			optional<Dwarf_Off> first_cu_offset;
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
			
		// --- end pasted from root_die
			// print the whole lot
			friend std::ostream& operator<<(std::ostream& s, const basic_root_die& d);
		};	
		std::ostream& operator<<(std::ostream& s, const basic_root_die& d);
		
// 		struct root_die : public basic_root_die
// 		{
// 			ptr_type make_payload(const iterator_base& it); // override;
// 			bool is_sticky(const iterator_base& it); // override;
// 
// 			bool move_to_parent(iterator_base& it);
// 			bool move_to_first_child(iterator_base& it);
// 			bool move_to_next_sibling(iterator_base& it);
// 			
// 			// libdwarf has this weird stateful CU API
// 			optional<Dwarf_Unsigned> last_seen_cu_header_length;
// 			optional<Dwarf_Half> last_seen_version_stamp;
// 			optional<Dwarf_Unsigned> last_seen_abbrev_offset;
// 			optional<Dwarf_Half> last_seen_address_size;
// 			optional<Dwarf_Half> last_seen_offset_size;
// 			optional<Dwarf_Half> last_seen_extension_size;
// 			optional<Dwarf_Unsigned> last_seen_next_cu_header;
// 			bool advance_cu_context();
// 			bool clear_cu_context();
// 		protected:
// 			bool set_subsequent_cu_context(Dwarf_Off off); // helper
// 		public:
// 			bool set_cu_context(Dwarf_Off off);
// 			
// 			friend struct compile_unit_die; // redundant because we're struct, but future hint
// 
// 		};
		typedef basic_root_die root_die;
		
		struct compile_unit_die : public basic_die
		{
			/* Like all payload types, we are constructed from an iterator.
			 * At the point when we are called, 
			 * the Die handle in i is the result of a call to first_die, 
			 * made when the CU state is pointing at the relevant CU.  */
			compile_unit_die(root_die& r) : basic_die(r/*, i*/)
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
		};
		
		//template <typename DerefAs /* = basic_die */>
		struct iterator_base
		{
			friend class Die;
			friend class Attribute;
			friend class basic_root_die;
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
				mutable basic_root_die::ptr_type cur_payload; // payload = handle + shared count + extra state
			// };
			mutable enum { HANDLE_ONLY, WITH_PAYLOAD } state;
			
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
			basic_root_die *p_root; // HMM -- necessary? alternative is: caller passes root& to some calls
			
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
			explicit iterator_base(basic_root_die& r)
			 : cur_handle(nullptr), state(HANDLE_ONLY), m_depth(0), p_root(&r) {}
			
			// this constructor sets us up using a handle -- 
			// this does the exploitation of the sticky set
			iterator_base(Die::handle_type& h, unsigned depth, basic_root_die& r)
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
			explicit iterator_base(basic_root_die::ptr_type p)
			 : cur_handle(nullptr), state(WITH_PAYLOAD), 
			   m_depth(p->get_depth()), p_root(&p->get_root()) {}
			
			// iterators must be copyable
			iterator_base(const iterator_base& arg)
			 : cur_handle(nullptr), 
			   cur_payload(arg.is_real_die_position() ? arg.get_root().make_payload(arg) : nullptr),
			   state(WITH_PAYLOAD), 
			   m_depth(arg.depth()), 
			   p_root(arg.is_end_position() ? nullptr : &arg.get_root())
			{ if (arg.is_real_die_position()) assert(false); } // FIXME: make sure root_die does the sticky set
			
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
			basic_root_die& root() { assert(p_root); return *p_root; }
			basic_root_die& root() const { assert(p_root); return *p_root; }
			basic_root_die& get_root() { assert(p_root); return *p_root; }
			basic_root_die& get_root() const { assert(p_root); return *p_root; }
		
			Dwarf_Off offset_here() const;
			Dwarf_Half tag_here() const;
			
			std::unique_ptr<const char, string_deleter>
			name_here() const;
			
			spec& spec_here() const
			{
				// HACK: avoid creating payload for now
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
			root_die::ptr_type payload_here();
			
			template<
				Dwarf_Half Tag, 
				dwarf::spec::abstract_def *Spec = dwarf::spec::DEFAULT_DWARF_SPEC
			>
			root_die::ptr_type 
			payload_here_as(); // SPECIALIZE THIS?
			
			typedef Dwarf_Signed difference_type;
			
			iterator_base nearest_enclosing(Dwarf_Half tag) const;
			Dwarf_Off enclosing_cu_offset_here() const;
			
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
		
		inline Die::handle_type 
		Die::try_construct(basic_root_die& r, const iterator_base& die) /* siblingof */
		{
			raw_handle_type returned;
			int ret
			 = dwarf_siblingof(r.dbg.handle.get(), die.get_raw_handle(), &returned, &current_dwarf_error);
			if (ret == DW_DLV_OK) return handle_type(returned);
			else return handle_type(nullptr);
		}
		inline Die::handle_type 
		Die::try_construct(basic_root_die& r) /* siblingof in root case */
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
		Die::try_construct(basic_root_die& r, Dwarf_Off off) /* offdie */
		{
			raw_handle_type returned;
			int ret = dwarf_offdie(r.dbg.handle.get(), off, &returned, &current_dwarf_error);
			if (ret == DW_DLV_OK) return handle_type(returned);
			else return handle_type(nullptr);
		}

		inline Die::Die(handle_type h) { this->handle = std::move(h); }
		inline Die::Die(basic_root_die& r, const iterator_base& die) /* siblingof */
		 : handle(try_construct(r, die))
		{ 
			if (!this->handle) throw Error(current_dwarf_error, 0); 
		} 
		inline Die::Die(basic_root_die& r) /* siblingof */
		 : handle(try_construct(r))
		{ 
			if (!this->handle) throw Error(current_dwarf_error, 0); 
		} 
		inline Die::Die(basic_root_die& r, Dwarf_Off off) /* siblingof */
		 : handle(try_construct(r, off))
		{ 
			if (!this->handle) throw Error(current_dwarf_error, 0); 
		} 
		inline Die::Die(const iterator_base& die) /* child */
		 : handle(try_construct(die))
		{
			if (!this->handle) throw Error(current_dwarf_error, 0); 
		}
		
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
		
		// FIXME: this is wrong
		// somebody needs to do the heap allocation / factory logic
		// Rather than this constructor trying to copy the handle itself
		// we need the copy to be passed in by iterator_base
		// with the count already set up (using intrusive_ptr utility functions?)
		//template <typename Iter>
		inline basic_die::basic_die(root_die& r/*, const Iter& i*/)
		: d(Die::handle_type(nullptr)), p_root(&r) {}

		template <typename Iter /* = iterator_df<> */ >
		inline Iter basic_root_die::begin()
		{
			/* The first DIE is always the root.
			 * We denote an iterator pointing at the root by
			 * a Debug but no Die. */
			return Iter(iterator_base(*this));
		}

		template <typename Iter /* = iterator_df<> */ >
		inline Iter basic_root_die::end()
		{
			return Iter(iterator_base::END);
		}

		template <typename Iter /* = iterator_df<> */ >
		inline pair<Iter, Iter> basic_root_die::sequence()
		{
			return std::make_pair(begin(), end());
		}


	}
	
	namespace lib
	{
		class file;
		class die;
		class attribute;
		class attribute_array;
		class global_array;
		class global;
		class block;
		class loclist;
        class aranges;
        class ranges;
		class evaluator;
		class srclines;
		
		class basic_die;
		class file_toplevel_die;
		
		struct No_entry {
			No_entry() {}
		};	
		
		struct Error {
			Dwarf_Error e;
			Dwarf_Ptr arg;
			Error(Dwarf_Error e, Dwarf_Ptr arg) : e(e), arg(arg) {}
			virtual ~Error() 
			{ /*dwarf_dealloc((Dwarf_Debug) arg, e, DW_DLA_ERROR); */ /* TODO: Fix segfault here */	}
		};

		void default_error_handler(Dwarf_Error error, Dwarf_Ptr errarg); 
		class file {
			friend class die;
			friend class global_array;
			friend class attribute_array;
			friend class aranges;
			friend class ranges;
			friend class srclines;
			friend class srcfiles;

			friend class file_toplevel_die;
			friend class compile_unit_die;

			Dwarf_Debug dbg; // our peer structure
			Dwarf_Error last_error; // pointer to Dwarf_Error_s detailing our last error
			//dieset file_ds; // the structure to hold encapsulated DIEs, if we use it

			/*dwarf_elf_handle*/ Elf* elf;
			bool free_elf; // whether to do elf_end in destructor

			aranges *p_aranges;

			// public interfaces to these are to use die constructors
			int siblingof(die& d, die *return_sib, Dwarf_Error *error = 0);
			int first_die(die *return_die, Dwarf_Error *error = 0); // special case of siblingof

		protected:
			bool have_cu_context; 

			/* We have a default constructor so that 
			 * - encap::file can inherit from us
			 * - we can wrap the libdwarf producer interface too.
			 * Note that dummy_file has gone away! */
			// protected constructor
			file() : dbg(0), last_error(0), 
			  elf(0), p_aranges(0), have_cu_context(false)
			{} 

			// we call out to a function like this when we hit a CU in clear_cu_context
			typedef void (*cu_callback_t)(void *arg, 
				Dwarf_Off cu_offset,
				Dwarf_Unsigned cu_header_length,
				Dwarf_Half version_stamp,
				Dwarf_Unsigned abbrev_offset,
				Dwarf_Half address_size,
				Dwarf_Unsigned next_cu_header);
				
            // libdwarf has a weird stateful API for getting compile unit DIEs.
            int clear_cu_context(cu_callback_t cb = 0, void *arg = 0);

			// TODO: forbid copying or assignment by adding private definitions 
		public:
			Dwarf_Debug get_dbg() { return dbg; }
			//dieset& get_ds() { return file_ds; }
			file(int fd, Dwarf_Unsigned access = DW_DLC_READ,
				Dwarf_Ptr errarg = 0,
				Dwarf_Handler errhand = default_error_handler,
				Dwarf_Error *error = 0);

			virtual ~file();

			int advance_cu_context(Dwarf_Unsigned *cu_header_length = 0,
				Dwarf_Half *version_stamp = 0,
				Dwarf_Unsigned *abbrev_offset = 0,
				Dwarf_Half *address_size = 0, 
				Dwarf_Unsigned *next_cu_header = 0,
				cu_callback_t cb = 0, void *arg = 0);
			
			/* map a function over all CUs */
			void iterate_cu(cu_callback_t cb, void *arg = 0)
			{
				clear_cu_context(); 
				while (advance_cu_context(0, 0, 0, 0, 0, cb, arg) == DW_DLV_OK);
			}
			
			int ensure_cu_context()
			{
				if (have_cu_context) return DW_DLV_OK;
				else 
				{
					int retval = advance_cu_context();
					assert(retval != DW_DLV_OK || have_cu_context);
					return retval; 
				}
			}
			
		protected:
			// public interface to this: use advance_cu_context
			int next_cu_header(
				Dwarf_Unsigned *cu_header_length,
				Dwarf_Half *version_stamp,
				Dwarf_Unsigned *abbrev_offset,
				Dwarf_Half *address_size,
				Dwarf_Unsigned *next_cu_header,
				Dwarf_Error *error = 0);
		private:
			int offdie(Dwarf_Off offset, die *return_die, Dwarf_Error *error = 0);
		public:

	//		int get_globals(
	//			global_array *& globarr, // on success, globarr points to a global_array
	//			Dwarf_Error *error = 0);

			int get_cu_die_offset_given_cu_header_offset(
				Dwarf_Off in_cu_header_offset,
				Dwarf_Off * out_cu_die_offset,
				Dwarf_Error *error = 0);
                
            int get_elf(Elf **out_elf, Dwarf_Error *error = 0);
            
            aranges& get_aranges() { if (p_aranges) return *p_aranges; else throw No_entry(); }
		};

		class die {
			friend class file;
			friend class attribute_array;
			friend class dwarf::encap::die;
			friend class block;
			friend class loclist;
			friend class ranges;
			friend class srclines;
			friend class srcfiles;
			
			friend class basic_die;
			friend class file_toplevel_die;
		public: // HACK while I debug the null-f bug
			file& f;
		private:
			Dwarf_Error *const p_last_error;
			Dwarf_Die my_die;
			die(file& f, Dwarf_Die d, Dwarf_Error *perror);
			Dwarf_Die get_die() { return my_die; }

			// public interface is to use constructor
			int first_child(die *return_kid, Dwarf_Error *error = 0);
			
			// "uninitialized" DIE is used to back file_toplevel_die
			//static file dummy_file;
			// get rid of this ^^! basic_die now no longer needs it
			
			bool have_cu_context; 

		public:
			virtual ~die();
			die(file& f, int dummy) : f(f), p_last_error(&f.last_error)
			{
				/* This constructor is used for the dummy DIE representing the
				 * top of the tree. We set my_die to 0 (it's a pointer). */
				my_die = 0;
			}
			die(file& f) : f(f), p_last_error(&f.last_error)
				{	/* ask the file to initialize us with the first DIE of the current CU */
					int cu_retval = f.ensure_cu_context();
					if (cu_retval != DW_DLV_OK) throw Error(*p_last_error, f.get_dbg());
					int retval = f.first_die(this, p_last_error);
					if (retval == DW_DLV_NO_ENTRY) throw No_entry();
					else if (retval == DW_DLV_ERROR) throw Error(*p_last_error, f.get_dbg()); }
			die(file& f, const die& d) : f(f), p_last_error(&f.last_error)
				{	/* ask the file to initialize us with the next sibling of d */
					int retval = f.siblingof(const_cast<die&>(d), this, p_last_error);
					if (retval == DW_DLV_NO_ENTRY) throw No_entry(); 
					else if (retval == DW_DLV_ERROR) throw Error(*p_last_error, f.get_dbg()); }
			// this is *not* a copy constructor! it constructs the child
			explicit die(const die& d) : f(const_cast<die&>(d).f), p_last_error(&f.last_error)
				{	/* ask the file to initialize us with the first child of d */
					int retval = const_cast<die&>(d).first_child(this, p_last_error); 
					if (retval == DW_DLV_NO_ENTRY) throw No_entry(); 
					else if (retval == DW_DLV_ERROR) throw Error(*p_last_error, f.get_dbg()); }
			die(file& f, Dwarf_Off off) : f(f), p_last_error(&f.last_error)
				{	/* ask the file to initialize us with a DIE from a known offset */
					assert (off != 0UL); // file_toplevel_die should use protected constructor
					int retval = f.offdie(off, this, p_last_error);
					if (retval == DW_DLV_ERROR) throw Error(*p_last_error, f.get_dbg()); }			

			int tag(Dwarf_Half *tagval,	Dwarf_Error *error = 0) const;
			int offset(Dwarf_Off * return_offset, Dwarf_Error *error = 0) const;
			int CU_offset(Dwarf_Off *return_offset, Dwarf_Error *error = 0);
			int name(string *return_name, Dwarf_Error *error = 0) const;
			//int attrlist(attribute_array *& attrbuf, Dwarf_Error *error = 0);
			int hasattr(Dwarf_Half attr, Dwarf_Bool *return_bool, Dwarf_Error *error = 0);
			int hasattr(Dwarf_Half attr, Dwarf_Bool *return_bool, Dwarf_Error *error = 0) const
			{ return const_cast<die *>(this)->hasattr(attr, return_bool, error); }
			//int attr(Dwarf_Half attr, attribute *return_attr, Dwarf_Error *error = 0);
			int lowpc(Dwarf_Addr * return_lowpc, Dwarf_Error * error = 0);
			int highpc(Dwarf_Addr * return_highpc, Dwarf_Error *error = 0);
			int bytesize(Dwarf_Unsigned *return_size, Dwarf_Error *error = 0);
			int bitsize(Dwarf_Unsigned *return_size, Dwarf_Error *error = 0);
			int bitoffset(Dwarf_Unsigned *return_size, Dwarf_Error *error = 0);
			int srclang(Dwarf_Unsigned *return_lang, Dwarf_Error *error = 0);
			int arrayorder(Dwarf_Unsigned *return_order, Dwarf_Error *error = 0);
		};

		/* Attributes may be freely passed by value, because there is 
         * no libdwarf resource allocation done when getting attributes
         * (it's all done when getting the attribute array). 
		 * In other words, an attribute is just a <base, offset> pair
		 * denoting a position in an attribute_array, which is assumed
		 * to outlive the attribute itself. */
		class attribute {
			friend class die;
			friend class attribute_array;
			friend class block;
			friend class loclist;
            friend class ranges;
			core::Attribute::raw_handle_type p_raw_attr;
			attribute_array *p_a;
			int i;
			core::Debug::raw_handle_type p_raw_dbg;
		public:
			inline attribute(attribute_array& a, int i); // defined in a moment
			attribute(Dwarf_Half attr, attribute_array& a, Dwarf_Error *error = 0);
			attribute(const core::Attribute& a, core::Debug::raw_handle_type dbg)
			 : p_raw_attr(a.handle.get()), p_a(0), i(0), p_raw_dbg(dbg) {}
			virtual ~attribute() {}	

			public:
			int hasform(Dwarf_Half form, Dwarf_Bool *return_hasform, Dwarf_Error *error = 0) const;
			int whatform(Dwarf_Half *return_form, Dwarf_Error *error = 0) const;
			int whatform_direct(Dwarf_Half *return_form, Dwarf_Error *error = 0) const;
			int whatattr(Dwarf_Half *return_attr, Dwarf_Error *error = 0) const;
			int formref(Dwarf_Off *return_offset, Dwarf_Error *error = 0) const;
			int formref_global(Dwarf_Off *return_offset, Dwarf_Error *error = 0) const;
			int formaddr(Dwarf_Addr * return_addr, Dwarf_Error *error = 0) const;
			int formflag(Dwarf_Bool * return_bool, Dwarf_Error *error = 0) const;
			int formudata(Dwarf_Unsigned * return_uvalue, Dwarf_Error * error = 0) const;
			int formsdata(Dwarf_Signed * return_svalue, Dwarf_Error *error = 0) const;
			int formblock(Dwarf_Block ** return_block, Dwarf_Error * error = 0) const;
			int formstring(char ** return_string, Dwarf_Error *error = 0) const;
			int loclist_n(Dwarf_Locdesc ***llbuf, Dwarf_Signed *listlen, Dwarf_Error *error = 0) const;
			int loclist(Dwarf_Locdesc **llbuf, Dwarf_Signed *listlen, Dwarf_Error *error = 0) const;

			const attribute_array& get_containing_array() const { return *p_a; }
		};

		class attribute_array {
			friend class die;
			friend class attribute;
			friend class block;
			friend class loclist;
            friend class ranges; 
			Dwarf_Attribute *p_attrs;
			die& d;
			Dwarf_Error *const p_last_error;
			Dwarf_Signed cnt;
			// TODO: forbid copying or assignment by adding private definitions 
		public:
			//attribute_array(Dwarf_Debug dbg, Dwarf_Attribute *attrs, Dwarf_Signed cnt)
			//	: dbg(dbg), attrs(attrs), cnt(cnt) {}
			attribute_array(die &d, Dwarf_Error *error = 0) : 
				d(d), p_last_error(error ? error : &d.f.last_error)
			{
				if (error == 0) error = p_last_error;
				int retval = dwarf_attrlist(d.get_die(), &p_attrs, &cnt, error);
				if (retval == DW_DLV_NO_ENTRY)
				{
					// this means two things -- cnt is zero, and nothing is allocated
					p_attrs = 0;
					cnt = 0;
				}
				else if (retval != DW_DLV_OK) 
				{
					throw Error(*error, d.f.get_dbg());
				}
			}
			Dwarf_Signed count() { return cnt; }
			attribute get(int i) { return attribute(*this, i); }
            attribute operator[](Dwarf_Half attr) { return attribute(attr, *this); }

			virtual ~attribute_array()	{
				for (int i = 0; i < cnt; i++)
				{
					dwarf_dealloc(d.f.dbg, p_attrs[i], DW_DLA_ATTR);
				}
				if (p_attrs != 0) dwarf_dealloc(d.f.dbg, p_attrs, DW_DLA_LIST);
			}
			const die& get_containing_die() const { return d; }
		};
		
		inline attribute::attribute(attribute_array& a, int i) : p_a(&a), i(i)
		{ p_raw_attr = p_a->p_attrs[i]; }
		
		class block {
			attribute attr; // it's okay to pass attrs by value
			Dwarf_Block *b;
		public:
			block(attribute a, Dwarf_Error *error = 0) : attr(a) // it's okay to pass attrs by value
			{
				if (error == 0) error = attr.p_a->p_last_error;
				int retval = a.formblock(&b);
				if (retval != DW_DLV_OK) throw Error(*error, attr.p_a->d.f.get_dbg());
			}

			Dwarf_Unsigned len() { return b->bl_len; }
			Dwarf_Ptr data() { return b->bl_data; }

			virtual ~block() { dwarf_dealloc(attr.p_a->d.f.get_dbg(), b, DW_DLA_BLOCK); }
		};
		
		ostream& operator<<(ostream& s, const Dwarf_Locdesc& ld);	
		ostream& operator<<(ostream& s, const Dwarf_Loc& l);
		ostream& operator<<(ostream& s, const loclist& ll);
		
		class global {
			friend class global_array;
			global_array& a;
			int i;

 			global(global_array& a, int i) : a(a), i(i) {}

			public:
			int get_name(char **return_name, Dwarf_Error *error = 0);
			int get_die_offset(Dwarf_Off *return_offset, Dwarf_Error *error = 0);
			int get_cu_offset(Dwarf_Off *return_offset, Dwarf_Error *error = 0);
			int cu_die_offset_given_cu_header_offset(Dwarf_Off in_cu_header_offset,
				Dwarf_Off *out_cu_die_offset, Dwarf_Error *error = 0);
			int name_offsets(char **return_name, Dwarf_Off *die_offset, 
				Dwarf_Off *cu_offset, Dwarf_Error *error = 0);			
		};

		class global_array {
		friend class file;
		friend class global;
			file& f;
			Dwarf_Error *const p_last_error;
			Dwarf_Global *p_globals;
			Dwarf_Debug dbg;
			Dwarf_Signed cnt;
			// TODO: forbid copying or assignment by adding private definitions 
		public:
			//global_array(Dwarf_Debug dbg, Dwarf_Global *globals, Dwarf_Signed cnt);
			global_array(file& f, Dwarf_Error *error = 0) : f(f), p_last_error(error ? error : &f.last_error)
			{
				if (error == 0) error = p_last_error;
				int retval = dwarf_get_globals(f.get_dbg(), &p_globals, &cnt, error);
				if (retval != DW_DLV_OK) throw Error(*error, f.get_dbg());
			}
			Dwarf_Signed count() { return cnt; }		
			global get(int i) { return global(*this, i); }

			virtual ~global_array() { dwarf_globals_dealloc(f.get_dbg(), p_globals, cnt); }
		};
        
        class aranges
        {
        	file &f;
		public: // temporary HACK
            Dwarf_Error *const p_last_error;
		//private:
            Dwarf_Arange *p_aranges;
            Dwarf_Signed cnt;
            // TODO: forbid copying or assignment
        public:
        	aranges(file& f, Dwarf_Error *error = 0) : f(f), p_last_error(error ? error : &f.last_error)
            {
            	if (error == 0) error = p_last_error;
                int retval = dwarf_get_aranges(f.get_dbg(), &p_aranges, &cnt, error);
                if (retval == DW_DLV_NO_ENTRY) { cnt = 0; p_aranges = 0; }
                else if (retval != DW_DLV_OK) throw Error(*error, f.get_dbg());
				cerr << "Constructed an aranges from block at " << p_aranges 
					<< ", count " << cnt << endl;
            }
			Dwarf_Signed count() { return cnt; }		
			int get_info(int i, Dwarf_Addr *start, Dwarf_Unsigned *length, Dwarf_Off *cu_die_offset,
				Dwarf_Error *error = 0);
			int get_info_for_addr(Dwarf_Addr a, Dwarf_Addr *start, Dwarf_Unsigned *length, Dwarf_Off *cu_die_offset,
				Dwarf_Error *error = 0);
			void *arange_block_base() const { return p_aranges; }
            virtual ~aranges()
            {
            	// FIXME: uncomment this after dwarf_dealloc segfault bug fixed
                
            	//for (int i = 0; i < cnt; ++i) dwarf_dealloc(f.dbg, p_aranges[i], DW_DLA_ARANGE);
				//dwarf_dealloc(f.dbg, p_aranges, DW_DLA_LIST);
            }
        };

        class ranges
        {
            const die& d;
            Dwarf_Error *const p_last_error;
            Dwarf_Ranges *p_ranges;
            Dwarf_Signed cnt;
            // TODO: forbid copying or assignment
        public:
        	ranges(const attribute& a, Dwarf_Off range_off, Dwarf_Error *error = 0) 
            : d(a.p_a->d), p_last_error(error ? error : &d.f.last_error)
            {
            	if (error == 0) error = p_last_error;
                Dwarf_Unsigned bytes;
                int res;
                res = dwarf_get_ranges_a(d.f.dbg, range_off, 
                	d.my_die, &p_ranges, &cnt, &bytes, error);
                if (res == DW_DLV_NO_ENTRY) throw No_entry();
                assert(res == DW_DLV_OK);
            }
            
            Dwarf_Ranges operator[](Dwarf_Signed i)
            { assert(i < cnt); return p_ranges[i]; }
            Dwarf_Ranges *begin() { return p_ranges; }
            Dwarf_Ranges *end() { return p_ranges + cnt; }
            
			Dwarf_Signed count() { return cnt; }		
            virtual ~ranges()
            {
            	dwarf_ranges_dealloc(d.f.dbg, p_ranges, cnt);
            }
        };
		
		class srclines
		{
			const file& f;
			Dwarf_Line *linebuf;
			Dwarf_Signed linecount;
			Dwarf_Error *const p_last_error;
		
		public:
			srclines(die& d, Dwarf_Error *error = 0) : f(d.f), 
				p_last_error(error ? error : &d.f.last_error)
			{
				linecount = -1;
				if (error == 0) error = p_last_error;
				int ret = dwarf_srclines(d.my_die, &linebuf, &linecount, error);
				if (ret == DW_DLV_OK) linecount = -1; // don't deallocate anything
				assert(ret == DW_DLV_OK);
			}
			
			virtual ~srclines()
			{
				if (linecount != -1 /*&& &d.f != &die::dummy_file*/)
				{
					dwarf_srclines_dealloc(f.dbg, linebuf, linecount);
				}
			}
		};

		class srcfiles
		{
			const file& f;
			char **filesbuf;
			Dwarf_Signed filescount;
			Dwarf_Error *const p_last_error;
		
		public:
			srcfiles(die& d, Dwarf_Error *error = 0) : f(d.f),
				p_last_error(error ? error : &d.f.last_error)
			{
				filescount = -1;
				if (error == 0) error = p_last_error;
				int ret = dwarf_srcfiles(d.my_die, &filesbuf, &filescount, error);
				if (ret == DW_DLV_NO_ENTRY) filescount = -1; // don't deallocate anything
				assert(ret == DW_DLV_OK);
			}
			
			virtual ~srcfiles()
			{
				if (filescount != -1 /*&& &d.f != &die::dummy_file*/) 
				{
					for (int i = 0; i < filescount; ++i)
					{
						dwarf_dealloc(f.dbg, filesbuf[i], DW_DLA_STRING);
					}
					dwarf_dealloc(f.dbg, filesbuf, DW_DLA_LIST);
				}
			}
			
			std::string get(unsigned o)
			{
				/* Source file numbers in DWARF are indexed starting from 1. 
				 * Source file zero means "no source file".
				 * However, our array filesbuf is indexed beginning zero! */
				if (o >= 1 && o <= filescount) return filesbuf[o-1];
				else throw No_entry();
			}
			
			unsigned count() { return filescount; }
		};		
		class Not_supported
        {
        	const string& m_msg;
        public:
        	Not_supported(const string& msg) : m_msg(msg) {}
        };
        
		class regs
        {
        public:
        	virtual Dwarf_Signed get(int regnum) = 0;
            virtual void set(int regnum, Dwarf_Signed val) 
            { throw Not_supported("writing registers"); }
		};

		class evaluator {
			std::stack<Dwarf_Unsigned> m_stack;
			vector<Dwarf_Loc> expr;
            const ::dwarf::spec::abstract_def& spec;
            regs *p_regs; // optional set of register values, for DW_OP_breg*
            boost::optional<Dwarf_Signed> frame_base;
			vector<Dwarf_Loc>::iterator i;
			void eval();
		public:
			evaluator(const vector<unsigned char> expr, 
            	const ::dwarf::spec::abstract_def& spec) : spec(spec), p_regs(0)
			{
            	//i = expr.begin();
				assert(false);
			}
			evaluator(const encap::loclist& loclist,
            	Dwarf_Addr vaddr,
	            const ::dwarf::spec::abstract_def& spec = spec::DEFAULT_DWARF_SPEC,
                regs *p_regs = 0,
                boost::optional<Dwarf_Signed> frame_base = boost::optional<Dwarf_Signed>(),
                const stack<Dwarf_Unsigned>& initial_stack = stack<Dwarf_Unsigned>()); 
			
            evaluator(const vector<Dwarf_Loc>& loc_desc,
	            const ::dwarf::spec::abstract_def& spec,
                const stack<Dwarf_Unsigned>& initial_stack = stack<Dwarf_Unsigned>()) 
                : m_stack(initial_stack), spec(spec), p_regs(0)
			{
				expr = loc_desc;
                i = expr.begin();
				eval();
			} 
			evaluator(const vector<Dwarf_Loc>& loc_desc,
	            const ::dwarf::spec::abstract_def& spec,
                regs& regs,
                Dwarf_Signed frame_base,
                const stack<Dwarf_Unsigned>& initial_stack = stack<Dwarf_Unsigned>()) 
                : m_stack(initial_stack), spec(spec), p_regs(&regs)
			{
				expr = loc_desc;
                i = expr.begin();
                this->frame_base = frame_base;
				eval();
			} 

			evaluator(const vector<Dwarf_Loc>& loc_desc,
	            const ::dwarf::spec::abstract_def& spec,
                Dwarf_Signed frame_base,
                const stack<Dwarf_Unsigned>& initial_stack = stack<Dwarf_Unsigned>()) 
                : m_stack(initial_stack), spec(spec), p_regs(0)
			{
				//if (av.get_form() != dwarf::encap::attribute_value::LOCLIST) throw "not a DWARF expression";
				//if (av.get_loclist().size() != 1) throw "only support singleton loclists for now";			
				//expr = *(av.get_loclist().begin());
				expr = loc_desc;
                i = expr.begin();
                this->frame_base = frame_base;
				eval();
			} 
            
            Dwarf_Unsigned tos() const { return m_stack.top(); }
            bool finished() const { return i == expr.end(); }
            Dwarf_Loc current() const { return *i; }
		};
        Dwarf_Unsigned eval(const encap::loclist& loclist,
        	Dwarf_Addr vaddr,
            Dwarf_Signed frame_base,
            boost::optional<regs&> rs,
	        const ::dwarf::spec::abstract_def& spec,
            const stack<Dwarf_Unsigned>& initial_stack);
        
        class loclist {
		    attribute attr;
		    Dwarf_Locdesc **locs;
		    Dwarf_Signed locs_len;
		    friend ostream& operator<<(ostream&, const loclist&);
		    friend struct ::dwarf::encap::loclist;
	    public:
		    loclist(attribute a, Dwarf_Error *error = 0) : attr(a)
		    {
			    if (error == 0) error = attr.p_a->p_last_error;
			    int retval = a.loclist_n(&locs, &locs_len);
			    if (retval == DW_DLV_NO_ENTRY)
			    {
				    // this means two things -- locs_len is zero, and nothing is allocated
				    locs = 0;
				    locs_len = 0;
			    }
			    if (retval == DW_DLV_ERROR) throw Error(*error, attr.p_a->d.f.get_dbg());
		    }
		    loclist(const core::Attribute& a, core::Debug::raw_handle_type dbg) : attr(a, dbg)
		    {
			    //if (error == 0) error = attr.a.p_last_error;
			    int retval = dwarf_loclist_n(a.handle.get(), &locs, &locs_len, &core::current_dwarf_error);
			    if (retval == DW_DLV_NO_ENTRY)
			    {
				    // this means two things -- locs_len is zero, and nothing is allocated
				    locs = 0;
				    locs_len = 0;
			    }
			    if (retval == DW_DLV_ERROR) throw Error(core::current_dwarf_error, 0);
		    }

		    Dwarf_Signed len() const { return locs_len; }
		    const Dwarf_Locdesc& operator[] (size_t off) const { return *locs[off]; }

		    virtual ~loclist()
		    { 
			    for (int i = 0; i < locs_len; ++i) 
			    {
				    dwarf_dealloc(attr.p_raw_dbg, locs[i]->ld_s, DW_DLA_LOC_BLOCK);
				    dwarf_dealloc(attr.p_raw_dbg, locs[i], DW_DLA_LOCDESC);
			    }
			    if (locs != 0) dwarf_dealloc(attr.p_raw_dbg, locs, DW_DLA_LIST);
		    }
	    };
	    ostream& operator<<(std::ostream& s, const loclist& ll);	

		bool operator==(const Dwarf_Loc& e1, const Dwarf_Loc& e2);
		bool operator!=(const Dwarf_Loc& e1, const Dwarf_Loc& e2);
	} // end namespace lib
} // end namespace dwarf

#endif
