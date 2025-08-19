/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * dwarflib-handles.hpp: basic C++ wrapping of libdwarf/libdw C API (info section).
 *
 * Copyright (c) 2008--20, Stephen Kell.
 */

#ifndef DWARFPP_PRIVATE_LIBDWARF_HANDLES_HPP_
#define DWARFPP_PRIVATE_LIBDWARF_HANDLES_HPP_

#include "dwarfpp/dwarflib.hpp"
#include "dwarfpp/abstract.hpp"

#include <iostream>
#include <utility>
#include <functional>
#include <vector>

namespace dwarf
{
	namespace core
	{
		using std::unique_ptr;
		using std::vector;
		using std::pair;
		using namespace dwarf::lib;
		/* Forward-declare what we assume from libdwarfpp. */
		struct root_die;
		struct iterator_base;
		struct abstract_die;
		
#ifdef USING_LIBDWARF
		/* FIXME: clean up Errors properly. It's complicated. A Dwarf_Error is a handle
		 * that needs to be dwarf_dealloc'd, but there are two exceptions:
		 * errors returned by dwarf_init() and dwarf_elf_init() need to be free()d. 
		 * In other words, these errors need different deleters. 
		 * We should unique_ptr'ify each Dwarf_Error at the point where it arises,
		 * so that we can specify this alternate handling. */
		// typedef struct Dwarf_Error_s*      Dwarf_Error;
		void exception_error_handler(Dwarf_Error error, Dwarf_Ptr errarg);
#endif

		/* What follows is^H^H^H *was* a fairly mechanical translation of libdwarf,
		 * plus destruction logic from the docs. It is now complicated by also
		 * supporting libdw.
		 *
		 * With libdwarf, the library almost always returns an opaque handle which
		 * points to osme data structure allocated privately by libdwarf. The handle
		 * must be freed by some special method, usually involving dwarf_dealloc().
		 *
		 * With libdw, the library prefers not to allocate stuff. This is good because
		 * it avoids the multiple indirection and frequent heap allocation that are
		 * going on in libdwarf. We undo some of that good by reintroducing the
		 * indirection, for uniformity with libdwarf. */
#ifdef USING_LIBDWARF
		typedef struct Dwarf_Debug_s*      Dwarf_Debug; // pasted from libdwarf.h
#else /* USING_LIBDW */
		typedef struct Dwarf Dwarf; // from libdw.h
#endif
/* in libdwarf:

    Dwarf_Debug_s* a.k.a.  
    Dwarf_Debug a.k.a.
    core::Debug
      ::raw_handle_type     opaque structure
+--------------+          +-------------- -
|      --------+--------->|
+--------------+          |
                          | 
                          |
                          |
                          :

            allocated by?       libdwarf
            freed how?          dwarf_dealloc(DW_DLA_DEBUG)

which we wrap as
   unique_ptr                opaque structure
  +==============+        +-------------- -
  ||     -------++------->|
  +==============+        |
                          |
                          |
                          |
                          :
Now in libdw:

    struct Dwarf * a.k.a.
    Dwarf* a.k.a.
    core::Debug
      ::raw_handle_Type      opaque structure
+==============+          +-------------- -
||     -------++--------->|
+==============+          |
                          |
                          |
                          |
                          :

           allocated by?         libdw
           freed how?            dwarf_end
*/
		struct Debug
		{
#ifdef USING_LIBDWARF
			typedef Dwarf_Debug raw_handle_type;
			typedef Dwarf_Debug_s opaque_type;
#else /* USING_LIBDW */
			typedef Dwarf *raw_handle_type;
			typedef Dwarf opaque_type;
#endif
			struct deleter
			{
				void operator ()(raw_handle_type arg) const;
			};
			typedef unique_ptr<opaque_type, deleter> handle_type;
			handle_type handle;
			
			// define constructors analogous to the libdwarf resource-acquisition functions
			Debug(int fd); /* FIXME: release Elf handle implicitly left open after dwarf_finish(). */
			Debug(Elf *elf);
			Debug() : handle(nullptr) {}
			
			raw_handle_type raw_handle()       { return handle.get(); }
			raw_handle_type raw_handle() const { return handle.get(); }
		};

#ifdef USING_LIBDWARF
		// Also there are some other kinds of libdwarf resource.
		struct string_deleter
		{
			Debug::raw_handle_type dbg; 
			string_deleter(Debug::raw_handle_type dbg) : dbg(dbg) {}

			// we supply a default constructor, creating a deleter
			// that can only "deallocate" null pointers (noop)
			string_deleter() : dbg(nullptr) {}
			
			void operator()(const char *arg)
			{ 
				if (dbg)
				{
					dwarf_dealloc(dbg, 
						const_cast<void*>(static_cast<const void *>(arg)), 
						DW_DLA_STRING);
				} else assert(!arg); 
			}
		};
		typedef std::unique_ptr<const char, string_deleter> raw_name_t;
#else
		typedef const char *raw_name_t;
#endif
#ifdef USING_LIBDWARF
		typedef struct Dwarf_Die_s*        Dwarf_Die;
#endif

/* in libdwarf:

    Dwarf_Die_s* a.k.a.  
    Dwarf_Die a.k.a.
    core::Die
      ::raw_handle_type     opaque structure
+--------------+           +-------------- -
|       --------+--------->|
+--------------+           |
                           |
                           |
                           |
                           :

allocated by?       libdwarf
freed how?          dwarf_dealloc(DW_DLA_DIE)

which we wrap as

 struct Die, wrapping a
 unique_ptr to Dwarf_Die_s (with deleter)
  + adding abstract_die implementation
+==============+
||     -------++------->the opaque structure
+==============+

... and then becomes with iterator_base (important for a complete picture...)
                  .--- only used in the "cur_handle" case, maintained by linear treatment of the iterator
+==============+  v                                 
|||    ------+++------->the opaque structure        basic_die or a subclass thereof
|''==========''|  OR                               +==============+
|      --------+---------------------------------->|||    ------+++------->the opaque structure
+==============+  ^                                |''==========''|
                  '--- only used in the            |   refcount   |
                      "cur_payload" case           | per-TAG data |
                      when the iterator is copied  |     ...      |
                                                   +==============+
                                                  


Now in libdw:
(if we want this pointer,
we can make it, but it's
not a handle in the API
because struct is non-opaque)
                       ]    non-opaque structure
    Dwarf_Die *        ]    Dwarf_Die
+--------------+       ]  +--------------------+
|      --------+-------]->|void *addr          |
+--------------+       ]  |Dwarf_CU *cu        |
                       ]  |Dwarf_Abbrev *abbrev|
                       ]  |padding             |
                       ]  |...                 |
                       ]  +--------------------+

           allocated by?         client
           freed how?            n/a

which we wrap as ...? not clear I've worked this out yet, but the obvious thing would be

struct Die, wrapping a       
unique_ptr to Dwarf_Die      non-opaque structure
 + adding abstract_die impl  Dwarf_Die
+==============+          +======================+
||     -------++--------->||void *addr          ||
+==============+          ||Dwarf_CU *cu        ||
                          ||Dwarf_Abbrev *abbrev||
    ^                     ||padding             ||
    |                     |'_...________________'|
 skip this?!              |dbg                   |     can we put these with the handle (far left)?
                          |root_die* p_constr... |
                          +----------------------+

           allocated by    client, on heap? via make_unique? haven't got that far yet?
                           try_construct returns a handle, i.e. the unique_ptr (not wrapped in a struct Die)
           perhaps we could memoise the structures? or otherwise avoid heap-allocating them
           can we make struct Die just be Dwarf_Die?
           i.e. when we construct one, we get libdw to fill it in, and that's that?
           handling exceptions -- hmm

If we keep the same shape for iterator_base...

                  .--- only used in the "cur_handle" case, maintained by linear treatment of the iterator
+==============+  v                                 
|||    ------+++------->the *non*-opaque structure?  basic_die or a subclass thereof
|''==========''|  OR                               +==============+
|      --------+---------------------------------->|||    ------+++------->the *non*-opaque structure?
+==============+  ^                                |''==========''|
                  '--- only used in the            | per-TAG data |
                      "cur_payload" case           |     ...      |   no need for a refcount?
                      when the iterator is copied  +==============+

Let's instead
use the non-opaque structure as our handle,
 (opaquely! because we need to hide access to the libdw fields in the case where
  we're not using that copy of the data)
core::Die : private Dwarf_Die
+========================+
|||void *addr		   ||| \
|||Dwarf_CU *cu  	   |||  \
|||Dwarf_Abbrev *abbrev|||  |--- used when just a handle
|||padding			   |||  /
||'_...________________'|| /
||dbg				    ||       .- used when "with payload"
||root_die* p_constr... ||       :   -- do we need the refcount still? yes I think so
|'----------------------'|       v
|           -------------+---------------------->
+------------------------+

Compared to libdwarf, we've removed a level of indirection -- the unique_ptr
to the libdwarf Die handle.
Instead we have a ~4-word handle inlined into our structures.
We still refcount when we promote into the heap.
And we have to do this if we want the payload behaviour --
which is whenever we dereference an iterator -- because the
per-DIE-tag classes will have differing sizes and so need
to be heap-allocated. We still do refcounting when we copy
these handles.

We *have* removed some heap allocation, but not in our own code --
it's the allocation inside libdwarf. In libdw, getting a handle on
a DIE does not involve a heap allocation.
*/


