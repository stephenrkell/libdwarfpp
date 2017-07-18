/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * attr.hpp: transparently-allocated, mutable representations
 *			of libdwarf-like structures.
 *
 * Copyright (c) 2010--17, Stephen Kell.
 */

#ifndef DWARFPP_ATTR_HPP_
#define DWARFPP_ATTR_HPP_

#include <memory>
#include <vector>

#include "spec.hpp"
#include "libdwarf.hpp" /* includes libdwarf.h, Error, No_entry, some fwddecls */

#include <boost/optional.hpp>
#include <srk31/util.hpp> /* for forward_constructors */

namespace dwarf
{
	namespace core
	{
		// libdwarf handle stuff -- FIXME: avoid depending where possible
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
		class loclist;
		using core::root_die;
		using core::debug;
		
		class attribute_value {
				friend class core::basic_die; // for use of the NO_ATTR constructor in find_attr
				friend class core::iterator_base; // the same in iterator_base::attr()
		public: 
			struct weak_ref { 
				friend class attribute_value;

				Dwarf_Off off; 
				bool abs;
				Dwarf_Off referencing_off; // off of DIE keeping the reference
				root_die *p_root;            // FIXME: hmm
				Dwarf_Half referencing_attr; // attr# of attribute holding the reference
				weak_ref(root_die& r, Dwarf_Off off, bool abs,
					Dwarf_Off referencing_off, Dwarf_Half referencing_attr)
					:	off(off), abs(abs), 
						referencing_off(referencing_off), 
						p_root(&r), referencing_attr(referencing_attr) {}
				// weak_ref is default-constructible
				weak_ref() : off(0UL), abs(false), referencing_off(0), p_root(0), referencing_attr(0)
				{ debug() << "Warning: default-constructed a weak_ref!" << std::endl; }
				weak_ref(bool arg) : off(0UL), abs(false), referencing_off(0), p_root(0), referencing_attr(0)
				{ /* Same as above but extra dummy argument, to suppress warning (see clone()). */}
				virtual weak_ref& operator=(const weak_ref& r); // assignment
				virtual weak_ref *clone() const { 
					weak_ref *n = new weak_ref(true); // FIXME: deleted how? containing attribute's destructor? yes, I think so
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
					&& p_root == arg.p_root; 
				}
				bool operator!=(const weak_ref& arg) const {
					return !(*this == arg);
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
				 * ambiguity maybe? */
			};
			enum form { NO_ATTR, ADDR, FLAG, UNSIGNED, SIGNED, BLOCK, STRING, REF, LOCLIST, RANGELIST, UNRECOG }; // TODO: complete?
			form get_form() const { return f; }
		private:
			Dwarf_Half orig_form;
			form f; // discriminant
			union {
				Dwarf_Bool v_flag;
				Dwarf_Unsigned v_u;
				Dwarf_Signed v_s;
				address v_addr;
				// pointees are RAII-allocated with new/delete -- because non-PODs can't go in union
				std::vector<unsigned char> *v_block;
				std::string *v_string;
				weak_ref *v_ref;
				encap::loclist *v_loclist;
				encap::rangelist *v_rangelist;
			};
			static form dwarf_form_to_form(const Dwarf_Half form); // helper hack
			// -- the operator<< is a friend (WHY?)
			friend std::ostream& ::dwarf::lib::operator<<(std::ostream& s, const dwarf::lib::Dwarf_Loc& l);

		private:
			attribute_value() : orig_form(0), f(NO_ATTR) { v_u = 0U; }
			// the following constructor is a HACK to re-use formatting logic when printing Dwarf_Locs
			attribute_value(Dwarf_Unsigned data, Dwarf_Half o_form) 
				:  orig_form(o_form), f(dwarf_form_to_form(o_form)), v_u(data) {} 
			// the following is a temporary HACK to allow core:: to create attribute_values
		public:
			attribute_value(const dwarf::core::Attribute& attr, 
				const dwarf::core::Die& d, 
				root_die& r/*,
				spec::abstract_def& = spec::DEFAULT_DWARF_SPEC*/);
				// spec is no longer passed because it's deducible from r and d.get_offset()
		public:
			explicit attribute_value(Dwarf_Bool b)         : orig_form(DW_FORM_flag),	 f(FLAG),	  v_flag(b) {}
			explicit attribute_value(address addr)         : orig_form(DW_FORM_addr),	 f(ADDR),	  v_addr(addr) {}
			explicit attribute_value(Dwarf_Unsigned u)     : orig_form(DW_FORM_udata),	 f(UNSIGNED), v_u(u) {}
			explicit attribute_value(Dwarf_Signed s)       : orig_form(DW_FORM_sdata),	 f(SIGNED),   v_s(s) {}
			attribute_value(const char *s)        : orig_form(DW_FORM_string),   f(STRING),   v_string(new std::string(s)) {}
			attribute_value(const std::string& s) : orig_form(DW_FORM_string),   f(STRING),   v_string(new std::string(s)) {}
			attribute_value(const weak_ref& r)    : orig_form(DW_FORM_ref_addr), f(REF),      v_ref(r.clone()) {}
			
		public:
			bool is_flag() const { return f == FLAG; }
			Dwarf_Bool get_flag() const { assert(is_flag()); return v_flag; }
			// allow mix-and-match among signed and unsigned
			bool is_unsigned() const { return f == UNSIGNED || f == SIGNED; }
			Dwarf_Unsigned get_unsigned() const 
			{ assert(is_unsigned()); return (f == UNSIGNED) ? v_u : static_cast<Dwarf_Unsigned>(v_s); }
			bool is_signed() const { return f == SIGNED || f == UNSIGNED; }
			Dwarf_Signed get_signed() const 
			{ assert(is_signed()); return (f == SIGNED) ? v_s : static_cast<Dwarf_Signed>(v_u); }
			bool is_block() const { return f == BLOCK; }
			const std::vector<unsigned char> *get_block() const { assert(is_block()); return v_block; }
			bool is_string() const { return f == STRING; }
			const std::string& get_string() const { assert(is_string()); return *v_string; }
			/* I added the tolerance of UNSIGNED here because sometimes high_pc is an address, 
			 * other times it's unsigned... BUT it means something different in the latter 
			 * case (lopc-relative) so it's best to handle this difference higher up. */
			bool is_address() const { return f == ADDR /* || f == UNSIGNED*/; }
			address get_address() const { assert(is_address()); return/* (f == ADDR) ?*/ v_addr /*: address(static_cast<Dwarf_Addr>(v_u))*/; }
			bool is_loclist() const { return f == LOCLIST; }
			const loclist& get_loclist() const { assert(is_loclist()); return *v_loclist; }
			bool is_rangelist() const { return f == RANGELIST; }
			const rangelist& get_rangelist() const { assert(is_rangelist()); return *v_rangelist; }
			bool is_ref() const { return f == REF; }
			weak_ref& get_ref() const { assert(is_ref()); return *v_ref; }
			Dwarf_Off get_refoff() const { assert(is_ref()); return v_ref->off; }
			Dwarf_Off get_refoff_is_type() const { assert(is_ref()); return v_ref->off; }
			bool is_refiter() const { return f == REF; }
			core::iterator_df<> get_refiter() const;// { assert(f == REF); return v_ref->off; }
			bool is_refiter_is_type() const { return f == REF; /* FIXME */ }
			core::iterator_df<core::type_die> get_refiter_is_type() const;// { assert(f == REF); return v_ref->off; }
			bool is_refdie() const { return f == REF; /* FIXME */ }

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
			
			void print(std::ostream& s, unsigned indent_level) const;
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
