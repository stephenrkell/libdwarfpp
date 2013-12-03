/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * attr.hpp: transparently-allocated, mutable representations 
 *			of libdwarf-like structures.
 *
 * Copyright (c) 2010, Stephen Kell.
 */

#ifndef __DWARFPP_ATTR_HPP
#define __DWARFPP_ATTR_HPP

#include <memory>

#include "spec.hpp"
#include "private/libdwarf.hpp" /* includes libdwarf.h, Error, No_entry, some fwddecls */

#include <boost/optional.hpp>

#include <srk31/util.hpp> /* for forward_constructors */

namespace dwarf
{
	namespace spec { 
		class basic_die; class abstract_dieset; class type_die; 
		std::ostream& operator<<(std::ostream& o, const dwarf::spec::basic_die& d);
	}
	namespace lib 
	{ 
		std::ostream& operator<<(std::ostream& s, const Dwarf_Ranges& rl);
		bool operator==(const Dwarf_Ranges& arg1, const Dwarf_Ranges& arg2);
		class basic_die; 
		std::ostream& operator<<(std::ostream& s, const dwarf::lib::Dwarf_Loc& l);
		class attribute;
	}
	namespace core
	{
		struct Attribute;
		struct AttributeList;
		struct Debug;
		struct Die;
		struct root_die;
		struct iterator_base;
		struct basic_die;
		struct type_die;
		template <typename DerefAs = basic_die> struct iterator_df;
	}
	namespace encap
	{
		using namespace dwarf::lib;
		class rangelist;
		
		template <typename Value> struct die_out_edge_iterator; // forward decl
		template <typename Value> struct sibling_dep_edge_iterator;
		class dieset; // forward decl
		class die;
		class loclist;
		using core::root_die;
		
		class attribute_value {
				friend std::ostream& operator<<(std::ostream& o, const dwarf::encap::die& d);
				friend std::ostream& dwarf::spec::operator<<(std::ostream& o, const dwarf::spec::basic_die& d);                
				friend class encap::die; // for the "convert to strong references" hack
				friend class encap::dieset;
				friend class core::basic_die; // for use of the NO_ATTR constructor in find_attr
		public: 
			struct weak_ref { 
				friend class attribute_value;
 				//friend dieset::operator=(const dieset& arg);
				friend class encap::dieset;
 	        	friend struct die_out_edge_iterator<weak_ref>; // in encap_graph.hpp
                friend struct sibling_dep_edge_iterator<weak_ref>; // in encap_sibling_graph.hpp

