/* Minimal C++ binding for a useful subset of libdwarf.
 * 
 * Copyright (c) 2008, Stephen Kell.
 */

#ifndef __DWARFPP_H
#define __DWARFPP_H

extern "C" {
#include <dwarf.h>
#include <libdwarf.h>
#include <libelf.h>
}
#include <map>
#include <vector>
#include <stack>
#include <string>
#include <memory>
#include <iostream>
#include <cassert>

namespace dwarf {

	// global tables of DWARF-defined symbolic constants
	extern std::map<const char *, Dwarf_Half> tag_forward_map;
	extern std::map<Dwarf_Half, const char *> tag_inverse_map;

	extern std::map<const char *, Dwarf_Half> form_forward_map;
	extern std::map<Dwarf_Half, const char *> form_inverse_map;

	extern std::map<const char *, Dwarf_Half> attr_forward_map;
	extern std::map<Dwarf_Half, const char *> attr_inverse_map;

	extern std::map<const char *, Dwarf_Half> encoding_forward_map;
	extern std::map<Dwarf_Half, const char *> encoding_inverse_map;
	
	extern std::map<const char *, Dwarf_Half> op_forward_map;
	extern std::map<Dwarf_Half, const char *> op_inverse_map;
	
	extern std::map<Dwarf_Half, Dwarf_Half *> op_operand_forms_tbl;

	// basic predicates defining useful groupings of DWARF entries
	bool tag_is_type(Dwarf_Half tag);
	bool tag_has_named_children(Dwarf_Half tag);
	bool attr_describes_location(Dwarf_Half attr);
	
	// basic functions over DWARF operations
	Dwarf_Unsigned op_operand_count(Dwarf_Half op);
		
	class file;
	class die;
	class attribute;
	class attribute_array;
	class global_array;
	class global;
	class block;
	class loclist;
	class evaluator;
	namespace encap {
		class die;
		class attribute;
		class attribute_value;
		struct expr;
		struct loclist;
	}
	
	// basic datatypes for dealing with encap data
	class dieset : public std::map<Dwarf_Off, dwarf::encap::die>
	{
	public:
		typedef std::pair<Dwarf_Off, Dwarf_Half> backref_rec;
		typedef std::vector<backref_rec> backref_list;
	private:
		std::map<Dwarf_Off, backref_list> m_backrefs;
	public:
		std::map<Dwarf_Off, backref_list>& backrefs() { return m_backrefs; }
	};
	typedef std::vector<Dwarf_Off> die_off_list;

	//extern const char *tag_lookup_tbl[];
	//extern const char *attr_lookup_tbl[];
	//extern const char *form_lookup_tbl[];
	//extern const char *encoding_lookup_tbl[];
	const char *tag_lookup(Dwarf_Half tag);
	const char *attr_lookup(Dwarf_Half attr);
	const char *form_lookup(Dwarf_Half form);
	const char *encoding_lookup(Dwarf_Half encoding);
	const char *op_lookup(Dwarf_Half op);
	
	class interp
	{
		interp() {} // non-instantiable
	public:
		enum interp_class
		{
			EOL = 0,
			reference,
			block,
			loclistptr,
			string,
			constant,
			lineptr,
			address,
			flag,
			macptr,
			rangelistptr,
			block_as_dwarf_expr
		};
	};
	
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
		Dwarf_Debug dbg; // our peer structure
		Dwarf_Error last_error; // pointer to Dwarf_Error_s detailing our last error
		dieset file_ds; // the structure to hold encapsulated DIEs, if we use it

		// public interfaces to these are to use constructor
		int siblingof(die& d, die *return_sib, Dwarf_Error *error = 0);
		int first_die(die *return_die, Dwarf_Error *error = 0); // special case of siblingof

		// private constructor
		file() {} // uninitialised default value, s.t. die can have a default value too	

