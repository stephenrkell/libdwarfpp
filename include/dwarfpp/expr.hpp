/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * expr.hpp: simple C++ abstraction of DWARF expressions and location lists.
 *
 * Copyright (c) 2010, Stephen Kell.
 */

#ifndef __DWARFPP_EXPR_HPP
#define __DWARFPP_EXPR_HPP

#include "spec.hpp"
#include "lib.hpp"

namespace dwarf
{
	namespace encap
    {
    	using namespace dwarf::lib;
		typedef ::dwarf::lib::Dwarf_Loc expr_instr;
        
		struct loc_expr : public std::vector<expr_instr>
		{
	        const dwarf::spec::abstract_def& spec;
			Dwarf_Addr hipc;
			Dwarf_Addr lopc;
			/*std::vector<expr_instr>& m_expr;*/ 
			loc_expr(const spec::abstract_def& spec = spec::dwarf3) 
            : spec(spec), hipc(0), lopc(0)/*, m_expr(*this)*/ {}
			loc_expr(const Dwarf_Locdesc& desc, const spec::abstract_def& spec = spec::dwarf3) : 
                std::vector<expr_instr>(desc.ld_s, desc.ld_s + desc.ld_cents),
                spec(spec), hipc(desc.ld_hipc), lopc(desc.ld_lopc)/*,
                m_expr(*this)*/ {}
            loc_expr(const std::vector<expr_instr>& expr,
            	const spec::abstract_def& spec = spec::dwarf3) 
            : std::vector<expr_instr>(expr),
              spec(spec), hipc(0), lopc(0)/*, m_expr(*this)*/ {}
            loc_expr(const loc_expr& arg)  // copy constructor
            : std::vector<expr_instr>(arg.begin(), arg.end()),
              spec(arg.spec), hipc(arg.hipc), lopc(arg.lopc)/*, 
              m_expr(*this)*/ {}
            loc_expr piece_for_offset(Dwarf_Off offset) const;
            std::vector<std::pair<loc_expr, Dwarf_Unsigned> > pieces() const;
			
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
			
			template <class In> loc_expr(In first, In last, 
            	const spec::abstract_def& spec = spec::dwarf3) 
            : std::vector<expr_instr>(first, last),
              spec(spec), /*m_expr(first, last), */hipc(0), lopc(0) {}
              
			/* This template parses a location expression out of an array of unsigneds. */
			template<size_t s> 
            loc_expr(Dwarf_Unsigned (&arr)[s], Dwarf_Addr lopc, Dwarf_Addr hipc,
            	const spec::abstract_def& spec = spec::dwarf3) 
            : spec(spec), hipc(hipc), lopc(lopc)
			{
				initialize_from_opcode_array(&arr[0], &arr[s], lopc, hipc, spec);
			}
			
			template <class In>
			loc_expr(In begin, In end, Dwarf_Addr lopc, Dwarf_Addr hipc,
				const spec::abstract_def& spec = spec::dwarf3) 
			: spec(spec), hipc(hipc), lopc(lopc)
			{
				initialize_from_opcode_array(begin, end, lopc, hipc, spec);
			}
		private:
			template <class In>
			void initialize_from_opcode_array(In begin, In end,
				Dwarf_Addr lopc, Dwarf_Addr hipc,
				const spec::abstract_def& spec) 
			{
				//size_t s = end - begin;
				auto iter = begin; // &arr[0];
				Dwarf_Unsigned next_offset = 0U;
				while (iter < /* arr + s */ end)
				{
					Dwarf_Loc loc;
					loc.lr_offset = next_offset;
					loc.lr_atom = *iter++; // read opcode
					next_offset += 1; // opcodes are one byte
					switch (spec.op_operand_count(loc.lr_atom))
					{
						case 2:
							loc.lr_number = *iter++;
							loc.lr_number2 = *iter++;
							// how many bytes of DWARF binary encoding?
							next_offset += form_encoded_size(
								spec.op_operand_form_list(loc.lr_atom)[0]
							);
							next_offset += form_encoded_size(
								spec.op_operand_form_list(loc.lr_atom)[1]
							);						
							break;
						case 1:
							loc.lr_number = *iter++;
							// how many bytes of DWARF binary encoding?
							next_offset += form_encoded_size(
								spec.op_operand_form_list(loc.lr_atom)[0]
							);
							break;
						case 0:
							break;
						default: assert(false);
					}
					/*m_expr.*/push_back(loc);
				}
			}
		public:
			bool operator==(const loc_expr& e) const 
			{ 
				//expr_instr e1; expr_instr e2;
				return hipc == e.hipc &&
					lopc == e.lopc &&
					//e1 == e2;
					static_cast<const std::vector<expr_instr> *>(this)
                    == static_cast<const std::vector<expr_instr> *>(&e);
			}
			bool operator!=(const loc_expr& e) const { return !(*this == e); }
            loc_expr& operator=(const loc_expr& e) 
            { 
                assert(&(this->spec) == &(e.spec)); // references aren't assignable
                *static_cast<std::vector<expr_instr> *>(this) = *static_cast<const std::vector<expr_instr> *>(&e);
                this->hipc = e.hipc;
                this->lopc = e.lopc;
                return *this;
            }
			friend std::ostream& operator<<(std::ostream& s, const loc_expr& e);
		};
		std::ostream& operator<<(std::ostream& s, const loc_expr& e);
		
		struct loclist : public std::vector<loc_expr>
		{
			friend class ::dwarf::lib::evaluator;
			friend class attribute_value;
			loclist(const dwarf::lib::loclist& dll)
			{
				for (int i = 0; i != dll.len(); i++)
				{
					push_back(loc_expr(dll[i])); 
				}		
			}
			// would ideally repeat all vector constructors
			template <class In> loclist(In first, In last) : std::vector<loc_expr>(first, last) {}
			loclist(const std::vector<loc_expr>& v) : std::vector<loc_expr>(v) {}
			loclist(const loc_expr& loc) : std::vector<loc_expr>(1, loc) {}
			//bool operator==(const loclist& oll) const { return *this == oll; }
			//bool operator!=(const loclist& oll) const { return !(*this == oll); }
			//friend std::ostream& operator<<(std::ostream& s, const ::dwarf::encap::loclist& ll);
            loc_expr loc_for_vaddr(Dwarf_Addr vaddr) const;
		};
		std::ostream& operator<<(std::ostream& s, const ::dwarf::encap::loclist& ll);	
	}    
}

#endif