				Dwarf_Off off; 
				bool abs;
				Dwarf_Off referencing_off; // off of DIE keeping the reference
				spec::abstract_dieset *p_ds; // FIXME: make this private again after fixing encap_graph.hpp
				root_die *p_root;            // FIXME: hmm
				Dwarf_Half referencing_attr; // attr# of attribute holding the reference
				weak_ref(spec::abstract_dieset& ds, Dwarf_Off off, bool abs, 
					Dwarf_Off referencing_off, Dwarf_Half referencing_attr)
                    :	off(off), abs(abs), 
						referencing_off(referencing_off), 
                        p_ds(&ds), p_root(0), referencing_attr(referencing_attr) {}
				weak_ref(root_die& r, Dwarf_Off off, bool abs,  // the same but root not ds
					Dwarf_Off referencing_off, Dwarf_Half referencing_attr)
                    :	off(off), abs(abs), 
						referencing_off(referencing_off), 
                        p_ds(0), p_root(&r), referencing_attr(referencing_attr) {}
                // weak_ref is default-constructible
                weak_ref() : off(0UL), abs(false), referencing_off(0), p_ds(0), p_root(0), referencing_attr(0)
                { std::cerr << "Warning: default-constructed a weak_ref!" << std::endl; }
                weak_ref(bool arg) : off(0UL), abs(false), referencing_off(0), p_ds(0), p_root(0), referencing_attr(0)
                { /* Same as above but extra dummy argument, to suppress warning (see clone()). */}
                virtual weak_ref& operator=(const weak_ref& r); // assignment
                virtual weak_ref *clone() { 
                	weak_ref *n = new weak_ref(true); // FIXME: deleted how? containing attribute's destructor? yes, I think so
                    n->p_ds = this->p_ds; // this is checked during assignment
					n->p_root = this->p_root;
                    *n = *this;
                    return n;
                }
				weak_ref(const weak_ref& r); // copy constructor
				virtual ~weak_ref() {}
                bool operator==(const weak_ref& arg) const {
                	return off == arg.off && abs == arg.abs
                    && referencing_off == arg.referencing_off 
                    && referencing_attr == arg.referencing_attr
                    && p_ds == arg.p_ds
					&& p_root == arg.p_root; 
                }
                bool operator!=(const weak_ref& arg) const {
                	return !(*this == arg);
                }
			};
			struct ref : weak_ref {
				ref(spec::abstract_dieset& ds, Dwarf_Off off, bool abs, 
					Dwarf_Off referencing_off, Dwarf_Half referencing_attr);
                encap::dieset& ds;					
				virtual ~ref();
				ref(const ref& r); // copy constructor
                virtual ref& operator=(const weak_ref& r); 
                ref *clone() { 
                	ref *n = new ref(*this->p_ds, this->off, this->abs,
                    	this->referencing_off, this->referencing_attr);
                    return n;
                }
            };
			struct address 
			/* Why do we have this? It's because of overload resolution.
			 * Dwarf_Addr is just a typedef for some integer type, and one
			 * of the other constructors of attribute_value also takes that
			 * integer type. To disambiguate, we create an encapsulated 
			 * address data type. */
			{ 
				Dwarf_Addr addr; 
				address(Dwarf_Addr val) { this->addr = val; }
				bool operator==(const address& arg) const { return this->addr == arg.addr; }
				bool operator!=(const address& arg) const { return !(*this == arg); }
				bool operator<(const address& arg) const { return this->addr < arg.addr; }
				bool operator<=(const address& arg) const { return this->addr <= arg.addr; }
				bool operator>(const address& arg) const { return this->addr > arg.addr; }
				bool operator>=(const address& arg) const { return this->addr >= arg.addr; }
				bool operator==(Dwarf_Addr arg) const { return this->addr == arg; }
				bool operator!=(Dwarf_Addr arg) const { return !(*this == arg); }
				bool operator<(Dwarf_Addr arg) const { return this->addr < addr; }
				bool operator<=(Dwarf_Addr arg) const { return this->addr <= addr; }
				bool operator>(Dwarf_Addr arg) const { return this->addr > addr; }
				bool operator>=(Dwarf_Addr arg) const { return this->addr >= addr; }
				//Dwarf_Addr operator Dwarf_Addr() { return addr; }
				/* FIXME: why *not* have the above? There must be a good reason....
				 *  ambiguity maybe? */
			};
			/*static const attribute_value& DOES_NOT_EXIST() {
				if (dne_val == 0) dne_val = new attribute_value(); // FIXME: delete this anywhere?
				return *dne_val;
			}*/
			enum form { NO_ATTR, ADDR, FLAG, UNSIGNED, SIGNED, BLOCK, STRING, REF, LOCLIST, RANGELIST, UNRECOG }; // TODO: complete?
			form get_form() const { return f; }
		private:
            spec::abstract_dieset *p_ds; // FIXME: really needed? refs have a p_ds in them too
			Dwarf_Half orig_form;
			form f; // discriminant			
			union {
				Dwarf_Bool v_flag;
				Dwarf_Unsigned v_u;
				Dwarf_Signed v_s;
				address v_addr;
				//std::vector<unsigned char> *v_block; // TODO: make resource-managing
				//std::string *v_str; // TODO: make resource-managing
				//ref *v_ref;
			//};
			// HACK: we can't include these in the union, it seems
			// TODO: instead of allocating them here, use new (here) and delete (in destructor)
				std::vector<unsigned char> *v_block;
				std::string *v_string;
				weak_ref *v_ref;
				encap::loclist *v_loclist;
				encap::rangelist *v_rangelist;
			};
			// -- the operator<< is a friend
			friend std::ostream& ::dwarf::lib::operator<<(std::ostream& s, const dwarf::lib::Dwarf_Loc& l);