		// TODO: forbid copying or assignment by adding private definitions 
	public:
		Dwarf_Debug get_dbg() { return dbg; }
		dieset& get_ds() { return file_ds; }
		file(int fd, Dwarf_Unsigned access = DW_DLC_READ,
			Dwarf_Ptr errarg = 0,
			Dwarf_Handler errhand = default_error_handler,
			Dwarf_Error *error = 0);
			
		virtual ~file();
		static file& default_file()
		{
			static file *pointer_to_default = 0;
			if (pointer_to_default == 0) pointer_to_default = new file();
			return *pointer_to_default;
		}
		
		/* Useful functions -- all trivial wrappers of libdwarf API functions */
		int next_cu_header(
			Dwarf_Unsigned *cu_header_length,
			Dwarf_Half *version_stamp,
			Dwarf_Unsigned *abbrev_offset,
			Dwarf_Half *address_size,
			Dwarf_Unsigned *next_cu_header,
			Dwarf_Error *error = 0);
						
		int offdie(Dwarf_Off offset, die *return_die, Dwarf_Error *error = 0);
		
//		int get_globals(
//			global_array *& globarr, // on success, globarr points to a global_array
//			Dwarf_Error *error = 0);
	
		int get_cu_die_offset_given_cu_header_offset(
			Dwarf_Off in_cu_header_offset,
			Dwarf_Off * out_cu_die_offset,
			Dwarf_Error *error = 0);
	};
	
	class die {
		friend class file;
		friend class attribute_array;
		friend class dwarf::encap::die;
		friend class block;
		friend class loclist;
		file& f;
		Dwarf_Error *const p_last_error;
		Dwarf_Die my_die;
		die(file& f, Dwarf_Die d, Dwarf_Error *perror);
		Dwarf_Die get_die() { return my_die; }
				
		// public interface is to use constructor
		int first_child(die *return_kid, Dwarf_Error *error = 0);
		
		// TODO: forbid copying or assignment by adding private definitions here
		
	public:
		virtual ~die();
		die(file& f) : f(f), p_last_error(&f.last_error)
			{ 	int retval = f.first_die(this, p_last_error);
				if (retval == DW_DLV_NO_ENTRY) throw No_entry();
				else if (retval == DW_DLV_ERROR) throw Error(*p_last_error, f.get_dbg()); }
		die(file& f, die& d) : f(f), p_last_error(&f.last_error)
			{ 	int retval = f.siblingof(d, this, p_last_error);
				if (retval == DW_DLV_NO_ENTRY) throw No_entry(); 
				else if (retval == DW_DLV_ERROR) throw Error(*p_last_error, f.get_dbg()); }
		explicit die(die& d) : f(d.f), p_last_error(&f.last_error)
			{ 	int retval = d.first_child(this, p_last_error); 
				if (retval == DW_DLV_NO_ENTRY) throw No_entry(); 
				else if (retval == DW_DLV_ERROR) throw Error(*p_last_error, f.get_dbg()); }
		die(file& f, Dwarf_Off off) : f(f), p_last_error(&f.last_error)
			{	int retval = f.offdie(off, this, p_last_error);
				if (retval == DW_DLV_ERROR) throw Error(*p_last_error, f.get_dbg()); }			
				
		int tag(Dwarf_Half *tagval,	Dwarf_Error *error = 0);			
		int offset(Dwarf_Off * return_offset, Dwarf_Error *error = 0) const;
		int CU_offset(Dwarf_Off *return_offset, Dwarf_Error *error = 0);
		int name(char ** return_name, Dwarf_Error *error = 0);
		//int attrlist(attribute_array *& attrbuf, Dwarf_Error *error = 0);
		int hasattr(Dwarf_Half attr, Dwarf_Bool *return_bool, Dwarf_Error *error = 0);
		//int attr(Dwarf_Half attr, attribute *return_attr, Dwarf_Error *error = 0);
		int lowpc(Dwarf_Addr * return_lowpc, Dwarf_Error * error = 0);
		int highpc(Dwarf_Addr * return_highpc, Dwarf_Error *error = 0);
		int bytesize(Dwarf_Unsigned *return_size, Dwarf_Error *error = 0);
		int bitsize(Dwarf_Unsigned *return_size, Dwarf_Error *error = 0);
		int bitoffset(Dwarf_Unsigned *return_size, Dwarf_Error *error = 0);
		int srclang(Dwarf_Unsigned *return_lang, Dwarf_Error *error = 0);
		int arrayorder(Dwarf_Unsigned *return_order, Dwarf_Error *error = 0);
	};

	class attribute {
		friend class die;
		friend class attribute_array;
		friend class block;
		friend class loclist;
		attribute_array& a;
		int i;
	public:
		attribute(attribute_array& a, int i) : a(a), i(i) {}
		virtual ~attribute() {}	
		
		public:
		int hasform(Dwarf_Half form, Dwarf_Bool *return_hasform, Dwarf_Error *error = 0);
		int whatform(Dwarf_Half *return_form, Dwarf_Error *error = 0);
		int whatform_direct(Dwarf_Half *return_form, Dwarf_Error *error = 0);
		int whatattr(Dwarf_Half *return_attr, Dwarf_Error *error = 0);
		int formref(Dwarf_Off *return_offset, Dwarf_Error *error = 0);
		int formref_global(Dwarf_Off *return_offset, Dwarf_Error *error = 0);
		int formaddr(Dwarf_Addr * return_addr, Dwarf_Error *error = 0);
		int formflag(Dwarf_Bool * return_bool, Dwarf_Error *error = 0);
		int formudata(Dwarf_Unsigned * return_uvalue, Dwarf_Error * error = 0);
		int formsdata(Dwarf_Signed * return_svalue, Dwarf_Error *error = 0);
		int formblock(Dwarf_Block ** return_block, Dwarf_Error * error = 0);
		int formstring(char ** return_string, Dwarf_Error *error = 0);
		int loclist_n(Dwarf_Locdesc ***llbuf, Dwarf_Signed *listlen, Dwarf_Error *error = 0);
		int loclist(Dwarf_Locdesc **llbuf, Dwarf_Signed *listlen, Dwarf_Error *error = 0);
		
		const attribute_array& get_containing_array() const { return a; }
	};

	class attribute_array {
		friend class die;
		friend class attribute;
		friend class block;
		friend class loclist;
		die& d;
		Dwarf_Error *const p_last_error;
		Dwarf_Attribute *p_attrs;
		Dwarf_Debug dbg;
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
			else if (retval != DW_DLV_OK) throw Error(*error, d.f.get_dbg());
		}
		Dwarf_Signed count() { return cnt; }
		attribute get(int i) { return attribute(*this, i); }
		virtual ~attribute_array()	{
			for (int i = 0; i < cnt; i++)
			{
				dwarf_dealloc(dbg, p_attrs[i], DW_DLA_ATTR);
			}
			if (p_attrs != 0) dwarf_dealloc(dbg, p_attrs, DW_DLA_LIST);
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
}
std::ostream& operator<<(std::ostream& s, const Dwarf_Locdesc& ld);	
std::ostream& operator<<(std::ostream& s, const Dwarf_Loc& l);
namespace dwarf {	
	class loclist {
		attribute attr;
		Dwarf_Locdesc **locs;
		Dwarf_Signed locs_len;
		friend std::ostream& operator<<(std::ostream&, const loclist&);
		friend struct encap::loclist;
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
	std::ostream& operator<<(std::ostream& s, const loclist& ll);	
	namespace encap { typedef Dwarf_Loc expr_instr; }
}
bool operator==(const dwarf::encap::expr_instr& e1, const dwarf::encap::expr_instr& e2);
bool operator!=(const dwarf::encap::expr_instr& e1, const dwarf::encap::expr_instr& e2);
namespace dwarf {
	namespace encap {
		//typedef Dwarf_Loc expr_instr;
		typedef struct expr
		{
			Dwarf_Addr hipc;
			Dwarf_Addr lopc;
			std::vector<expr_instr> m_expr;
			expr() : hipc(0), lopc(0) {}
			expr(const Dwarf_Locdesc& desc) : hipc(desc.ld_hipc), lopc(desc.ld_lopc),
				m_expr(desc.ld_s, desc.ld_s + desc.ld_cents) {}
			
			// this is languishing here because it's a HACK.. should take the value as argument
			// too, to calculate variable-length encodings correctly
			size_t form_encoded_size(Dwarf_Half form)
			{
				switch(form)
				{
					case DW_FORM_addr: return sizeof (Dwarf_Addr); 
					case DW_FORM_block2: return 2;
					case DW_FORM_block4: return 4;
					case DW_FORM_data2: return 2;
					case DW_FORM_data4: return 4;
					case DW_FORM_data8: return 8;
					case DW_FORM_string: return sizeof (Dwarf_Unsigned);
					case DW_FORM_block: return sizeof (Dwarf_Unsigned);
					case DW_FORM_block1: return 1;
					case DW_FORM_data1: return 1;
					case DW_FORM_flag: return 1;
					case DW_FORM_sdata: return sizeof (Dwarf_Unsigned);
					case DW_FORM_strp: return sizeof (Dwarf_Addr);
					case DW_FORM_udata: return sizeof (Dwarf_Unsigned);
					case DW_FORM_ref_addr: return sizeof (Dwarf_Addr);
					case DW_FORM_ref1: return 1;
					case DW_FORM_ref2: return 2;
					case DW_FORM_ref4: return 4;
					case DW_FORM_ref8: return 8;
					case DW_FORM_ref_udata: return sizeof (Dwarf_Unsigned);
					case DW_FORM_indirect: return sizeof (Dwarf_Addr);
					default: assert(false); return 0;					
				}
			}
			
			template <class In> expr(In first, In last) : m_expr(first, last), hipc(0), lopc(0) {}
			/* This template parses a location expression out of an array of unsigneds. */
			template<size_t s> expr(Dwarf_Unsigned (&arr)[s]) : hipc(0), lopc(0)
			{
				Dwarf_Unsigned *iter = &arr[0];
				Dwarf_Unsigned next_offset = 0U;
				while (iter < arr + s)
				{
					Dwarf_Loc loc;
					loc.lr_offset = next_offset;
					loc.lr_atom = *iter++; // read opcode
					next_offset += 1; // opcodes are one byte
					switch (op_operand_count(loc.lr_atom))
					{
						case 2:
							loc.lr_number = *iter++;
							loc.lr_number2 = *iter++;
							// how many bytes of DWARF binary encoding?
							next_offset += form_encoded_size(
								dwarf::op_operand_forms_tbl[loc.lr_atom][0]
							);
							next_offset += form_encoded_size(
								dwarf::op_operand_forms_tbl[loc.lr_atom][1]
							);						
							break;
						case 1:
							loc.lr_number = *iter++;
							// how many bytes of DWARF binary encoding?
							next_offset += form_encoded_size(
								dwarf::op_operand_forms_tbl[loc.lr_atom][0]
							);
							break;
						case 0:
							break;
						default: assert(false);
					}
					m_expr.push_back(loc);
				}
			}
			bool operator==(const expr& e) const 
			{ 
				//expr_instr e1; expr_instr e2;
				return hipc == e.hipc &&
					lopc == e.lopc &&
					//e1 == e2;
					m_expr == e.m_expr;
			}
			bool operator!=(const expr& e) const { return !(*this == e); }
			friend std::ostream& operator<<(std::ostream& s, const expr& e);
		} loc_expr;
		//typedef expr loc_expr;
		//typedef std::vector<loc_expr> loc_list;
		std::ostream& operator<<(std::ostream& s, const expr& e);
		
