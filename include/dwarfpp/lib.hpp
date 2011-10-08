/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * lib.hpp: basic C++ wrapping of libdwarf C API.
 *
 * Copyright (c) 2008--9, Stephen Kell.
 */

#ifndef __DWARFPP_LIB_HPP
#define __DWARFPP_LIB_HPP

#include <iostream>
#include <stack>
#include <vector>
#include <cassert>
#include <boost/optional.hpp>
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
            //typedef ::Elf Elf_opaque_in_libdwarf;
		}
		
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
            
            friend class file_toplevel_die;
            friend class compile_unit_die;

			Dwarf_Debug dbg; // our peer structure
			Dwarf_Error last_error; // pointer to Dwarf_Error_s detailing our last error
			//dieset file_ds; // the structure to hold encapsulated DIEs, if we use it

			/*dwarf_elf_handle*/ Elf* elf;
			bool free_elf; // whether to do elf_end in destructor
            
            aranges *p_aranges;

			// public interfaces to these are to use constructor
			int siblingof(die& d, die *return_sib, Dwarf_Error *error = 0);
			int first_die(die *return_die, Dwarf_Error *error = 0); // special case of siblingof

		protected:
			// protected constructor
			file() {} // uninitialised default value, s.t. die can have a default value too	
            
			// we call out to a function like this when we hit a CU in reset_cu_context
			typedef void (*cu_callback_t)(void *arg, 
				Dwarf_Off cu_offset,
				Dwarf_Unsigned cu_header_length,
				Dwarf_Half version_stamp,
				Dwarf_Unsigned abbrev_offset,
				Dwarf_Half address_size,
				Dwarf_Unsigned next_cu_header);
				
            // libdwarf has a weird stateful API for getting compile unit DIEs.
            int reset_cu_context(cu_callback_t cb = 0, void *arg = 0);

			// TODO: forbid copying or assignment by adding private definitions 
		public:
			Dwarf_Debug get_dbg() { return dbg; }
			//dieset& get_ds() { return file_ds; }
			file(int fd, Dwarf_Unsigned access = DW_DLC_READ,
				Dwarf_Ptr errarg = 0,
				Dwarf_Handler errhand = default_error_handler,
				Dwarf_Error *error = 0);

			virtual ~file();

			/* Useful functions -- all trivial wrappers of libdwarf API functions */
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
			
			friend class basic_die;
			friend class file_toplevel_die;
			
			file& f;
			Dwarf_Error *const p_last_error;
			Dwarf_Die my_die;
			die(file& f, Dwarf_Die d, Dwarf_Error *perror);
			Dwarf_Die get_die() { return my_die; }

			// public interface is to use constructor
			int first_child(die *return_kid, Dwarf_Error *error = 0);
			
			// "uninitialized" DIE is used to back file_toplevel_die
			static file dummy_file;
			die() : f(dummy_file), p_last_error(0) {}

	public:
			virtual ~die();
			die(file& f) : f(f), p_last_error(&f.last_error)
				{ 	int retval = f.first_die(this, p_last_error);
					if (retval == DW_DLV_NO_ENTRY) throw No_entry();
					else if (retval == DW_DLV_ERROR) throw Error(*p_last_error, f.get_dbg()); }
			die(file& f, const die& d) : f(f), p_last_error(&f.last_error)
				{ 	int retval = f.siblingof(const_cast<die&>(d), this, p_last_error);
					if (retval == DW_DLV_NO_ENTRY) throw No_entry(); 
					else if (retval == DW_DLV_ERROR) throw Error(*p_last_error, f.get_dbg()); }
			// this is *not* a copy constructor! it constructs the child
			explicit die(const die& d) : f(const_cast<die&>(d).f), p_last_error(&f.last_error)
				{ 	int retval = const_cast<die&>(d).first_child(this, p_last_error); 
					if (retval == DW_DLV_NO_ENTRY) throw No_entry(); 
					else if (retval == DW_DLV_ERROR) throw Error(*p_last_error, f.get_dbg()); }
			die(file& f, Dwarf_Off off) : f(f), p_last_error(&f.last_error)
				{	assert (off != 0UL); // file_toplevel_die should use protected constructor
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
         * (it's all done when getting the attribute array). */
		class attribute {
			friend class die;
			friend class attribute_array;
			friend class block;
			friend class loclist;
            friend class ranges;
			attribute_array& a;
			int i;
		public:
			attribute(attribute_array& a, int i) : a(a), i(i) {}
			attribute(Dwarf_Half attr, attribute_array& a, Dwarf_Error *error = 0);
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

			const attribute_array& get_containing_array() const { return a; }
		};

		class attribute_array {
			friend class die;
			friend class attribute;
			friend class block;
			friend class loclist;
            friend class ranges;
			die& d;
			Dwarf_Error *const p_last_error;
			Dwarf_Attribute *p_attrs;
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

		class block {
			attribute attr; // it's okay to pass attrs by value
			Dwarf_Block *b;
		public:
			block(attribute a, Dwarf_Error *error = 0) : attr(a) // it's okay to pass attrs by value
			{
				if (error == 0) error = attr.a.p_last_error;
				int retval = a.formblock(&b);
				if (retval != DW_DLV_OK) throw Error(*error, attr.a.d.f.get_dbg());
			}

			Dwarf_Unsigned len() { return b->bl_len; }
			Dwarf_Ptr data() { return b->bl_data; }

			virtual ~block() { dwarf_dealloc(attr.a.d.f.get_dbg(), b, DW_DLA_BLOCK); }
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
            Dwarf_Error *const p_last_error;
            Dwarf_Arange *p_aranges;
            Dwarf_Signed cnt;
            // TODO: forbid copying or assignment
        public:
        	aranges(file& f, Dwarf_Error *error = 0) : f(f), p_last_error(error ? error : &f.last_error)
            {
            	if (error == 0) error = p_last_error;
                int retval = dwarf_get_aranges(f.get_dbg(), &p_aranges, &cnt, error);
                if (retval == DW_DLV_NO_ENTRY) { cnt = 0; p_aranges = 0; return; }
                else if (retval != DW_DLV_OK) throw Error(*error, f.get_dbg());
            }
			Dwarf_Signed count() { return cnt; }		
			int get_info(int i, Dwarf_Addr *start, Dwarf_Unsigned *length, Dwarf_Off *cu_die_offset,
				Dwarf_Error *error = 0);
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
            : d(a.a.d), p_last_error(error ? error : &d.f.last_error)
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
			    if (error == 0) error = attr.a.p_last_error;
			    int retval = a.loclist_n(&locs, &locs_len);
			    if (retval == DW_DLV_NO_ENTRY)
			    {
				    // this means two things -- locs_len is zero, and nothing is allocated
				    locs = 0;
				    locs_len = 0;
			    }
			    if (retval == DW_DLV_ERROR) throw Error(*error, attr.a.d.f.get_dbg());
		    }

		    Dwarf_Signed len() const { return locs_len; }
		    const Dwarf_Locdesc& operator[] (size_t off) const { return *locs[off]; }

		    virtual ~loclist()
		    { 
			    for (int i = 0; i < locs_len; ++i) 
			    {
				    dwarf_dealloc(attr.a.d.f.get_dbg(), locs[i]->ld_s, DW_DLA_LOC_BLOCK);
				    dwarf_dealloc(attr.a.d.f.get_dbg(), locs[i], DW_DLA_LOCDESC);
			    }
			    if (locs != 0) dwarf_dealloc(attr.a.d.f.get_dbg(), locs, DW_DLA_LIST);
		    }
	    };
	    ostream& operator<<(std::ostream& s, const loclist& ll);	

		bool operator==(const Dwarf_Loc& e1, const Dwarf_Loc& e2);
		bool operator!=(const Dwarf_Loc& e1, const Dwarf_Loc& e2);
	} // end namespace lib
} // end namespace dwarf

#endif