			static form dwarf_form_to_form(const Dwarf_Half form); // helper hack

		private:
			attribute_value() : orig_form(0), f(NO_ATTR) { v_u = 0U; }
			attribute_value(spec::abstract_dieset& ds, Dwarf_Unsigned data, Dwarf_Half o_form) 
				: p_ds(&ds), orig_form(o_form), f(dwarf_form_to_form(o_form)), v_u(data) {} 
			// the following constructor is a HACK to re-use formatting logic when printing Dwarf_Locs
			attribute_value(Dwarf_Unsigned data, Dwarf_Half o_form) 
				: p_ds(0), orig_form(o_form), f(dwarf_form_to_form(o_form)), v_u(data) {} 
			// the following is a temporary HACK to allow core:: to create attribute_values
		public:
			attribute_value(const dwarf::core::Attribute& attr, 
				const dwarf::core::Die& d, 
				root_die& r/*,
				spec::abstract_def& = spec::DEFAULT_DWARF_SPEC*/);
				// spec is no longer passed because it's deducible from r and d.get_offset()
		public:
			attribute_value(spec::abstract_dieset& ds, const dwarf::lib::attribute& a);
// 			//attribute_value() {} // allow uninitialised temporaries
 			attribute_value(spec::abstract_dieset& ds, Dwarf_Bool b) : p_ds(&ds), orig_form(DW_FORM_flag), f(FLAG), v_flag(b) {}
 			attribute_value(spec::abstract_dieset& ds, address addr) : p_ds(&ds), orig_form(DW_FORM_addr), f(ADDR), v_addr(addr) {}		
 			attribute_value(spec::abstract_dieset& ds, Dwarf_Unsigned u) : p_ds(&ds), orig_form(DW_FORM_udata), f(UNSIGNED), v_u(u) {}				
 			attribute_value(spec::abstract_dieset& ds, Dwarf_Signed s) : p_ds(&ds), orig_form(DW_FORM_sdata), f(SIGNED), v_s(s) {}			
 			attribute_value(spec::abstract_dieset& ds, const char *s) : p_ds(&ds), orig_form(DW_FORM_string), f(STRING), v_string(new std::string(s)) {}
 			attribute_value(spec::abstract_dieset& ds, const std::string& s) : p_ds(&ds), orig_form(DW_FORM_string), f(STRING), v_string(new std::string(s)) {}				
 			attribute_value(spec::abstract_dieset& ds, weak_ref& r) : p_ds(&ds), orig_form(DW_FORM_ref_addr), f(REF), v_ref(r.clone()) {}
 			attribute_value(spec::abstract_dieset& ds, std::shared_ptr<spec::basic_die> ref_target);
			attribute_value(spec::abstract_dieset& ds, const encap::loclist& l);
			attribute_value(spec::abstract_dieset& ds, const encap::rangelist& l);
		public:
			
			Dwarf_Bool get_flag() const { assert(f == FLAG); return v_flag; }
			// allow mix-and-match among signed and unsigned
			Dwarf_Unsigned get_unsigned() const 
			{ assert(f == UNSIGNED || f == SIGNED); return (f == UNSIGNED) ? v_u : static_cast<Dwarf_Unsigned>(v_s); }
			Dwarf_Signed get_signed() const 
			{ assert(f == SIGNED || f == UNSIGNED); return (f == SIGNED) ? v_s : static_cast<Dwarf_Signed>(v_u); }
			const std::vector<unsigned char> *get_block() const { assert(f == BLOCK); return v_block; }
			const std::string& get_string() const { assert(f == STRING); return *v_string; }
			/* I added the tolerance of UNSIGNED here because sometimes high_pc is an address, 
			 * other times it's unsigned... BUT it means something different in the latter 
			 * case (lopc-relative) so it's best to handle this difference higher up. */
			address get_address() const { assert(f == ADDR/* || f == UNSIGNED*/); return/* (f == ADDR) ?*/ v_addr /*: address(static_cast<Dwarf_Addr>(v_u))*/; }
			const loclist& get_loclist() const { assert(f == LOCLIST); return *v_loclist; }
			const rangelist& get_rangelist() const { assert(f == RANGELIST); return *v_rangelist; }