		struct loclist : public std::vector<loc_expr>
		{
			friend class ::dwarf::evaluator;
			friend class attribute_value;
			loclist(const dwarf::loclist& dll)
			{
				for (int i = 0; i != dll.len(); i++)
				{
					push_back(loc_expr(dll[i])); // construct new vector
				}		
			}
			// would ideally repeat all vector constructors
			template <class In> loclist(In first, In last) : std::vector<loc_expr>(first, last) {}
			loclist(const loc_expr& loc) : std::vector<loc_expr>(1, loc) {}
			//bool operator==(const loclist& oll) const { return *this == oll; }
			//bool operator!=(const loclist& oll) const { return !(*this == oll); }
			//friend std::ostream& operator<<(std::ostream& s, const ::dwarf::encap::loclist& ll);	
		};
		std::ostream& operator<<(std::ostream& s, const ::dwarf::encap::loclist& ll);	
			
// 		/* We record attributes so as to remember backreferences. */
// 		class attr_set
// 		{
// 			std::map<Dwarf_Half, attribute_value> m_map;
// 		
// 		public:
// 			typedef Dwarf_Half key_type;
// 			typedef attribute_value mapped_type;
// 			typedef std::pair<Dwarf_Half, attribute_value> value_type;
// 			typedef std::map<Dwarf_Half, attribute_value>::key_compare key_compare;
// 			//typedef std::map<Dwarf_Half, attribute_value>::A allocator_type;
// 			typedef std::map<Dwarf_Half, attribute_value>::A::reference reference;
// 			typedef std::map<Dwarf_Half, attribute_value>::A::const_reference const_reference;
// 			typedef std::map<Dwarf_Half, attribute_value>::iterator iterator;
// 			typedef std::map<Dwarf_Half, attribute_value>::const_iterator const_iterator;
// 			typedef std::map<Dwarf_Half, attribute_value>::size_type size_type;
// 			typedef std::map<Dwarf_Half, attribute_value>::difference_type difference_type;
// 			typedef std::map<Dwarf_Half, attribute_value>::reverse_iterator reverse_iterator;
// 			typedef std::map<Dwarf_Half, attribute_value>::const_reverse_iterator const_reverse_iterator;
// 			
// 			iterator begin() { return m_map.begin(); }
// 			const_iterator begin() const { return m_map.begin(); }
// 			iterator end() { return m_map.end() ; }
// 			const_iterator end() const { return m_map.end(); }
// 			reverse_iterator rbegin() { return m_map.rbegin(); }
// 			const_reverse_iterator rbegin() const { return m_map.rbegin(); }
// 			reverse_iterator rend() { return m_map.rend(); }
// 			const_reverse_iterator rend() const { return m_map.rend(); }
// 			mapped_type& operator[] (const key_type& k) { return m_map[k]; }
// 			
// 		};

