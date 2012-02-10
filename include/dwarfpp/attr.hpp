/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * attr.hpp: transparently-allocated, mutable representations 
 *            of libdwarf-like structures.
 *
 * Copyright (c) 2010, Stephen Kell.
 */

#ifndef __DWARFPP_ATTR_HPP
#define __DWARFPP_ATTR_HPP

#include <boost/shared_ptr.hpp>

#include "spec.hpp"
#include "lib.hpp"
//#include "spec_adt.hpp"
#include "expr.hpp"

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
    }
	namespace encap
    {
    	using namespace dwarf::lib;
        
		template <typename Value> struct die_out_edge_iterator; // forward decl
		template <typename Value> struct sibling_dep_edge_iterator;
    	class dieset; // forward decl
        class die;
        
//         struct arange
//         {
//         	Dwarf_Addr start;
// 			Dwarf_Unsigned length;
// 			Dwarf_Off cu_die_offset;
//         };
//         struct arangelist : public std::vector<arange>
//         {
//         	arangelist(lib::aranges& all_aranges, Dwarf_Unsigned i)
//             {
//             	arange r = { 42, 42, 42 };  // any non-sentinel value
//                 try
//                 {
//                     while (!(r.start == 0 && r.length == 0))
//                     {
//                 	    int ret = all_aranges.get_info(i++, &r.start, &r.length, &r.cu_die_offset);
//                 	    assert(ret == DW_DLV_OK);
//                         // assume we don't have a "base address selection entry"
//                         assert(r.start != 0xffffffffU && r.start != 0xffffffffffffffffULL);
//                         this->push_back(r);
//                     } // terminate on end-of-list entry (start and length both 0)...
//                 } // ... or on exhausting the list
//                 catch (No_entry) {}
//             }
//             boost::optional<std::pair<Dwarf_Off, int> >
//         	find_addr(Dwarf_Off cu_relative_address);
//         };
        class rangelist : public std::vector<lib::Dwarf_Ranges> 
        {
        public:
        	template <class In> rangelist(In first, In last) 
            : std::vector<lib::Dwarf_Ranges>(first, last) {}
            rangelist() : std::vector<lib::Dwarf_Ranges>() {}
            
            boost::optional<std::pair<Dwarf_Off, long> >
            find_addr(Dwarf_Off file_relative_addr);
        };
        std::ostream& operator<<(std::ostream& s, const rangelist& rl);
        
		class attribute_value {
				friend std::ostream& operator<<(std::ostream& o, const dwarf::encap::die& d);
				friend std::ostream& dwarf::spec::operator<<(std::ostream& o, const dwarf::spec::basic_die& d);                
				friend class encap::die; // for the "convert to strong references" hack
				friend class encap::dieset;
		public: 
			struct weak_ref { 
				friend class attribute_value;
 				//friend dieset::operator=(const dieset& arg);
				friend class encap::dieset;
 	        	friend struct die_out_edge_iterator<weak_ref>; // in encap_graph.hpp
                friend struct sibling_dep_edge_iterator<weak_ref>; // in encap_sibling_graph.hpp
				Dwarf_Off off; bool abs;
				Dwarf_Off referencing_off; // off of DIE keeping the reference
				spec::abstract_dieset *p_ds; // FIXME: make this private again after fixing encap_graph.hpp
				Dwarf_Half referencing_attr; // attr# of attribute holding the reference
				weak_ref(spec::abstract_dieset& ds, Dwarf_Off off, bool abs, 
					Dwarf_Off referencing_off, Dwarf_Half referencing_attr)
                    :	off(off), abs(abs), 
						referencing_off(referencing_off), 
                        p_ds(&ds), referencing_attr(referencing_attr) {}
                // weak_ref is default-constructible
                weak_ref() : off(0UL), abs(false), referencing_off(0), p_ds(0), referencing_attr(0)
                { std::cerr << "Warning: default-constructed a weak_ref!" << std::endl; }
                weak_ref(bool arg) : off(0UL), abs(false), referencing_off(0), p_ds(0), referencing_attr(0)
                { /* Same as above but extra dummy argument, to suppress warning (see clone()). */}
                virtual weak_ref& operator=(const weak_ref& r); // assignment
                virtual weak_ref *clone() { 
                	weak_ref *n = new weak_ref(true); // FIXME: deleted how? containing attribute's destructor? yes, I think so
                    n->p_ds = this->p_ds; // this is checked during assignment
                    *n = *this;
                    return n;
                }
				weak_ref(const weak_ref& r); // copy constructor
				virtual ~weak_ref() {}
                bool operator==(const weak_ref& arg) const {
                	return off == arg.off && abs == arg.abs
                    && referencing_off == arg.referencing_off 
                    && referencing_attr == arg.referencing_attr
                    && p_ds == arg.p_ds; 
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
			/* Why do we have this? 
			 * Hmm -- I think it was about overload resolution. Dwarf_Addr
			 * is just a typedef for some integer type. And one of the other
			 * constructors of attribute_value was taking that integer type.
			 * So to disambiguate, we create an encapsulated address data type. */
            { 
            	Dwarf_Addr addr; 
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
			enum form { NO_ATTR, ADDR, FLAG, UNSIGNED, SIGNED, BLOCK, STRING, REF, LOCLIST, RANGELIST }; // TODO: complete?
			form get_form() const { return f; }
		private:
            spec::abstract_dieset *p_ds;
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
				loclist *v_loclist;
                rangelist *v_rangelist;
			};
			// the following constructor is a HACK to re-use formatting logic when printing Dwarf_Locs
			// -- the operator<< is a friend
			friend std::ostream& ::dwarf::lib::operator<<(std::ostream& s, const dwarf::lib::Dwarf_Loc& l);

			attribute_value(spec::abstract_dieset& ds, Dwarf_Unsigned data, Dwarf_Half o_form) 
				: p_ds(&ds), orig_form(o_form), f(dwarf_form_to_form(o_form)), v_u(data) {} 
			attribute_value(Dwarf_Unsigned data, Dwarf_Half o_form) 
				: p_ds(0), orig_form(o_form), f(dwarf_form_to_form(o_form)), v_u(data) {} 
			static const form dwarf_form_to_form(const Dwarf_Half form); // helper hack

			/*attribute_value() : orig_form(0), f(NO_ATTR) { v_u = 0U; } // FIXME: this zero value can still be harmful when clients do get_ on wrong type
				// ideally the return values of get_() methods should return some Option-style type,
				// which I think boost provides... i.e. Some of value | None*/
			static const attribute_value *dne_val;
			
		public:
			attribute_value(spec::abstract_dieset& ds, const dwarf::lib::attribute& a);
// 			//attribute_value() {} // allow uninitialised temporaries
 			attribute_value(spec::abstract_dieset& ds, Dwarf_Bool b) : p_ds(&ds), orig_form(DW_FORM_flag), f(FLAG), v_flag(b) {}
// 			// HACK to allow overload resolution: addr is ignored
 			attribute_value(spec::abstract_dieset& ds, address addr) : p_ds(&ds), orig_form(DW_FORM_addr), f(ADDR), v_addr(addr) {}		
 			attribute_value(spec::abstract_dieset& ds, Dwarf_Unsigned u) : p_ds(&ds), orig_form(DW_FORM_udata), f(UNSIGNED), v_u(u) {}				
 			attribute_value(spec::abstract_dieset& ds, Dwarf_Signed s) : p_ds(&ds), orig_form(DW_FORM_sdata), f(SIGNED), v_s(s) {}			
// 			attribute_value(dwarf::block& b) : f(BLOCK), v_block(new std::vector<unsigned char>(
// 					(unsigned char *) b.data(), ((unsigned char *) b.data()) + b.len())) 
// 					{ /*std::cerr << "Constructed a block attribute_value with vector at 0x" << std::hex << (unsigned) v_block << std::dec << std::endl;*/ }			
 			attribute_value(spec::abstract_dieset& ds, const char *s) : p_ds(&ds), orig_form(DW_FORM_string), f(STRING), v_string(new std::string(s)) {}
 			attribute_value(spec::abstract_dieset& ds, const std::string& s) : p_ds(&ds), orig_form(DW_FORM_string), f(STRING), v_string(new std::string(s)) {}				
// 			attribute_value(Dwarf_Off off, bool abs) : f(REF), v_ref(new ref(off, abs)) {}
//			attribute_value(die& d) : orig_form(DW_FORM_ref_addr), f(REF), v_ref(new ref(*d.p_ds, d.m_offset, true, 
 			attribute_value(spec::abstract_dieset& ds, weak_ref& r) : p_ds(&ds), orig_form(DW_FORM_ref_addr), f(REF), v_ref(r.clone()) {}
 			attribute_value(spec::abstract_dieset& ds, boost::shared_ptr<spec::basic_die> ref_target);
 			//attribute_value(spec::abstract_dieset& ds, boost::shared_ptr<spec::basic_die> p_r)
			// : p_ds(&ds), orig_form(DW_FORM_ref_addr), f(REF), v_ref(r.clone()) {}
//			attribute_value(lib::abstract_dieset& ds, spec::basic_die& d) : orig_form(DW_FORM_ref_addr), f(REF)
//            { assert(dynamic_cast<encap::dieset *>(ds)); this->v_ref = new ref(*d.p_ds, d.m_offset, true, 
			attribute_value(spec::abstract_dieset& ds, const loclist& l) : p_ds(&ds), orig_form(DW_FORM_data4), f(LOCLIST), v_loclist(new loclist(l)) {}
			attribute_value(spec::abstract_dieset& ds, const rangelist& l) : p_ds(&ds), orig_form(DW_FORM_data4), f(RANGELIST), v_rangelist(new rangelist(l)) {}
		public:
			
			Dwarf_Bool get_flag() const { assert(f == FLAG); return v_flag; }
			Dwarf_Unsigned get_unsigned() const { assert(f == UNSIGNED); return v_u; }
			Dwarf_Signed get_signed() const { assert(f == SIGNED); return v_s; }
			const std::vector<unsigned char> *get_block() const { assert(f == BLOCK); return v_block; }
			const std::string& get_string() const { assert(f == STRING); return *v_string; }
			weak_ref& get_ref() const { assert(f == REF); return *v_ref; }
            address get_address() const { assert(f == ADDR); return v_addr; }
            boost::shared_ptr<spec::basic_die> get_refdie() const; // defined in cpp file
			//spec::basic_die& get_refdie() const; // defined in cpp file
            boost::shared_ptr<spec::type_die> get_refdie_is_type() const { return boost::dynamic_pointer_cast<spec::type_die>(get_refdie()); }
            //spec::type_die& get_refdie_is_type() { return dynamic_cast<spec::type_die&>(get_refdie()); }
            /* ^^^ I think a plain reference is okay here because the "this" pointer
             * (i.e. whatever pointer we'll be accessing the attribute through)
             * will be keeping the containing DIE in existence. */
			const loclist& get_loclist() const { assert(f == LOCLIST); return *v_loclist; }
            const rangelist& get_rangelist() const { assert(f == RANGELIST); return *v_rangelist; }
			
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
                    case RANGELIST:
                    	return *(this->v_rangelist) == *(v.v_rangelist);
					default: 
						std::cerr << "Warning: comparing a dwarf::encap::attribute_value of unknown form " << v.f << std::endl;
						return false;
				} // end switch
			}
			
			bool operator!=(const attribute_value &v) const { return !(*this == v); }
			
			void print_raw(std::ostream& s) const;
			void print_as(std::ostream& s, int cls) const;
			friend std::ostream& operator<<(std::ostream& s, const attribute_value v);
			friend std::ostream& operator<<(std::ostream& s, std::pair<const Dwarf_Half, attribute_value>&);
			//friend std::ostream& operator<<(std::ostream& o, const dwarf::encap::die& d);
			// copy constructor
			attribute_value(const attribute_value& av) : p_ds(av.p_ds), f(av.f)
			{
            	assert(this->p_ds == av.p_ds);
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
						v_ref = /*new ref(av.v_ref->ds, av.v_ref->off, av.v_ref->abs,
							av.v_ref->referencing_off, av.v_ref->referencing_attr);*/
                            av.v_ref->clone();
					break;
					case ADDR:
						v_addr = av.v_addr;
					break;
					case LOCLIST:
						v_loclist = new loclist(*av.v_loclist);
					break;
                    case RANGELIST:
                    	v_rangelist = new rangelist(*av.v_rangelist);
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
                    case RANGELIST:
                    	delete v_rangelist;
                    break;
					default: break;
				} // end switch
			} // end ~attribute_value
		}; // end class attribute_value
        
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