			weak_ref& get_ref() const { assert(f == REF); return *v_ref; }
			Dwarf_Off get_refoff() const { assert(f == REF); return v_ref->off; }
			Dwarf_Off get_refoff_is_type() const { assert(f == REF); return v_ref->off; }
			core::iterator_df<> get_refiter() const;// { assert(f == REF); return v_ref->off; }
			core::iterator_df<core::type_die> get_refiter_is_type() const;// { assert(f == REF); return v_ref->off; }
			std::shared_ptr<spec::basic_die> get_refdie() const; // defined in cpp file
			//spec::basic_die& get_refdie() const; // defined in cpp file
			std::shared_ptr<spec::type_die> get_refdie_is_type() const; 
			//spec::type_die& get_refdie_is_type() { return dynamic_cast<spec::type_die&>(get_refdie()); }
			/* ^^^ I think a plain reference is okay here because the "this" pointer
			 * (i.e. whatever pointer we'll be accessing the attribute through)
			 * will be keeping the containing DIE in existence. */

			bool operator==(const attribute_value& v) const;
			bool operator!=(const attribute_value &v) const { return !(*this == v); }
			
			void print_raw(std::ostream& s) const;
			void print_as(std::ostream& s, int cls) const;
			friend std::ostream& operator<<(std::ostream& s, const attribute_value v);
			friend std::ostream& operator<<(std::ostream& s, std::pair<const Dwarf_Half, attribute_value>&);
			//friend std::ostream& operator<<(std::ostream& o, const dwarf::encap::die& d);
			// copy constructor
			attribute_value(const attribute_value& av);
			
			virtual ~attribute_value();
		}; // end class attribute_value
		
		struct attribute_map : public std::map<Dwarf_Half, attribute_value> 
		{
			typedef std::map<Dwarf_Half, attribute_value> base;
			// forward constructors
			//forward_constructors(base, attribute_map)
			// hmm -- this messes with overload resolution; just forward default for now
			attribute_map() : base() {}
			
			// also construct from AttributeList
			attribute_map(const core::AttributeList& a, const core::Die& d, root_die& r, 
				spec::abstract_def &p_spec = spec::DEFAULT_DWARF_SPEC);
			
			// static 3-arg print function
			//static std::ostream& 
			//print_to(std::ostream& s, const AttributeList& attrs, root_die& r);
		};
		std::ostream& operator<<(std::ostream& s, const attribute_map& arg); 
		
        
        bool operator==(Dwarf_Addr arg, attribute_value::address a);
        bool operator!=(Dwarf_Addr arg, attribute_value::address a);
        bool operator<(Dwarf_Addr arg, attribute_value::address a);
        bool operator<=(Dwarf_Addr arg, attribute_value::address a);
        bool operator>(Dwarf_Addr arg, attribute_value::address a);
        bool operator>=(Dwarf_Addr arg, attribute_value::address a);
        Dwarf_Addr operator-(Dwarf_Addr arg, attribute_value::address a);
        Dwarf_Addr operator-(attribute_value::address a, Dwarf_Addr arg);

		std::ostream& operator<<(std::ostream& s, const attribute_value v);
		std::ostream& operator<<(std::ostream& s, std::pair<const Dwarf_Half, attribute_value>& v);
        std::ostream& operator<<(std::ostream& s, const attribute_value::address& a);
    }
}
    
#endif