		class attribute_value {
			friend class die;
		public: 
			struct ref { 
				friend class attribute_value;
				Dwarf_Off off; bool abs;
			private:
				dieset& ds;
				Dwarf_Off referencing_off; // off of DIE keeping the reference
				Dwarf_Half referencing_attr; // attr# of attribute holding the reference
			public:
				ref(dieset& ds, Dwarf_Off off, bool abs, 
					Dwarf_Off referencing_off, Dwarf_Half referencing_attr);					
				virtual ~ref();
				ref(const ref& r); // copy constructor
			};
			static const attribute_value& DOES_NOT_EXIST() {
				if (dne_val == 0) dne_val = new attribute_value(); // FIXME: delete this anywhere?
				return *dne_val;
			}
			enum form { NO_ATTR, ADDR, FLAG, UNSIGNED, SIGNED, BLOCK, STRING, REF, LOCLIST }; // TODO: complete?
			form get_form() const { return f; }
		private:
			Dwarf_Half orig_form;
			form f; // discriminant			
			union {
				Dwarf_Bool v_flag;
				Dwarf_Unsigned v_u;
				Dwarf_Signed v_s;
				Dwarf_Addr v_addr;
				//std::vector<unsigned char> *v_block; // TODO: make resource-managing
				//std::string *v_str; // TODO: make resource-managing
				//ref *v_ref;
			//};
			// HACK: we can't include these in the union, it seems
			// TODO: instead of allocating them here, use new (here) and delete (in destructor)
				std::vector<unsigned char> *v_block;
				std::string *v_string;
				ref *v_ref;
				loclist *v_loclist;
			};
			// this constructor is a HACK to re-use formatting logic when printing Dwarf_Locs
			// -- the operator<< is a friend
			friend std::ostream& ::operator<<(std::ostream& s, const ::Dwarf_Loc& l);
			attribute_value(Dwarf_Unsigned data, Dwarf_Half o_form) 
				: orig_form(o_form), f(dwarf_form_to_form(o_form)), v_u(data) {} 
			static const form dwarf_form_to_form(const Dwarf_Half form); // helper hack

