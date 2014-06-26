		
		/* FIXME: clean up Errors properly. It's complicated. A Dwarf_Error is a handle
		 * that needs to be dwarf_dealloc'd, but there are two exceptions:
		 * errors returned by dwarf_init() and dwarf_elf_init() need to be free()d. 
		 * In other words, these errors need different deleters. 
		 * We should unique_ptr'ify each Dwarf_Error at the point where it arises,
		 * so that we can specify this alternate handling. */
		// typedef struct Dwarf_Error_s*      Dwarf_Error;
		void exception_error_handler(Dwarf_Error error, Dwarf_Ptr errarg);

		/* What follows is a fairly mechanical translation of libdwarf,
		 * plus destruction logic from the docs. */
		typedef struct Dwarf_Debug_s*      Dwarf_Debug; // pasted from libdwarf.h
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
			Debug() : handle(nullptr) {}
			
			raw_handle_type raw_handle()       { return handle.get(); }
			raw_handle_type raw_handle() const { return handle.get(); }
		};

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
		typedef struct Dwarf_Die_s*        Dwarf_Die;
		struct Die : /*private*/ virtual abstract_die // remind me: why is this private?
		{
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
			Debug::raw_handle_type get_dbg() const { return handle.get_deleter().dbg; }
			root_die& get_constructing_root() const 
			{ return *handle.get_deleter().p_constructing_root; }
			
			// to avoid making exception handling compulsory, 
			// we provide static "maybe" constructor functions (defined in lib.hpp)...
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
			
			// ... and a "nullptr" constructor
			inline Die(std::nullptr_t n, root_die *p_r) : handle(nullptr, deleter(nullptr, *p_r)) {} 
			
			// ... then the "normal" constructors, that throw exceptions on failure
			inline Die(root_die& r, const iterator_base& die); /* siblingof */
			inline explicit Die(root_die& r); /* siblingof in the root case */
			inline explicit Die(const iterator_base& die); /* child */
			inline explicit Die(root_die& r, Dwarf_Off off); /* offdie */
			
			// move constructor
			inline Die(Die&& d) : handle(std::move(d.handle)) {}
			// move assignment
			inline Die& operator=(Die&& d) { handle = std::move(d.handle); return *this; }

			raw_handle_type raw_handle()       { return handle.get(); }
			raw_handle_type raw_handle() const { return handle.get(); }
			
			// libdwarf methods
			Dwarf_Off offset_here() const;
			Dwarf_Half tag_here() const;
			std::unique_ptr<const char, string_deleter> name_here() const;
			Dwarf_Off enclosing_cu_offset_here() const;
			bool has_attr_here(Dwarf_Half attr) const;
			bool has_attribute_here(Dwarf_Half attr) const { return has_attr_here(attr); }
			inline spec& spec_here(root_die& r) const;
			
			// for convenience, this one is public -- basic_die's subclasses call it
			// (whereas the rest of our abstract_die implementation is private)
			inline encap::attribute_map copy_attrs(opt<root_die&> opt_r) const;

			friend class iterator_base;
		//private: 
			/* implement the abstract_die interface, but privately -- WHY? */
			inline Dwarf_Off get_offset() const { return offset_here(); }
			inline Dwarf_Half get_tag() const { return tag_here(); }
			inline opt<string> get_name() const 
			{ return name_here() ? opt<string>(string(name_here().get())) : opt<string>(); }
			inline unique_ptr<const char, string_deleter> get_raw_name() const
			{ return name_here(); }
			inline Dwarf_Off get_enclosing_cu_offset() const 
			{ return enclosing_cu_offset_here(); }
			inline bool has_attr(Dwarf_Half attr) const { return has_attr_here(attr); }
			// inline encap::attribute_map copy_attrs(root_die& r) const; // -- declared above
			inline spec& get_spec(root_die& r) const { return spec_here(r); }
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
			Dwarf_Debug get_dbg() const { return handle.get_deleter().dbg; }

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
		struct Locdesc
		{
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
			
			handle_type handle;
			Dwarf_Debug get_dbg() const { return handle.get_deleter().dbg; }

			/* LocdescList can create individual Locdescs in a list. */
			inline Locdesc(handle_type h) : handle(std::move(h)) {}
			/* Individual Locdescs can be constructed too. */
			static inline handle_type 
			try_construct(const Attribute& a);
			static inline handle_type 
			try_construct(Dwarf_Debug dbg, Dwarf_Ptr bytes_in, Dwarf_Unsigned bytes_len);
			
			raw_handle_type raw_handle()       { return handle.get(); }
			raw_handle_type raw_handle() const { return handle.get(); }
		};
		typedef struct Dwarf_Attribute_s*  Dwarf_Attribute;

		/* Block is special because it doesn't have an opaque type. */
		struct Block
		{ 
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
			handle_type handle; 
			static inline handle_type 
			try_construct(const Attribute& a); 
			inline explicit Block(const Attribute& a); 
			inline Block(handle_type h) : handle(std::move(h)) { /* "upgrade" constructor */ 
				if (!handle) throw Error(current_dwarf_error, 0); 
			} 
			raw_handle_type raw_handle()       { return handle.get(); } 
			raw_handle_type raw_handle() const { return handle.get(); } 
			Debug::raw_handle_type get_dbg() const { return handle.get_deleter().dbg; } 
		};
			
		/* Ranges is special: we never get a single range, only a list, 
		 * and we can never deallocate a single range. So there's no "handle"
		 * on a Range, so we don't bother with a class for it. */
		
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
		
		/* Ideally we would in-place construct a unique_ptr array over the
		 * actual returned array. Then, to use this, the client would std::move
		 * elements out of the array. This allows individual attrs to be used
		 * and deallocated early, like the C style. NOTE that it only works
		 * if unique_ptrs are the same size/rep as normal ptrs, which means
		 * no deleter state. That is a problem, because our deleters need
		 * a reference to the dbg. NOTE that these are the individual-element
		 * deleters, so they do not store the block length. Anyway, for now, 
		 * we just copy the array of pointers into a new unique_ptr array,
		 * and let the clients use that. */
		struct AttributeList
		{
			typedef Dwarf_Attribute *raw_handle_type; /* What libdwarf returns us. */
			typedef Dwarf_Attribute raw_element_type;
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
			  
			vector<Attribute> copied_list; // see note in destructor
			Dwarf_Debug get_dbg() const { return handle.get_deleter().dbg; }
			// IMPORTANT: this copied_list must come *after* the handle in the 
			// field order, because it must be destructed *first*. We want to
			// delete the individual Attributes, using the unique_ptr destructor,
			// then delete the whole list using our whole-list deleter.

			Dwarf_Signed get_len() const { return handle.get_deleter().len; }
			Attribute& operator[](Dwarf_Signed i) { return copied_list.at(i); }
			Attribute const& operator[](Dwarf_Signed i) const 
			{ return copied_list.at(i); }

			// can't define msot of these now, because iterator_base is currently incomplete
			static inline handle_type
			try_construct(const Die& it);
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
			inline explicit AttributeList(const Die& it);
			inline AttributeList(handle_type h, const Die& d) : handle(std::move(h)), d(d) /* "upgrade" constructor */ 
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

#define list_handle(Fragment, ConstructorArgs...) \
		struct Fragment ## List \
		{ \
			typedef Fragment::raw_handle_type *raw_handle_type; /* What libdwarf returns us. */ \
			/* don't say opaque_type... */ \
			/* typedef Fragment::opaque_type *raw_element_type; */ \
			/* ... because LocDesc doesn't have one. But it does have... */ \
			typedef Fragment::raw_handle_type raw_element_type; \
			typedef Fragment::handle_type copied_element_type; \
			/* This is a whole-list deleter. */ \
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

/* These guys need *two* dwarf_dealloc calls. */

/* These are fairly normal singleton things */
#define DEALLOC_TOKEN_Error     DW_DLA_ERROR
#define DEALLOC_TOKEN_Abbrev    DW_DLA_ABBREV

/* These guys are grabbed by the same call, dwarf_get_fde_list, but are deprecated 
 * in libdwarf because the relevant API is leaky. There's a separate dwarf_fde_cie_list_dealloc
 * call instead, so we don't need these. */
// #define DEALLOC_TOKEN_Fde       DW_DLA_FDE
// #define DEALLOC_TOKEN_Cie       DW_DLA_CIE

		/* Do the basic handles -- commenting out the SGI-specific ones for now */
		basic_handle(Line, const iterator_base& it) /* dwarf_srclines -- allocates a block; free each line, free block */
		//basic_handle(Func) /* dwarf_get_funcs */ // SGI-specific
		//basic_handle(Type) /* dwarf_get_types */ // SGI-specific
		//basic_handle(Var) /* dwarf_get_vars */ // SGI-specific
		//basic_handle(Weak) /* dwarf_get_weaks */ // SGI-specific
		basic_handle(Arange) /* dwarf_get_arange and dwarf_get_aranges */
		basic_handle(Global) /* dwarf_get_globals */
		//basic_handle(Error) /* can be created by most libdwarf calls */ // FIXME: reinstate
		//basic_handle(Abbrev) /* abbrevs are abstracted away by libdwarf -- we can ignore */

		/* Do the list handles. */
		list_handle(Locdesc, const Attribute& a)
		list_handle(Line, const iterator_base& it)
		list_handle(Arange)
		list_handle(Global)
		
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
		
		// inlines we couldn't define earlier
		inline encap::attribute_map Die::copy_attrs(opt<root_die&> opt_r) const
		{ return encap::attribute_map(AttributeList(*this), *this, *opt_r/*, s*/); }