		struct Die :
#ifndef USING_LIBDWARF /* i.e. using libdw */
			private Dwarf_Die,
#endif
			public virtual abstract_die
		{
#ifdef USING_LIBDWARF
			typedef Dwarf_Die raw_handle_type;
			typedef Dwarf_Die_s opaque_type;
			struct deleter
			{
				Debug::raw_handle_type dbg;
				root_die *p_constructing_root; // HMM: really don't like this
				deleter(Debug::raw_handle_type dbg, root_die& r)
				 : dbg(dbg), p_constructing_root(&r) {}
				deleter(Debug::raw_handle_type dbg)
				 : dbg(dbg), p_constructing_root(nullptr) {}
				// also provide a lame default deleter that can only delete nullptr
				//deleter() : dbg(nullptr) {}
				// temporarily DISABLED while we check we only use it where necessary
				void operator ()(raw_handle_type arg) const 
				{ if (!dbg) assert(!arg); else if (arg) dwarf_dealloc(dbg, arg, DW_DLA_DIE); }
			};
			typedef unique_ptr<opaque_type, deleter> handle_type;
			handle_type handle;
#else /* USING_LIBDW */
			Debug::raw_handle_type dbg;
			root_die *p_constructing_root;
			typedef Dwarf_Die *raw_handle_type;
			typedef Die handle_type; // HMM... do I mean this?
			// WE *are* the handle
#endif
#ifdef USING_LIBDWARF
			Debug::raw_handle_type get_dbg() const { return handle.get_deleter().dbg; }
			root_die& get_constructing_root() const 
			{ return *handle.get_deleter().p_constructing_root; }
#else /* USING_LIBDW */
			Debug::raw_handle_type get_dbg() const { return dbg; }
			root_die& get_constructing_root() const { return *p_constructing_root; }
#endif
			// to avoid making exception handling compulsory, 
			// we provide static "maybe" constructor functions (defined in lib.hpp)...
			static handle_type 
			try_construct(root_die& r, const iterator_base& die); /* siblingof */
			static handle_type 
			try_construct(root_die& r); /* siblingof with null die */
			static handle_type 
			try_construct(const iterator_base& die); /* child */
			static handle_type 
			try_construct(root_die& r, Dwarf_Off off); /* offdie */

#ifdef USING_LIBDWARF
			// ... and an "upgrade" constructor that is guaranteed not to fail
			Die(handle_type h) : handle(std::move(h)) {}
#endif
			
			// ... and a "nullptr" constructor
#ifdef USING_LIBDWARF
			Die(std::nullptr_t n, root_die *p_r) : handle(nullptr, deleter(nullptr, *p_r)) {} 
#else /* USING_LIBDW */
			Die(std::nullptr_t n, root_die *p_r) : dbg(nullptr), p_constructing_root(p_r) {} 
#endif
			
			// ... then the "normal" constructors, that throw exceptions on failure
			Die(root_die& r, const iterator_base& die); /* siblingof */
			explicit Die(root_die& r); /* siblingof in the root case */
			explicit Die(const iterator_base& die); /* child */
			Die(root_die& r, Dwarf_Off off); /* offdie */
#ifdef USING_LIBDWARF
			// move constructor
			Die(Die&& d) : handle(std::move(d.handle)) {}
			// move assignment
			Die& operator=(Die&& d) { handle = std::move(d.handle); return *this; }

			raw_handle_type raw_handle()       { return handle.get(); }
			raw_handle_type raw_handle() const { return handle.get(); }
			// Dwarf_Error takes a Dwarf_Ptr argument... what's the pointer that refers to us?
			Dwarf_Ptr as_error_arg() const { return handle.get(); }
#else /* libdw */
			raw_handle_type raw_handle()       { return this; }
			raw_handle_type raw_handle() const { return const_cast<raw_handle_type>(static_cast<const Dwarf_Die *>(this)); }
			// FIXME: to support client code wanting to do "d.handle", it's tempting to
			// add a pointer to ourselves, called 'handle'. Let's try to fix up the code
			// that is doing 'd.handle', for now. If that is not feasible, we can add
			// the hack.

			Dwarf_Ptr as_error_arg() const { return const_cast<void*>(static_cast<const void*>(this)); }
#endif
			// libdwarf methods
			Dwarf_Off offset_here() const;
			Dwarf_Half tag_here() const;

			raw_name_t name_here() const;

			Dwarf_Off enclosing_cu_offset_here() const;
			bool has_attr_here(Dwarf_Half attr) const;
			bool has_attribute_here(Dwarf_Half attr) const { return has_attr_here(attr); }
			spec& spec_here() const;
			
			// for convenience, this one is public -- basic_die's subclasses call it
			// (whereas the rest of our abstract_die implementation is private)
			inline encap::attribute_map copy_attrs() const;

			friend class iterator_base;
		//private: 
			/* implement the abstract_die interface, but privately -- WHY? */
			inline Dwarf_Off get_offset() const { return offset_here(); }
			inline Dwarf_Half get_tag() const { return tag_here(); }
			inline opt<string> get_name() const 
#ifdef USING_LIBDWARF
			{ return name_here() ? opt<string>(string(name_here().get())) : opt<string>(); }
#else /* USING_LIBDW */
			{ return name_here() ? opt<string>(string(name_here())) : opt<string>(); }
#endif
			inline raw_name_t get_raw_name() const
			{ return name_here(); }
			inline Dwarf_Off get_enclosing_cu_offset() const 
			{ return enclosing_cu_offset_here(); }
			inline bool has_attr(Dwarf_Half attr) const { return has_attr_here(attr); }
			// inline encap::attribute_map copy_attrs(root_die& r) const; // -- declared above
			inline spec& get_spec(root_die& r) const { return spec_here(); }
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
#ifdef USING_LIBDWARF
		typedef struct Dwarf_Attribute_s*  Dwarf_Attribute;
#endif
		struct Attribute
		{
#ifdef USING_LIBDWARF
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
#else /* USING_LIBDW */
			struct handle_contents : Dwarf_Attribute
			{
				Debug::raw_handle_type dbg;
			};
			typedef Dwarf_Attribute *raw_handle_type;
			typedef unique_ptr<handle_contents> handle_type;
			// IMPORTANT: under libdw, the only data member of this class
			// should be a handle_type. This is so that we can use an array of
			// Attribute as an array of handles. See AttributeList below.
#endif
			handle_type handle;
#ifdef USING_LIBDWARF
			Debug::raw_handle_type get_dbg() const { return handle.get_deleter().dbg; }
#else /* USING_LIBDW */
			Debug::raw_handle_type get_dbg() const { return handle->dbg; }
#endif

			static inline handle_type 
			try_construct(const Die& it, Dwarf_Half attr);
			inline explicit Attribute(const Die& it, Dwarf_Half attr);
			inline Attribute(handle_type h) : handle(std::move(h)) {}
			
			raw_handle_type raw_handle()       { return handle.get(); }
			raw_handle_type raw_handle() const { return handle.get(); }
			
			// libdwarf methods
			Dwarf_Half attr_here() const;
			Dwarf_Half form_here() const;
		};
#ifdef USING_LIBDW
		static_assert(sizeof (Attribute) == sizeof (Attribute::handle_type));
#endif

		struct Locdesc
		{
#ifdef USING_LIBDWARF
			/* Locdesc is weird. Instead of being a pointer to an opaque type, 
			 * it's a non-opaque type embedding a pointer. These non-opaque types
			 * are allocated by libdwarf, however. Threfore, our "handle" is the
			 * address of one of these non-opaque types. But we are still responsible
			 * for deallocating *both* the embedded pointer *and* the libdwarf-allocated
			 * non-opaque object. So there is an extra level of indirection in all this.
			 *
			 * Also, we can construct Locdescs either as part of a list using dwarf_loclist_n, 
			 * or as single instances e.g. with dwarf_loclist_from_expr.
			  */
			typedef Dwarf_Locdesc *raw_handle_type;
			struct deleter
			{
				Debug::raw_handle_type dbg;
				deleter(Debug::raw_handle_type dbg) : dbg(dbg) {}
				void operator()(raw_handle_type arg) const
				{
					dwarf_dealloc(dbg, arg->ld_s, DW_DLA_LOC_BLOCK);
					dwarf_dealloc(dbg, arg, DW_DLA_LOCDESC);
				}
			};
			typedef unique_ptr<Dwarf_Locdesc, deleter> handle_type;
#else /* USING_LIBDW */
			struct handle_contents : Dwarf_Locdesc
			{
				Debug::raw_handle_type dbg;
			};
			typedef unique_ptr<handle_contents> handle_type;
			typedef handle_contents *raw_handle_type;
#endif
			handle_type handle;
#ifdef USING_LIBDWARF
			Debug::raw_handle_type get_dbg() const { return handle.get_deleter().dbg; }
#else /* USING_LIBDW */
			Debug::raw_handle_type get_dbg() const { return handle->dbg; }
#endif

			/* LocdescList can create individual Locdescs in a list. */
			inline Locdesc(handle_type h) : handle(std::move(h)) {}
			/* Individual Locdescs can be constructed too. */
			static inline handle_type 
			try_construct(const Attribute& a);
			static inline handle_type 
			try_construct(Debug::raw_handle_type dbg, Dwarf_Ptr bytes_in, Dwarf_Unsigned bytes_len);
			
			raw_handle_type raw_handle()       { return handle.get(); }
			raw_handle_type raw_handle() const { return handle.get(); }
		};
#ifdef USING_LIBDWARF
		typedef struct Dwarf_Attribute_s*  Dwarf_Attribute;
#endif

		struct Block
		{
#ifdef USING_LIBDWARF
			/* Block is special because it doesn't have an opaque type. */
			typedef Dwarf_Block *raw_handle_type; 
			struct deleter 
			{ 
				Debug::raw_handle_type dbg; 
				deleter(Debug::raw_handle_type dbg) : dbg(dbg) {} 
				void operator()(raw_handle_type arg) const 
				{ 
					dwarf_dealloc(dbg, arg, DW_DLA_BLOCK); 
				} 
			}; 
			typedef unique_ptr<Dwarf_Block, deleter> handle_type; 
#else /* USING_LIBDW */
			struct handle_contents : Dwarf_Block
			{
				Debug::raw_handle_type dbg;
			};
			typedef handle_contents *raw_handle_type;
			typedef unique_ptr<handle_contents> handle_type;
#endif
			handle_type handle; 
			static inline handle_type 
			try_construct(const Attribute& a); 
			inline explicit Block(const Attribute& a); 
			inline Block(handle_type h) : handle(std::move(h)) { /* "upgrade" constructor */ 
				if (!handle) throw Error(current_dwarf_error, 0); 
			} 
			raw_handle_type raw_handle()       { return handle.get(); } 
			raw_handle_type raw_handle() const { return handle.get(); } 
#ifdef USING_LIBDWARF
			Debug::raw_handle_type get_dbg() const { return handle.get_deleter().dbg; } 
#else /* USING_LIBDW */
/* FIXME: see how many of these get_dbg() methods we can delete without breaking stuff.
 * Then we can get rid of the handle_contents structs too. */
			Debug::raw_handle_type get_dbg() const { return handle->dbg; } 
#endif
		};

#ifdef USING_LIBDWARF
		/* Ranges is special: we never get a single range, only a list, 
		 * and we can never deallocate a single range. So there's no "handle"
		 * on a Range, so we don't bother with a class for it. */
/* We instantiate this for Line, Arange, Global */
#define basic_handle(Fragment, ConstructorArgs...) \
		/* typedef struct Dwarf_ ## Fragment ## _s*  Dwarf_ ## Fragment; */ \
		struct Fragment \
		{ \
			typedef Dwarf_ ## Fragment raw_handle_type; \
			typedef Dwarf_ ## Fragment ## _s opaque_type; \
			struct deleter \
			{ \
				Debug::raw_handle_type dbg; \
				deleter(Debug::raw_handle_type dbg) : dbg(dbg) {} \
				void operator()(raw_handle_type arg) const \
				{ \
					dwarf_dealloc(dbg, arg, DEALLOC_TOKEN_ ## Fragment); \
				} \
			}; \
			typedef unique_ptr<opaque_type, deleter> handle_type; \
			 \
			handle_type handle; \
			 \
			static inline handle_type \
			try_construct(ConstructorArgs); \
			inline explicit Fragment(ConstructorArgs); \
			inline Fragment(handle_type h) : handle(std::move(h)) { /* "upgrade" constructor */ \
				if (!handle) throw Error(current_dwarf_error, 0); \
			} \
			raw_handle_type raw_handle()       { return handle.get(); } \
			raw_handle_type raw_handle() const { return handle.get(); } \
			Debug::raw_handle_type get_dbg() const { return handle.get_deleter().dbg; } \
		};
#else /* USING_LIBDW */
/* libdw also has some opaque types that fit this pattern:
 * Dwarf_Lines_s, Dwarf_Files_s, Dwarf_Arange_s.
 * One difference is that there is no dealloc function;
 * they are remembered by the library in its Dwarf_CU
 * structure (which is opaque to us). */
#define basic_handle(Fragment, ConstructorArgs...) \
		struct Fragment \
		{ \
			typedef Dwarf_ ## Fragment raw_handle_type; \
			typedef Dwarf_ ## Fragment ## _s opaque_type; \
			struct deleter \
			{ \
				Debug::raw_handle_type dbg; \
				deleter(Debug::raw_handle_type dbg) : dbg(dbg) {} \
				void operator()(raw_handle_type arg) const { /* DO NOTHING */ } \
			}; \
			typedef unique_ptr<opaque_type, deleter> handle_type; \
			 \
			handle_type handle; \
			 \
			static inline handle_type \
			try_construct(ConstructorArgs); \
			inline explicit Fragment(ConstructorArgs); \
			inline Fragment(handle_type h) : handle(std::move(h)) { /* "upgrade" constructor */ \
				if (!handle) throw Error(current_dwarf_error, 0); \
			} \
			raw_handle_type raw_handle()       { return handle.get(); } \
			raw_handle_type raw_handle() const { return handle.get(); } \
			Debug::raw_handle_type get_dbg() const { return handle.get_deleter().dbg; } \
		};
#endif

		struct AttributeList
		{
#ifdef USING_LIBDWARF
			/* In libdwarf, dwarf_attrlist() gives us a list of Dwarf_Attribute
			 * pointers (opaque); the list is freed as a single unit, but
			 * only *after* each Attribute has been individually freed. See note
			 * on copied_list below.
			 *
			 * Ideally we would in-place construct a unique_ptr array over the
			 * actual returned array. Then, to use this, the client would std::move
			 * elements out of the array. This allows individual attrs to be used
			 * and deallocated early, like the C style.
			 *
			 * But a problem: that can only work if unique_ptrs are the same size/rep
			 * as normal ptrs, which means no deleter state. Our deleters need a
			 * reference to the dbg, so this doesn't work. NOTE that this is talking
			 * about the individual-element so they do not store the block length.
			 *
			 * For now we just copy the array of pointers into a vector of the
			 * Attribute (our wrapper around Dwarf_Attribute_ opaque pointers), and
			 * use that to service client requests. Clients don't get access to this
			 * array directly. */
			typedef Dwarf_Attribute *raw_handle_type; /* What libdwarf returns us. */
			typedef Dwarf_Attribute raw_element_type; // what we get when we index the raw handle
			typedef Attribute::handle_type copied_element_type;
			/* This is a whole-list deleter. Although dwarf_dealloc doesn't 
			 * need the list length, we store it in the deleter so that it
			 * is embedded in each unique_ptr instance. */
			struct deleter
			{
				Debug::raw_handle_type dbg;
				Dwarf_Signed len;
				deleter(Debug::raw_handle_type dbg, Dwarf_Signed len)
				 : dbg(dbg), len(len) {} 
				void operator()(raw_handle_type arg) const
				{
					if (len > 0) dwarf_dealloc(dbg, arg, DW_DLA_LIST);
				}
			};
			typedef unique_ptr<raw_element_type, deleter> handle_type;
			handle_type handle;
			Die const& d; /* SPECIAL: we have to track the Die too,
			 * so that we can construct encap::attribute_value,
			 * so that operator<< can work. */
			Dwarf_Debug get_dbg() const { return handle.get_deleter().dbg; }
			// IMPORTANT: this copied_list must come *after* the handle in the 
			// field order, because it must be destructed *first*. We want to
			// delete the individual Attributes, using the unique_ptr destructor,
			// then delete the whole list using our whole-list deleter.
			vector<core::Attribute> copied_list;
			Attribute& operator[](Dwarf_Signed i) { return copied_list.at(i); }
			Attribute const& operator[](Dwarf_Signed i) const 
			{ return copied_list.at(i); }
			Dwarf_Signed get_len() const { return handle.get_deleter().len; }
			inline void copy_list()
			{
				for (Dwarf_Signed i = 0; i < handle.get_deleter().len; ++i)
				{
					copied_list.push_back(
						Attribute::handle_type(
							handle.get()[i], 
							Attribute::deleter(get_dbg())
						)
					);
				}
			}
			inline AttributeList(handle_type h, const Die& d) : handle(std::move(h)), d(d) /* "upgrade" constructor */ 
			{
				/* we tolerate null handles -- it just means the empty list. */
				if (handle) copy_list();
			}
			// can't define most of these now, because iterator_base is currently incomplete
			static inline handle_type
			try_construct(const Die& it);

#else /* USING_LIBDW */
			/* In libdw, we have a single non-opaque Dwarf_Attribute structure
			 * that is populated by a callback-based dwarf_getattrs() call.
			 * Recall our usual pattern from libdwarf:
			 * "raw_handle" is a plain ptr to the libdwarf-created opaque thing,
			 * "handle"     is a unique_ptr to the same  (with custom deleter).
			 * These classes, like AttributeList, are wrappers of handles.
			 * The "upgrade constructor" will turn a handle into an instance of the wrapper.
			 * The "try_construct()" helper is a uniform way to get a handle,
			 * in a might-fail (might-return-null) way.
			 *
			 * In our case, there is no libdw-created equivalent of an attribute
			 * list. But we can allocate a list ourselves...
			 * then an upgrade constructor 
			 */
			typedef vector< core::Attribute /* or just Dwarf_Attribute? */ > handle_payload_t;
			typedef unique_ptr< handle_payload_t > handle_type;
			typedef vector< handle_payload_t >    *raw_handle_type;
			handle_type handle;
			// If we followed the libdwarf pattern, the copied list would be
			// a vector of (our wrappers of) *pointers* to libdwarf opaque attributes.
			// Rather pointless in our case. We hold the attributes ourselves.
			// So there's no copied list!
			// Let's just define the operators to do the access directly.
			// Problem: what about the return type?
			// OK, if what we allocate really is a vector of Attributes then we're OK?
			core::Attribute&       operator[](Dwarf_Signed i)       { return handle->at(i); }
			core::Attribute const& operator[](Dwarf_Signed i) const { return handle->at(i); }
			Dwarf_Signed get_len() const { return handle->size(); }
			inline AttributeList(handle_type h, const Die& d) : handle(std::move(h)) {
				if (!handle) throw Error(nullptr, d.as_error_arg());
			} /* "upgrade" constructor */ 

			// TEST: let's not provide this, and see whether anything breaks
			//Debug::raw_handle_type get_dbg() const {  }
			
			static int libdw_attr_cb(Dwarf_Attribute *a, void *arg);
			static inline handle_type
			try_construct(const Die& d)
			{
				handle_type new_vec = std::make_unique< handle_payload_t > ();
				ptrdiff_t ret = dwarf_getattrs(d.raw_handle(), libdw_attr_cb,
					new_vec.get(), 0);
				// success means ret == 1 ("got to the end"), failure -1,
				// anything else "DWARF_CB_OK was returned at this offset"
				if (ret == -1) return nullptr;
				return new_vec;
			}
#endif
			inline explicit AttributeList(const Die& it);

			// FIXME: get raw handle?
			
			/* Destruction logic:
			 * Suppose our constructor in-place reconstructed 
			 * the libdwarf-returned ptrblock 
			 * as a unique_ptr block. How can we destruct these
			 * unique_ptrs? Can we delete[] an in-place-alloc'd 
			 * array block? Seems doubtful, because we have no new[]-cookie. 
			 * If there were a delete[n] in C++, that would be ideal.
			 * Instead, the recommended option is explicit destructor
			 * calls. Oh well. We stick with copying for now. 
			 * We need to copy the libdwarf-returned array into our own
			 * unique_ptr vector, use that, and then free both
			 * the original (using whole-list deleter, above; happens automatically)
			 * and the copy (using vector destructor, also happens automatically).
			 * Let's do that for now. */
		};

#ifdef USING_LIBDWARF
/* Things that come in blocks needing DW_DLA_LIST treatment: */
#define DEALLOC_TOKEN_Attribute DW_DLA_ATTR
#define DEALLOC_TOKEN_Line      DW_DLA_LINE
#define DEALLOC_TOKEN_Func      DW_DLA_FUNC /* SGI-specific */
#define DEALLOC_TOKEN_Type      DW_DLA_TYPENAME /* SGI-specific */
#define DEALLOC_TOKEN_Var       DW_DLA_VAR /* SGI-specific */
#define DEALLOC_TOKEN_Weak      DW_DLA_WEAK /* SGI-specific */
#define DEALLOC_TOKEN_Arange    DW_DLA_ARANGE
#define DEALLOC_TOKEN_Global    DW_DLA_GLOBAL
#define DEALLOC_TOKEN_Block     DW_DLA_BLOCK

/* These are fairly normal singleton things */
#define DEALLOC_TOKEN_Error     DW_DLA_ERROR
#define DEALLOC_TOKEN_Abbrev    DW_DLA_ABBREV

/*  Dwar_Locdesc** a.k.a.  Dwarf_Locdesc* a.k.a.                            | stuff here is specific to Dwarf_Locdesc
    core::LocdescList      core::Locdesc::raw_handle_type                   | and has no equivalent for Arange, Global etc
      ::raw_handle_type                               Dwarf_Locdesc -  -  - | ---since these are opaque, unlike Dwarf_Locdesc
+--------------+          +--------------+            +----------------     |
|      --------+--------->|      --------+----------->|               |     |    +------------
+--------------+          +--------------+            |===============+-----|--->|    |    | ...
                          |      --------+            |               |     |    +------------
                          +--------------+\           +---------------+     |    array of Dwarf_Loc a.k.a. Dwarf_Op
                          |      ---------.\                                |
                          +--------------+ \\         +---------------+     |
                                            \-------->|               |
                      ...many allocated at   \        |===============+--------->
                      once.                   \       |               |
                                               \      +---------------+
                                                \
                                                 \    +---------------+
                                                  --->| ...           |

          allocated by?      libdwarf               libdwarf                     also libdwarf
         deallocated how?    explicit client call   explicit client call         explicit client call per array (BLOCK)
                             for whole array (LIST) for *each* locdesc (LOCDESC)
                  
and we wrap this as a core::LocdescList structure
  +==============+
  ||     -------++-----------^  wrap the raw handle to a list
  +`------------'|              s.t. the wrapped handle's deleter does the LIST deletion
  |.------------.|
  | vector of    +-----------------------------^ after the vector destructor
  | core::Locdesc+-----------------------------^ has run the Locdesc::deleter
  | ::handle_type+-----------------------------^ on each individual Locdesc
  | a.k.a.       +-----------------------------^
  | unique_ptr<  +-----------------------------^  ... the deleter does both the BLOCK
  |Dwarf_Locdesc,+-----------------------------^      and the LOCDESC dwarf_dealloc calls
  |Locdesc::delet+-----------------------------^
  |er>           +-----------------------------^
  +'============'+                                (WHY do we copy into the vector? We could just
                                                   do the Locdesc deletions when the raw handle is deleted,
                                                   i.e. immediately before the list is deleted.
                                                   The reason is to enable uniformity with the other list
                                                   types. The unique_ptr deleter takes care of the struct-specific
                                                   deletion, and the list deleter is generic -- it just does the LIST
                                                   deletion.)

**and** the other list types are handled similarly-ish. So we have LocdescList, LineList,
ArangeList, GlobalList. The difference is that there is no non-opaque Dwarf_Locdesc
equivalent for Arange or Global. Instead 

Now for libdw.

                                                     pointer(s) return-written by dwarf_getlocation_addr(),
                                                     dwarf_get_locations()
                                                     and similar
                                                                                 array(s) of Dwarf_Op a.k.a. Dwarf_Loc
                                                     +--------------+            +------------
                                                     |      --------+----------->|   |   | ...
                                                     +--------------+            +------------
                                                     |      --------+----.       +------------
                                                     +--------------+     '----->|   |   | ... 
                                                      ...                        +------------
         allocated by?                               caller                      libdw
        deallocated how?                             n/a                         no deallocation needed(?)

                                                     libdwarf's Dwarf_Locdesc has no equivalent in libdw:
                                                     its other fields like hipc, lopc etc
                                                     are returned additionally by some calls

and we wrap this how? just a vector of pointers, I guess...
make them unique_ptr if we want to prevent copying, for uniformity with libdwarf
i.e. the "handle" is this vector, confusingly

Is it really worth supporting libdw and libdwarf simultaneously?
Unless we forward-port to the latest libdwarf, this support is very low value,
except maybe for testing the port. Can we test the port another way?
Hmm, it does appeal to do a one-off test like this. Maybe it is worth proceeding as we have been.


*/





/* The following is generalised from LocdescList */
#define list_handle(Fragment, ConstructorArgs...) \
		struct Fragment ## List \
		{ \
			typedef Fragment::raw_handle_type *raw_handle_type; /* What libdwarf returns us. */ \
			/* don't say opaque_type... */ \
			/* typedef Fragment::opaque_type *raw_element_type; */ \
			/* ... because LocDesc doesn't have one (the struct is non-opaque). But it does have... */ \
			typedef Fragment::raw_handle_type raw_element_type; \
			typedef Fragment::handle_type copied_element_type; \
			/* This is a whole-list deleter. For LocdescList, note that raw_handle_type */ \
			/* is a Locdesc**, i.e. Locdesc::raw_handle_type is a Locdesc* */ \
			/* and our typedef above adds a level of indirection to that. */ \
			struct deleter \
			{ \
				Debug::raw_handle_type dbg; \
				Dwarf_Unsigned len; \
				deleter(Debug::raw_handle_type dbg, Dwarf_Signed len) : dbg(dbg), len(len) {}  \
				void operator()(raw_handle_type arg) const \
				{ \
					dwarf_dealloc(dbg, arg, DW_DLA_LIST); \
				} \
			}; \
			Debug::raw_handle_type get_dbg() const { return handle.get_deleter().dbg; } \
			typedef unique_ptr<raw_element_type, deleter> handle_type; \
			handle_type handle; \
			std::vector<copied_element_type> copied_list; /* comes after "handle" for destruction order */ \
			static inline handle_type \
			try_construct(ConstructorArgs); \
			/* we can't have this constructor because we need the bare arg names... */ \
			/* inline explicit Fragment ## List(ConstructorArgs...) */ \
			/* : handle(try_construct(ConstructorArgs...)) */ \
			/* { assert(handle); copy_list(); } */ \
			inline void copy_list() \
			{ \
				for (unsigned i = 0; i < handle.get_deleter().len; ++i) \
				{ \
					copied_list.push_back( \
						copied_element_type( \
							std::move(handle.get()[i]), Fragment::deleter(get_dbg()) \
						) \
					); \
				} \
			} \
			inline Fragment ## List(handle_type h) : handle(std::move(h)) { \
				/* tolerate null handle -- means empty list */ \
				if (handle) copy_list(); \
			} \
		};

		/* Do the basic handles -- commenting out the SGI-specific ones for now */
		basic_handle(Line, const iterator_base& it) /* dwarf_srclines -- allocates a block; free each line, free block */
		basic_handle(Arange) /* dwarf_get_arange and dwarf_get_aranges */
		basic_handle(Global) /* dwarf_get_globals */

		/* Do the list handles. */
		list_handle(Locdesc, const Attribute& a)      // defines LocdescList
		list_handle(Line, const iterator_base& it)    // defines LineList
		list_handle(Arange)                           // defines ArangeList
		list_handle(Global)                           // defines GlobalList
#else /* libdw */
		/* What does a LocdescList handle look like?
		   RECALL:
		   libdwarf has Dwarf_Locdesc:
		      a lib-allocated, non-opaque type embedding a pointer to an array of zero or more non-opaque Dwarf_Loc (op)s
		            i.e. libdwarf's Dwarf_Loc should really be called Dwarf_Op
		            s... in libdw, it is!
		   libdw has no equivalent, instead:
		      an array-copying function:
		        dwarf_getlocation (Dwarf_Attribute *attr, Dwarf_Op **expr, size_t *exprlen) __nonnull_attribute__ (2, 3);
		
		   ... i.e. I THINK it returns us <a pointer to zero or more Dwarf_Ops, a size_t> ?
		
			and
			
			 Return location expressions.  If the attribute uses a location list,
   ADDRESS selects the relevant location expressions from the list.
   There can be multiple matches, resulting in multiple expressions to
   return.  EXPRS and EXPRLENS are parallel arrays of NLOCS slots to
   fill in.  Returns the number of locations filled in, or -1 for
   errors.  If EXPRS is a null pointer, stores nothing and returns the
   total number of locations.  A return value of zero means that the
   location list indicated no value is accessible. 
extern int dwarf_getlocation_addr (Dwarf_Attribute *attr, Dwarf_Addr address,
                                   Dwarf_Op **exprs, size_t *exprlens,
                                   size_t nlocs);
			and

Enumerate the locations ranges and descriptions covered by the
   given attribute.  In the first call OFFSET should be zero and
   *BASEP need not be initialized.  Returns -1 for errors, zero when
   there are no more locations to report, or a nonzero OFFSET
   value to pass to the next call.  Each subsequent call must preserve
   *BASEP from the prior call.  Successful calls fill in *STARTP and
   *ENDP with a contiguous address range and *EXPR with a pointer to
   an array of operations with length *EXPRLEN.  If the attribute
   describes a single location description and not a location list the
   first call (with OFFSET zero) will return the location description
   in *EXPR with *STARTP set to zero and *ENDP set to minus one.
extern ptrdiff_t dwarf_getlocations (Dwarf_Attribute *attr,
                                     ptrdiff_t offset, Dwarf_Addr *basep,
                                     Dwarf_Addr *startp, Dwarf_Addr *endp,
                                     Dwarf_Op **expr, size_t *exprlen);

		... so the latter callback-style one is the one we use to enumerate the list.
		We can do this in our constructor.

		 */
		struct LocdescList
		{
			typedef Locdesc::raw_handle_type *raw_handle_type;
			typedef Locdesc::raw_handle_type raw_element_type;
			typedef Locdesc::handle_type copied_element_type;
			Debug::raw_handle_type get_dbg() const { return handle.get_deleter().dbg; }
			typedef unique_ptr<raw_element_type, deleter> handle_type;
			handle_type handle;
			std::vector<copied_element_type> copied_list; /* comes after "handle" for destruction order */
			static inline handle_type
			try_construct(const Attribute& a);
			inline void copy_list();
			// construct a LocdescList from a Locdesc handle -- recall that in libdwarf
			// a Locdesc handle is pointer to zero or more locdescs. Each locdesc
			// embeds a pointer to one or more locations, allocated by the library.
			// And all these are non-opaque. ("Locdesc is weird", above.)
            // in libdw, we use a callback to enumerate the list.
            // Each time around the callback loop we get an address range and a pointer
            // to a library-allocated array of operations.
            // This is really not unlike libdwarf!
            // EXCEPT That there is no Dwarf_Locdesc equivalent, just the raw array
            // and the return values (start addr, end addr, lenth)
            // that we get at each callback invocation.
            // We have defined Dwarf_Locdesc ourselves.
            // So maybe we should just fill one in, each time we do the callback?
            // YES I think so.
            // But how do we allocate the locdescs themselves?
            // Remember that we had: typedef Dwarf_Locdesc *raw_handle_type;
            // So again I think we need a level of indirection
			inline LocdescList(handle_type h) : handle(std::move(h)) {
				/* tolerate null handle -- means empty list */
				if (handle) copy_list();
			}
			inline void copy_list()
			{
			
				for (unsigned i = 0; i < handle.get_deleter().len; ++i)
				{
					copied_list.push_back(
						copied_element_type(
							std::move(handle.get()[i]), Fragment::deleter(get_dbg())
						)
					);
				}
			}

		};

		/* FIXME: equivalents of
		 * Dwarf_Line
		 * Dwarf_Arange
		 * Dwarf_Global
		 * Dwarf_LineList
		 * Dwarf_ArangeList
		 * Dwarf_GlobalList
		 */
#endif

#ifndef LIBDW_SUPPORT_NOT_FINISHED
		/* RangesList is special because it uses its own deallocation function. Also,
		 * don't bother to copy the list. */
		struct RangesList
		{ 
			typedef Dwarf_Ranges *raw_handle_type; /* What libdwarf returns us. */ 
			typedef Dwarf_Ranges raw_element_type; 
			/* This is a whole-list deleter. */ \
			struct deleter 
			{ 
				Debug::raw_handle_type dbg;
				Dwarf_Signed len;
				deleter(Debug::raw_handle_type dbg, Dwarf_Signed len) : dbg(dbg), len(len) {} 
				void operator()(raw_handle_type arg) const
				{
					if (arg && arg != (void*)-1) dwarf_ranges_dealloc(dbg, arg, len);
					else assert(len == 0);
				}
			};
			Debug::raw_handle_type get_dbg() const { return handle.get_deleter().dbg; }
			typedef unique_ptr<raw_element_type, deleter> handle_type;
			handle_type handle;

			static inline handle_type
			try_construct(const Attribute& a);
			static inline handle_type
			try_construct(const Attribute& a, const Die& d);
			// helper
			static inline Dwarf_Unsigned
			get_rangelist_offset(const Attribute& a);

			RangesList(handle_type h) : handle(std::move(h)) { /* "upgrade" */
				if (!handle) throw Error(current_dwarf_error, 0);
			}
			RangesList(const Attribute& a) : handle(try_construct(a)) 
			{ if (!handle) throw Error(current_dwarf_error, 0); }
			RangesList(const Attribute& a, const Die& d) : handle(try_construct(a, d)) 
			{ if (!handle) throw Error(current_dwarf_error, 0); }
		};

		/* srcfiles, which is a list of strings */
		struct StringList
		{
			typedef char **raw_handle_type; /* What libdwarf returns us. */
			typedef char *raw_element_type;
			typedef unique_ptr<char, string_deleter> copied_element_type;
			/* This is a whole-list deleter. Although dwarf_dealloc doesn't 
			 * need the list length, we store it in the deleter so that it
			 * is embedded in each unique_ptr instance. */
			struct deleter
			{
				Debug::raw_handle_type dbg;
				Dwarf_Signed len;
				deleter(Debug::raw_handle_type dbg, Dwarf_Signed len)
				 : dbg(dbg), len(len) {} 
				void operator()(raw_handle_type arg) const
				{
					if (len > 0) dwarf_dealloc(dbg, arg, DW_DLA_LIST);
				}
			};
			
			typedef unique_ptr<raw_element_type, deleter> handle_type;
			handle_type handle;
			//Die const& d; /* SPECIAL: we have to track the Die too,
			// * so that we can construct encap::attribute_value,
			// * so that operator<< can work. */
			  
			vector<copied_element_type> copied_list; // see note in destructor
			Dwarf_Debug get_dbg() const { return handle.get_deleter().dbg; }
			// IMPORTANT: this copied_list must come *after* the handle in the 
			// field order

			Dwarf_Signed get_len() const { return handle.get_deleter().len; }
			char * operator[](Dwarf_Signed i) { return copied_list.at(i).get(); }
			char * operator[](Dwarf_Signed i) const { return copied_list.at(i).get(); }

			static inline handle_type
			try_construct(const Die& it);
			inline void copy_list()
			{
				for (Dwarf_Signed i = 0; i < handle.get_deleter().len; ++i)
				{
					copied_list.push_back(
						unique_ptr<char, string_deleter>(
							handle.get()[i], 
							string_deleter(get_dbg())
						)
					);
				}
			}
			inline explicit StringList(const Die& it);
			inline StringList(handle_type h, const Die& d) : handle(std::move(h)) /* "upgrade" constructor */ 
			{
				/* we tolerate null handles -- it just means the empty list. */
				if (handle) copy_list();
			}

			// FIXME: get raw handle?
			
			/* Destruction logic:
			 * Suppose our constructor in-place reconstructed 
			 * the libdwarf-returned ptrblock 
			 * as a unique_ptr block. How can we destruct these
			 * unique_ptrs? Can we delete[] an in-place-alloc'd 
			 * array block? Seems doubtful, because we have no new[]-cookie. 
			 * If there were a delete[n] in C++, that would be ideal.
			 * Instead, the recommended option is explicit destructor
			 * calls. Oh well. We stick with copying for now. 
			 * We need to copy the libdwarf-returned array into our own
			 * unique_ptr vector, use that, and then free both
			 * the original (using whole-list deleter, above; happens automatically)
			 * and the copy (using vector destructor, also happens automatically).
			 * Let's do that for now. */
		};
		
		// alias for LocdescList
		typedef LocdescList LocList;
		typedef RangesList RangeList;

		inline Attribute::handle_type 
		Attribute::try_construct(const Die& h, Dwarf_Half attr)
		{
			raw_handle_type returned;
			int ret = dwarf_attr(h.raw_handle(), attr, &returned, &current_dwarf_error);
			if (ret == DW_DLV_OK) return handle_type(returned, deleter(h.get_dbg()));
			else return handle_type(nullptr, deleter(nullptr)); // could be ERROR or NO_ENTRY
		}
		inline Attribute::Attribute(const Die& h, Dwarf_Half attr)
		 : handle(try_construct(h, attr))
		{
			if (!this->handle) throw Error(current_dwarf_error, 0);
		}
		inline AttributeList::handle_type
		AttributeList::try_construct(const Die& h)
		{
			Dwarf_Attribute *block_start;
			Dwarf_Signed count;
			int ret = dwarf_attrlist(h.raw_handle(), &block_start, &count, &current_dwarf_error);
			if (ret == DW_DLV_OK)
			{
				/* Since we are try_construct, the most we can do is 
				 * pass a unique_ptr to the allocated block, where that
				 * unique_ptr's deleter includes the length of the block. */
				assert(count != 0); // this would be ambiguous w.r.t the NO_ENTRY case
				return handle_type(block_start, deleter(h.get_dbg(), count));
			}
			else if (ret == DW_DLV_NO_ENTRY)
			{
				/* We allow zero-length AttributeLists. 
				   HACK: Use (void*)-1 as the block_start. */
				return handle_type((raw_element_type*)-1, deleter(h.get_dbg(), 0));
			}
			else
			{
				return handle_type(nullptr, deleter(nullptr, 0));
			}
		}
		inline AttributeList::AttributeList(const Die& d)
		 : handle(try_construct(d)), d(d)
		{
			if (!handle) throw Error(current_dwarf_error, 0);
			/* Create a unique_ptr to each attribute in the block.
			 * Note: the block contains Dwarf_Attributes, i.e. 
			 * Dwarf_Attribute_s pointers.
			 * We have to take each block element in turn
			 * and make it into an Attribute::handle. */
			copy_list();
		}
		
		inline StringList::handle_type
		StringList::try_construct(const Die& h)
		{
			char **block_start;
			Dwarf_Signed count;
			int ret = dwarf_srcfiles(h.raw_handle(), &block_start, &count, &current_dwarf_error);
			if (ret == DW_DLV_OK)
			{
				assert(count > 0);
				return handle_type(block_start, deleter(h.get_dbg(), count));
			}
			else if (ret == DW_DLV_NO_ENTRY)
			{
				return handle_type((raw_element_type*)-1, deleter(h.get_dbg(), 0));
			}
			else return handle_type(nullptr, deleter(nullptr, 0));
		}
		inline StringList::StringList(const Die& d)
		 : handle(try_construct(d))//, d(d)
		{
			if (!handle) throw Error(current_dwarf_error, 0);
			/* Create a unique_ptr to each attribute in the block.
			 * Note: the block contains char pointers. 
			 * We have to take each block element in turn
			 * and make it into an unique_ptr<char, string_deleter>. */
			copy_list();
		}
		
		inline Locdesc::handle_type
		Locdesc::try_construct(const Attribute& a)
		{
			Dwarf_Unsigned exprlen;
			Dwarf_Ptr block_ptr;
			int ret = dwarf_formexprloc(a.handle.get(), &exprlen, &block_ptr, 
				&core::current_dwarf_error);
			assert(ret == DW_DLV_OK);
			
			Dwarf_Locdesc *raw_handle;
			Dwarf_Signed listlen; // will be set to 1
			/* libdwarf can fail here if it doesn't understand an opcode in the 
			 * expression (e.g. vendor extensions). We tolerate it by passing
			 * back null to the caller. */
			ret = dwarf_loclist_from_expr(a.get_dbg(), block_ptr, exprlen, &raw_handle, &listlen, &current_dwarf_error);
			if (ret != DW_DLV_OK)
			{
				debug() << "Warning: libdwarf didn't understand DWARF expression in " //DIE 0x"
					<< std::hex /*<< a.d.get_offset() << ", */ << "attribute " << DEFAULT_DWARF_SPEC.attr_lookup(a.attr_here()) << std::dec
					<< std::endl;
				return handle_type(nullptr, deleter(a.get_dbg()));
			}
			assert(listlen == 1);

			return handle_type(raw_handle, deleter(a.get_dbg()));
		}
		
		inline Locdesc::handle_type
		Locdesc::try_construct(Dwarf_Debug dbg, Dwarf_Ptr bytes_in, Dwarf_Unsigned bytes_len)
		{
			Dwarf_Locdesc *raw_handle;
			Dwarf_Signed listlen; // will be set to 1
			/* libdwarf can fail here if it doesn't understand an opcode in the 
			 * expression (e.g. vendor extensions). We tolerate it by passing
			 * back null to the caller. */
			int ret = dwarf_loclist_from_expr(dbg, bytes_in, bytes_len, &raw_handle, &listlen, &current_dwarf_error);
			if (ret != DW_DLV_OK)
			{
				debug() << "Warning: libdwarf didn't understand DWARF expression from caller."
					<< std::endl;
				return handle_type(nullptr, deleter(dbg));
			}
			assert(listlen == 1);

			return handle_type(raw_handle, deleter(dbg));
		}
		
		inline LocdescList::handle_type 
		LocdescList::try_construct(const Attribute& a)
		{
			/* dwarf_loclist_n returns us
			 * a pointer 
			 *   to an array 
			 *	  of pointers 
			 *		to Locdescs.
			 * We will copy each pointer in the array into our vector of Locdesc handles. */
			Dwarf_Locdesc **block_start;
			Dwarf_Signed count = 0;
			int ret = dwarf_loclist_n(a.raw_handle(), &block_start, &count, &current_dwarf_error);
			if (ret == DW_DLV_OK)
			{
				assert(count > 0);
				/* Now what? handle_type is a unique_ptr<Dwarf_Locdesc*>
				 * pointing at an array of Dwarf_Locdesc*s . 
				 * We will make unique_ptrs out of each of them, but only when
				 * we upgrade this handle and copy the array. */
				return handle_type(block_start, deleter(a.get_dbg(), count));
			} else return handle_type(nullptr, deleter(nullptr, 0));
		}
		inline Dwarf_Unsigned 
		RangeList::get_rangelist_offset(const Attribute& a)
		{
			/* Since DWARF4, form can be unsigned or sec_offset, so we  
			 * check it here. */
			Dwarf_Half form;
			int retF = dwarf_whatform(a.handle.get(), &form, &core::current_dwarf_error); 
			if (retF == DW_DLV_OK)
			{
				int ret;
				switch (form)
				{
					case DW_FORM_udata: {
						Dwarf_Unsigned ranges_off;
						ret = dwarf_formudata(a.handle.get(), &ranges_off, &core::current_dwarf_error); 
						return (ret == DW_DLV_OK) ? ranges_off : (Dwarf_Unsigned) -1;
					}
					case DW_FORM_data4: {
						Dwarf_Signed ref;
						ret = dwarf_formsdata(a.handle.get(), &ref, &core::current_dwarf_error); 
						return (ret == DW_DLV_OK) ? ref : (Dwarf_Unsigned) -1;
					}
					case DW_FORM_sec_offset: {
						Dwarf_Off ref; 
						ret = dwarf_global_formref(a.handle.get(), &ref, &core::current_dwarf_error); 
						return (ret == DW_DLV_OK) ? ref : (Dwarf_Unsigned) -1;
					}
					default: assert(false);
				}
			}
			return (Dwarf_Unsigned)-1; // error
		}
		
		inline RangesList::handle_type 
		RangesList::try_construct(const Attribute& a)
		{
			Dwarf_Unsigned ranges_off = get_rangelist_offset(a);
			if (ranges_off != (Dwarf_Unsigned)-1)
			{
				Dwarf_Ranges *block_start;
				Dwarf_Signed count = 0;
				Dwarf_Unsigned bytes = 0;
				int ret2 = dwarf_get_ranges(a.get_dbg(), ranges_off, &block_start, &count, &bytes, &current_dwarf_error);
				if (ret2 == DW_DLV_OK)
				{
					assert(count > 0);
					/* Now what? handle_type is a unique_ptr<Dwarf_Locdesc*>
					 * pointing at an array of Dwarf_Locdesc*s . 
					 * We will make unique_ptrs out of each of them, but only when
					 * we upgrade this handle and copy the array. */
					return handle_type(block_start, deleter(a.get_dbg(), count));
				}
				else if (ret2 == DW_DLV_NO_ENTRY)
				{
					/* HACK: use (void*)-1 */
					return handle_type((raw_element_type*)-1, deleter(a.get_dbg(), 0));
				}
			}
			return handle_type(nullptr, deleter(nullptr, 0));
		}
		inline RangesList::handle_type 
		RangesList::try_construct(const Attribute& a, const Die& d)
		{
			Dwarf_Unsigned ranges_off = get_rangelist_offset(a);
			if (ranges_off != (Dwarf_Unsigned)-1)
			{
				Dwarf_Ranges *block_start;
				Dwarf_Signed count = 0;
				Dwarf_Unsigned bytes = 0;
				int ret2 = dwarf_get_ranges_a(a.get_dbg(), ranges_off, d.raw_handle(), 
					&block_start, &count, &bytes, &current_dwarf_error);
				if (ret2 == DW_DLV_OK)
				{
					assert(count > 0);
					/* Now what? handle_type is a unique_ptr<Dwarf_Locdesc*>
					 * pointing at an array of Dwarf_Locdesc*s . 
					 * We will make unique_ptrs out of each of them, but only when
					 * we upgrade this handle and copy the array. */
					return handle_type(block_start, deleter(a.get_dbg(), count));
				}
				else if (ret2 == DW_DLV_OK)
				{
					return handle_type((raw_element_type*)-1, deleter(a.get_dbg(), 0));
				}
			}
			
			return handle_type(nullptr, deleter(nullptr, 0));
		}
		inline Block::handle_type
		Block::try_construct(const Attribute& a)
		{
			Dwarf_Block *returned;
			int ret = dwarf_formblock(a.raw_handle(), &returned, &current_dwarf_error);
			if (ret == DW_DLV_OK)
			{
				return handle_type(returned, deleter(a.get_dbg()));
			} else return handle_type(nullptr, deleter(nullptr));
		}
		
		inline Block::Block(const Attribute& a) : handle(try_construct(a)) 
		{ 
			if (!handle) throw Error(current_dwarf_error, 0);
		}
		std::ostream& operator<<(std::ostream& s, const AttributeList& attrs);
		// inlines we couldn't define earlier -- declared in private/libdwarf-handles.hpp
		inline encap::attribute_map Die::copy_attrs() const
		{ return encap::attribute_map(AttributeList(*this), *this, get_constructing_root()); }

#endif /* LIBDW_SUPPORT_NOT_FINISHED */
	} /* end namespace core */
} /* end namespace dwarf */
#endif