			attribute_value() : orig_form(0), f(NO_ATTR) { v_u = 0U; } // FIXME: this zero value can still be harmful when clients do get_ on wrong type
				// ideally the return values of get_() methods should return some Option-style type,
				// which I think boost provides... i.e. Some of value | None
			static const attribute_value *dne_val;
			
		public:
			attribute_value(dieset& ds, dwarf::attribute& a);
// 			//attribute_value() {} // allow uninitialised temporaries
 			attribute_value(Dwarf_Bool b) : orig_form(DW_FORM_flag), f(FLAG), v_flag(b) {}
// 			// HACK to allow overload resolution: addr is ignored
// 			attribute_value(void *addr, Dwarf_Addr value) : f(ADDR), v_addr(value) {}		
 			attribute_value(Dwarf_Unsigned u) : orig_form(DW_FORM_udata), f(UNSIGNED), v_u(u) {}				
 			attribute_value(Dwarf_Signed s) : orig_form(DW_FORM_sdata), f(SIGNED), v_s(s) {}			
// 			attribute_value(dwarf::block& b) : f(BLOCK), v_block(new std::vector<unsigned char>(
// 					(unsigned char *) b.data(), ((unsigned char *) b.data()) + b.len())) 
// 					{ /*std::cerr << "Constructed a block attribute_value with vector at 0x" << std::hex << (unsigned) v_block << std::dec << std::endl;*/ }			
 			attribute_value(const char *s) : orig_form(DW_FORM_string), f(STRING), v_string(new std::string(s)) {}
 			attribute_value(const std::string& s) : orig_form(DW_FORM_string), f(STRING), v_string(new std::string(s)) {}				
// 			attribute_value(Dwarf_Off off, bool abs) : f(REF), v_ref(new ref(off, abs)) {}
 			attribute_value(ref r) : orig_form(DW_FORM_ref_addr), f(REF), v_ref(new ref(r.ds, r.off, r.abs, r.referencing_off, r.referencing_attr)) {}
			attribute_value(const loclist& l) : orig_form(DW_FORM_block), f(LOCLIST), v_loclist(new loclist(l)) {}
			
