/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * expr.cpp: basic C++ abstraction of DWARF expressions.
 *
 * Copyright (c) 2010, Stephen Kell.
 */

#include <limits>

#include "expr.hpp" 

namespace dwarf
{
	namespace lib
    {
		evaluator::evaluator(const encap::loclist& loclist,
        	Dwarf_Addr vaddr,
	        const ::dwarf::spec::abstract_def& spec,
            //Dwarf_Off offset_into_object,
            regs *p_regs,
            boost::optional<Dwarf_Signed> frame_base,
            const std::stack<Dwarf_Unsigned>& initial_stack)
        : m_stack(initial_stack), spec(spec), p_regs(p_regs), frame_base(frame_base)
		{
        	i = expr.begin();
			Dwarf_Addr current_vaddr_base = 0; // relative to CU "applicable base" (Dwarf 3 sec 3.1)
        	/* Search through loc expressions for the one that matches vaddr. */
            for (auto i_loc_expr = loclist.begin();
        		    i_loc_expr != loclist.end();
                    i_loc_expr++)
            {
				/* HACK: we should instead use address_size as reported by next_cu_header,
				 * lifting it to a get_address_size() method in spec::compile_unit_die. */
				// Dwarf_Addr magic_addr = 
					
				if (i_loc_expr->lopc == 0xffffffffU
				||  i_loc_expr->lopc == 0xffffffffffffffffUL)
				{
					/* This is a "base address selection entry". */
					current_vaddr_base = i_loc_expr->hipc;
					continue;
				}
			
				/* According to the libdwarf manual, 
				 * lopc == 0 and hipc == 0 means "for all vaddrs".
				 * I seem to have been using 
				 * 0..std::numeric_limits<Dwarf_Addr>::max() for this.
				 * For now, allow both. */
			
        	    if ((i_loc_expr->lopc == 0 && // this kind of loc_expr covers all vaddrs
                	i_loc_expr->hipc == std::numeric_limits<Dwarf_Addr>::max())
				|| (i_loc_expr->lopc == 0 && i_loc_expr->hipc == 0)
                || (vaddr >= i_loc_expr->lopc + current_vaddr_base
            	    && vaddr < i_loc_expr->hipc + current_vaddr_base))
                {
            	    expr = *i_loc_expr/*->m_expr*/;
         			i = expr.begin();
                    eval();
                    return;
                }
            }			
        	throw No_entry();
		} 
    }
	namespace encap
    {
		std::ostream& operator<<(std::ostream& s, const ::dwarf::encap::loclist& ll)
		{
			s << "(loclist) {";
			for (::dwarf::encap::loclist::const_iterator i = ll.begin(); i != ll.end(); i++)
			{
				if (i != ll.begin()) s << ", ";
				s << *i;
			}
			s << "}";
			return s;
		}
		std::ostream& operator<<(std::ostream& s, const loc_expr& e)
		{
			s << "loc described by { ";
			for (std::vector<Dwarf_Loc>::const_iterator i = e./*m_expr.*/begin(); i != e./*m_expr.*/end(); i++)
			{
				s << *i;
			}
			s << " } (for ";
			if (e.lopc == 0 && e.hipc == std::numeric_limits<Dwarf_Addr>::max()) // FIXME
			{
				s << "all vaddrs";
			}
			else
			{
				s << "vaddr 0x"
					<< std::hex << e.lopc << std::dec
					<< "..0x"
					<< std::hex << e.hipc << std::dec;
			}
			s << ")";
			return s;
		}		
        std::vector<std::pair<loc_expr, Dwarf_Unsigned> > loc_expr::pieces() const
        {
            /* Split the loc_expr into pieces, and return pairs
             * of the subexpr and the length of the object segment
             * that this expression locates (or 0 for "whole object"). 
             * Note that pieces may
             * not be nested (DWARF 3 Sec. 2.6.4, read carefully). */
            std::vector<std::pair<loc_expr, Dwarf_Unsigned> > ps;

            std::vector<expr_instr>::const_iterator done_up_to_here = /*m_expr.*/begin();
            Dwarf_Unsigned done_this_many_bytes = 0UL;
            for (auto i_instr = /*m_expr.*/begin(); i_instr != /*m_expr.*/end(); i_instr++)
            {
                if (i_instr->lr_atom == DW_OP_piece) 
                {
                    ps.push_back(std::make_pair(
                    	loc_expr(/*std::vector<expr_instr>(*/done_up_to_here, i_instr/*)*/, spec),
	                    i_instr->lr_number));
    	            done_this_many_bytes += i_instr->lr_number;
        	        done_up_to_here = i_instr + 1;
                }
                assert(i_instr->lr_atom != DW_OP_bit_piece); // FIXME: not done bit_piece yet
            }
            // if we did any DW_OP_pieces, we should have finished on one
            assert(done_up_to_here == /*m_expr.*/end() || done_this_many_bytes == 0UL);
            // if we didn't finish on one, we need to add a singleton to the pieces vector
            if (done_this_many_bytes == 0UL) ps.push_back(
                std::make_pair(
                    	loc_expr(*this), 0));
            return ps;
        }
        loc_expr loc_expr::piece_for_offset(Dwarf_Off offset) const
        {
        	auto ps = pieces();
            Dwarf_Off cur_off = 0UL;
            for (auto i = ps.begin(); i != ps.end(); i++)
            {
            	if (i->second == 0UL) return i->first;
                else if (offset >= cur_off && offset < cur_off + i->second) return i->first;
                else cur_off += i->second;
            }
            throw No_entry(); // no piece covers that offset -- likely a bogus offset
        }
        loc_expr loclist::loc_for_vaddr(Dwarf_Addr vaddr) const
        {
        	for (auto i = this->begin(); i != this->end(); i++)
            {
            	if (vaddr >= i->lopc && vaddr < i->hipc) return *i;
            }
            throw No_entry(); // bogus vaddr
        }
    }
}