			Dwarf_Bool get_flag() const { assert(f == FLAG); return v_flag; }
			Dwarf_Unsigned get_unsigned() const { assert(f == UNSIGNED); return v_u; }
			Dwarf_Signed get_signed() const { assert(f == SIGNED); return v_s; }
			const std::vector<unsigned char> *get_block() const { assert(f == BLOCK); return v_block; }
			const std::string& get_string() const { assert(f == STRING); return *v_string; }
			ref& get_ref() const { assert(f == REF); return *v_ref; }
			const loclist& get_loclist() const { assert(f == LOCLIST); return *v_loclist; }
			
			bool operator==(const attribute_value& v) const { 
				if (this->f != v.f) return false;
				// else this->f == v.f
				switch (f)
				{
					case NO_ATTR:
						return true;
					case FLAG:
						return this->v_flag == v.v_flag;
					case UNSIGNED:
						return this->v_u == v.v_u;
					case SIGNED:
						return this->v_s == v.v_s;
					case BLOCK:
						return this->v_block == v.v_block;
					case STRING:
						return *(this->v_string) == *(v.v_string);
					case REF:
						return this->v_ref == v.v_ref;
					case ADDR:
						return this->v_addr == v.v_addr;
					case LOCLIST:
						return *(this->v_loclist) == *(v.v_loclist);
					default: 
						std::cerr << "Warning: comparing a dwarf::encap::attribute_value of unknown form " << v.f << std::endl;
						return false;
				} // end switch
			}
			
			bool operator!=(const attribute_value &v) const { return !(*this == v); }
			
			void print_raw(std::ostream& s) const;
			void print_as(std::ostream& s, interp::interp_class cls) const;
			friend std::ostream& operator<<(std::ostream& s, const attribute_value v);
			friend std::ostream& operator<<(std::ostream& s, std::pair<const Dwarf_Half, attribute_value>&);
			friend interp::interp_class guess_interp(const Dwarf_Half attr, const attribute_value& v);
				
			// copy constructor
			attribute_value(const attribute_value& av) : f(av.f)
			{
				this->orig_form = av.orig_form;
				switch (f)
				{
					case FLAG:
						v_flag = av.v_flag;
					break;
					case UNSIGNED:
						v_u = av.v_u;
					break;
					case SIGNED:
						v_s = av.v_s;
					break;
					case BLOCK:
						//std::cerr << "Copy constructing a block attribute value from vector at 0x" << std::hex << (unsigned) v_block << std::dec << std::endl;
						v_block = new std::vector<unsigned char>(*av.v_block);
						//std::cerr << "New block is at " << std::hex << (unsigned) v_block << std::dec << std::endl;						
					break;
					case STRING:
						//std::cerr << "Copy constructing a string attribute value from string at 0x" << std::hex << (unsigned) v_string << std::dec << std::endl;
						v_string = new std::string(*av.v_string);
						//std::cerr << "New string is at " << std::hex << (unsigned) v_string << std::dec << std::endl;
					break;
					case REF:
						v_ref = new ref(av.v_ref->ds, av.v_ref->off, av.v_ref->abs,
							av.v_ref->referencing_off, av.v_ref->referencing_attr);
					break;
					case ADDR:
						v_addr = av.v_addr;
					break;
					case LOCLIST:
						v_loclist = new loclist(*av.v_loclist);
					break;
					default: 
						std::cerr << "Warning: copy-constructing a dwarf::encap::attribute_value of unknown form " << f << std::endl;
						break;
				} // end switch				
			}
			
			virtual ~attribute_value() {
				switch (f)
				{
					case FLAG:
					case UNSIGNED:
					case SIGNED:
					case ADDR:
						// nothing allocated
					break;
					case BLOCK:
						//std::cerr << "Destructing a block attribute_value with vector at 0x" << std::hex << (unsigned) v_block << std::dec << std::endl;
						delete v_block;
					break;
					case STRING:
						delete v_string;
					break;
					case REF:
						delete v_ref;
					break;
					case LOCLIST:
						delete v_loclist;
					break;
					default: break;
				} // end switch
			} // end ~attribute_value
		}; // end class attribute_value

		std::ostream& operator<<(std::ostream& s, const attribute_value v);
		std::ostream& operator<<(std::ostream& s, std::pair<const Dwarf_Half, attribute_value>& v);
		interp::interp_class guess_interp(const Dwarf_Half, const attribute_value&);
		interp::interp_class get_interp(const Dwarf_Half attr, const Dwarf_Half form);
		interp::interp_class get_basic_interp(const Dwarf_Half attr, const Dwarf_Half form);
		interp::interp_class get_extended_interp(const Dwarf_Half attr, const Dwarf_Half form);
		std::ostream& operator<<(std::ostream& s, const interp::interp_class cls);

		class die {
			const file& f;
			dieset& m_ds;
			Dwarf_Off m_parent;
			Dwarf_Half m_tag;
			Dwarf_Off m_offset;
			Dwarf_Off cu_offset;
			std::map<Dwarf_Half, attribute_value> m_attrs;
			std::vector<Dwarf_Off> m_children;
			//backref_list m_backrefs;
// 			Dwarf_Addr lowpc;
// 			Dwarf_Addr highpc;
// 			Dwarf_Unsigned bytesize;
// 			Dwarf_Unsigned bitsize;
// 			Dwarf_Unsigned bitoffset;
// 			Dwarf_Unsigned srclang;
// 			Dwarf_Unsigned arrayorder;
		public: // FIXME: make the zero-arg constructor private, maybe by friending dwarfpp_simple
			die() : f(file::default_file()), m_ds(file::default_file().get_ds()), 
				m_parent(0UL), m_tag(0), m_offset(0), cu_offset(0),
				m_attrs(), m_children() 
			{
				std::cerr << "Warning: created dummy encap::die" << std::endl;
			} // dummy to support std::map<_, die> and []
			die(dieset& ds, dwarf::die& d, Dwarf_Off parent_off); 
			die(const die& d); // copy constructor
			
			die& operator=(const die& d)
			{
				assert(&(this->f) == &(d.f)); // can only assign DIEs of same file+dieset
				assert(&(this->m_ds) == &(d.m_ds));
				
				// for now, can only assign sibling DIES
				assert(this->m_parent == d.m_parent);
				
				// offset and cu_offset are *unchanged*!
				
				this->m_tag = d.m_tag;
				this->m_attrs = d.m_attrs;
				this->m_children = d.m_children;
				
				// fIXME: move child DIEs too
				
				return *this;
			}
			
			virtual ~die();
			
			typedef std::map<Dwarf_Half, attribute_value> attribute_map;
			
			// fully specifying constructor
			die(const file& f, dieset& ds, const Dwarf_Off parent, const Dwarf_Half tag, 
				const Dwarf_Off offset, const Dwarf_Off cu_offset, 
				const attribute_map& attrs, const die_off_list& children) :
			f(f), m_ds(ds), m_parent(parent), m_tag(tag), m_offset(offset), 
			cu_offset(cu_offset), m_attrs(attrs), m_children(children) {}
			
			Dwarf_Off get_offset() const { return m_offset; }
			Dwarf_Off offset() const { return m_offset; }
				
			attribute_map& get_attrs() { return m_attrs; }
			attribute_map& attrs() { return m_attrs; }
			const attribute_map& const_attrs() const { return m_attrs; }
			
			Dwarf_Half get_tag() const { return m_tag; }
			Dwarf_Half set_tag(Dwarf_Half v) { return m_tag = v; }
			Dwarf_Half tag() const { return m_tag; }
			
			Dwarf_Off parent_offset() const { return m_parent; }
			Dwarf_Off parent() const { return m_parent; }
			
			die_off_list& children()  { return m_children; }
			die_off_list& get_children() { return m_children; }
			
			bool has_attr(Dwarf_Half at) const { return (m_attrs.find(at) != m_attrs.end()); }
			const attribute_value& operator[] (Dwarf_Half at) const 
			{ 
				if (has_attr(at)) return m_attrs.find(at)->second; 
				else return attribute_value::DOES_NOT_EXIST(); 
			}
			attribute_value& put_attr(Dwarf_Half attr, attribute_value val) 
			{ 
				m_attrs.insert(std::make_pair(attr, val)); 
				return m_attrs.find(attr)->second; 
			}			
			friend std::ostream& operator<<(std::ostream& o, const dwarf::encap::die& d);
			void print(std::ostream& o) const
			{
				o << *this;
			}			
		};	
	} // end namespace encap
		
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
	
	class evaluator {
		std::stack<Dwarf_Unsigned> stack;
		dwarf::encap::expr expr;
		void eval();
	public:
		evaluator(const std::vector<unsigned char> expr) 
		{
			assert(false);
		}
		evaluator(const dwarf::encap::attribute_value& av)
		{
			if (av.get_form() != dwarf::encap::attribute_value::LOCLIST) throw "not a DWARF expression";
			if (av.get_loclist().size() != 1) throw "only support singleton loclists for now";			
			expr = *(av.get_loclist().begin());
			eval();
		}
		Dwarf_Unsigned tos() const { return stack.top(); }
	};
}

#endif
