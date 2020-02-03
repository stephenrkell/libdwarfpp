/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * expr.cpp: basic C++ abstraction of DWARF expressions.
 *
 * Copyright (c) 2010--17, Stephen Kell. For licensing information, see the
 * LICENSE file in the root of the libdwarfpp tree.
 */

#include <limits>
#include <map>
#include <set>
#include <srk31/endian.hpp>

#include "abstract.hpp"
#include "abstract-inl.hpp"
#include "expr.hpp"
#include "dwarfpp/root.hpp"
#include "dwarfpp/root-inl.hpp"
#include "dwarfpp/iter.hpp"
#include "dwarfpp/iter-inl.hpp"
#include "dwarfpp/dies.hpp"
#include "dwarfpp/dies-inl.hpp"

using std::map;
using std::pair;
using std::make_pair;
using std::set;
using std::string;
using std::endl;
using dwarf::spec::opt;

namespace dwarf
{
	namespace expr
	{
		using namespace dwarf::lib;
		using core::debug;
		
		evaluator::evaluator(const encap::loclist& loclist,
			Dwarf_Addr vaddr,
			const ::dwarf::spec::abstract_def& spec,
			regs *p_regs,
			opt<Dwarf_Signed> frame_base,
			const std::stack<Dwarf_Unsigned>& initial_stack)
		: m_stack(initial_stack), spec(spec), p_regs(p_regs), tos_state(ADDRESS), frame_base(frame_base)
		{
			// sanity check while I suspect stack corruption
			assert(vaddr < 0x00008000000000ULL
			|| 	vaddr == 0xffffffffULL
			||  vaddr == 0xffffffffffffffffULL);
			
			i = expr.begin();
			Dwarf_Addr current_vaddr_base = 0; // relative to CU "applicable base" (Dwarf 3 sec 3.1)
			/* Search through loc expressions for the one that matches vaddr. */
			for (auto i_loc_expr = loclist.begin();
					i_loc_expr != loclist.end();
					++i_loc_expr)
			{
				/* HACK: we should instead use address_size as reported by next_cu_header,
				 * lifting it to a get_address_size() method in spec::compile_unit_die. */
				// Dwarf_Addr magic_addr = 
					
				if (i_loc_expr->lopc == 0xffffffffU
				||  i_loc_expr->lopc == 0xffffffffffffffffULL)
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
			
			/* Dump something about the vaddr. */
			debug(2) << "Vaddr 0x" << std::hex << vaddr << std::dec
				<< " is not covered by any loc expr in " << loclist << endl;
			throw No_entry();
		}
		
		
		void evaluator::eval()
		{
			if (i != expr.end() && i != expr.begin())
			{
				/* This happens when we stopped at a DW_OP_piece argument. 
				 * Advance the opcode iterator and clear the stack. */
				++i;
				while (!m_stack.empty()) m_stack.pop();
			}
			opt<std::string> error_detail;
			while (i != expr.end())
			{
				// FIXME: be more descriminate -- do we want to propagate valueness? probably not
				tos_state = ADDRESS;
				switch(i->lr_atom)
				{
					case DW_OP_const1u:
					case DW_OP_const2u:
					case DW_OP_const4u:
					case DW_OP_const8u:
					case DW_OP_constu:
						m_stack.push(i->lr_number);
						break;
					case DW_OP_const1s:
					case DW_OP_const2s:
					case DW_OP_const4s:
					case DW_OP_const8s:
					case DW_OP_consts:
						m_stack.push((Dwarf_Signed) i->lr_number);
						break;
				   case DW_OP_plus_uconst: {
						int tos = m_stack.top();
						m_stack.pop();
						m_stack.push(tos + i->lr_number);
					} break;
					case DW_OP_plus: {
						int arg1 = m_stack.top(); m_stack.pop();
						int arg2 = m_stack.top(); m_stack.pop();
						m_stack.push(arg1 + arg2);
					} break;
					case DW_OP_shl: {
						int arg1 = m_stack.top(); m_stack.pop();
						int arg2 = m_stack.top(); m_stack.pop();
						m_stack.push(arg2 << arg1);
					} break;
					case DW_OP_shr: {
						int arg1 = m_stack.top(); m_stack.pop();
						int arg2 = m_stack.top(); m_stack.pop();
						m_stack.push((int)((unsigned) arg2 >> arg1));
					} break;
					case DW_OP_shra: {
						int arg1 = m_stack.top(); m_stack.pop();
						int arg2 = m_stack.top(); m_stack.pop();
						m_stack.push(arg2 >> arg1);
					} break;
					case DW_OP_fbreg: {
						if (!frame_base) goto logic_error;
						m_stack.push(*frame_base + i->lr_number);
					} break;
					case DW_OP_call_frame_cfa: {
						if (!frame_base) goto logic_error;
						m_stack.push(*frame_base);
					} break;
					case DW_OP_piece: {
						/* Here we do something special: leave the opcode iterator
						 * pointing at the piece argument, and return. This allow us
						 * to probe the piece size (by getting *i) and to resume by
						 * calling eval() again. */
						 ++i;
					}	return;
					case DW_OP_breg0:
					case DW_OP_breg1:
					case DW_OP_breg2:
					case DW_OP_breg3:
					case DW_OP_breg4:
					case DW_OP_breg5:
					case DW_OP_breg6:
					case DW_OP_breg7:
					case DW_OP_breg8:
					case DW_OP_breg9:
					case DW_OP_breg10:
					case DW_OP_breg11:
					case DW_OP_breg12:
					case DW_OP_breg13:
					case DW_OP_breg14:
					case DW_OP_breg15:
					case DW_OP_breg16:
					case DW_OP_breg17:
					case DW_OP_breg18:
					case DW_OP_breg19:
					case DW_OP_breg20:
					case DW_OP_breg21:
					case DW_OP_breg22:
					case DW_OP_breg23:
					case DW_OP_breg24:
					case DW_OP_breg25:
					case DW_OP_breg26:
					case DW_OP_breg27:
					case DW_OP_breg28:
					case DW_OP_breg29:
					case DW_OP_breg30:
					case DW_OP_breg31:
					{
						/* the breg family get the contents of a register and add an offset */ 
						if (!p_regs) goto no_regs;
						int regnum = i->lr_atom - DW_OP_breg0;
						m_stack.push(p_regs->get(regnum) + i->lr_number);
					} break;
					case DW_OP_addr:
					{
						m_stack.push(i->lr_number);
					} break;
					case DW_OP_reg0:
					case DW_OP_reg1:
					case DW_OP_reg2:
					case DW_OP_reg3:
					case DW_OP_reg4:
					case DW_OP_reg5:
					case DW_OP_reg6:
					case DW_OP_reg7:
					case DW_OP_reg8:
					case DW_OP_reg9:
					case DW_OP_reg10:
					case DW_OP_reg11:
					case DW_OP_reg12:
					case DW_OP_reg13:
					case DW_OP_reg14:
					case DW_OP_reg15:
					case DW_OP_reg16:
					case DW_OP_reg17:
					case DW_OP_reg18:
					case DW_OP_reg19:
					case DW_OP_reg20:
					case DW_OP_reg21:
					case DW_OP_reg22:
					case DW_OP_reg23:
					case DW_OP_reg24:
					case DW_OP_reg25:
					case DW_OP_reg26:
					case DW_OP_reg27:
					case DW_OP_reg28:
					case DW_OP_reg29:
					case DW_OP_reg30:
					case DW_OP_reg31:
					{
						/* the reg family just get the contents of the register */
						if (!p_regs) goto no_regs;
						int regnum = i->lr_atom - DW_OP_reg0;
						m_stack.push(p_regs->get(regnum));
					} break;
					case DW_OP_lit0:
					case DW_OP_lit1:
					case DW_OP_lit2:
					case DW_OP_lit3:
					case DW_OP_lit4:
					case DW_OP_lit5:
					case DW_OP_lit6:
					case DW_OP_lit7:
					case DW_OP_lit8:
					case DW_OP_lit9:
					case DW_OP_lit10:
					case DW_OP_lit11:
					case DW_OP_lit12:
					case DW_OP_lit13:
					case DW_OP_lit14:
					case DW_OP_lit15:
					case DW_OP_lit16:
					case DW_OP_lit17:
					case DW_OP_lit18:
					case DW_OP_lit19:
					case DW_OP_lit20:
					case DW_OP_lit21:
					case DW_OP_lit22:
					case DW_OP_lit23:
					case DW_OP_lit24:
					case DW_OP_lit25:
					case DW_OP_lit26:
					case DW_OP_lit27:
					case DW_OP_lit28:
					case DW_OP_lit29:
					case DW_OP_lit30:
					case DW_OP_lit31:
						m_stack.push(i->lr_atom - DW_OP_lit0);
						break;
					case DW_OP_stack_value:
						/* This means that the object has no address, but that the 
						 * DWARF evaluator has just computed its *value*. We record
						 * this. */
						tos_state = VALUE;
						break;
#ifdef DW_OP_implicit_pointer
					case DW_OP_implicit_pointer:
#endif
					case DW_OP_GNU_implicit_pointer:
						/* Two operands: a reference to the debugging information entry
						 * that describes the dereferenced object's value, and a signed
						 * number that is treated as a byte offset from the start of that
						 * object.
						 * Since the evaluator is asked to produce an address, which by
						 * definition an "implicit pointer" is not, we simply have to
						 * record the state that is in the instruction. */
						tos_state = IMPLICIT_POINTER;
						implicit_pointer = make_pair(i->lr_number,
							static_cast<Dwarf_Signed>(i->lr_number2));
						break;
					case DW_OP_deref_size:
					case DW_OP_deref:
						/* FIXME: we can do this one if we have p_mem analogous to p_regs. */
						throw No_entry();
					default:
						debug() << "Error: unrecognised opcode: " << spec.op_lookup(i->lr_atom) << std::endl;
						throw Not_supported("unrecognised opcode");
					no_regs:
						debug() << "Warning: asked to evaluate register-dependent expression with no registers." << std::endl;
						throw No_entry();
					logic_error:
						debug() << "Logic error in DWARF expression evaluator";
						if (error_detail) debug() << ": " << *error_detail;
						debug() << std::endl;
						assert(false);
						throw Not_supported(error_detail ? *error_detail : "unknown");
				}
			i++;
			}
		}
		Dwarf_Unsigned eval(const encap::loclist& loclist,
			Dwarf_Addr vaddr,
			Dwarf_Signed frame_base,
			opt<regs&> regs,
			const ::dwarf::spec::abstract_def& spec,
			const std::stack<Dwarf_Unsigned>& initial_stack)
		{
			assert(false); return 0UL;
		}
	}
	namespace encap
	{
		using namespace dwarf::lib;
		using namespace dwarf::expr;
		using core::debug;
		loclist loclist::NO_LOCATION;

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
			for (std::vector<Dwarf_Loc>::const_iterator i = e.begin(); i != e.end(); i++)
			{
				s << *i << " ";
			}
			s << "} (for ";
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
		loc_expr::loc_expr(Dwarf_Debug dbg, lib::Dwarf_Ptr instrs, lib::Dwarf_Unsigned len, const spec::abstract_def& spec /* = spec::dwarf_current */)
			: spec(spec)
		{
			auto loc = core::Locdesc::try_construct(dbg, instrs, len);
			assert(loc);
			core::Locdesc ld(std::move(loc));
			*static_cast<vector<expr_instr> *>(this) = vector<expr_instr>(ld.raw_handle()->ld_s, ld.raw_handle()->ld_s + ld.raw_handle()->ld_cents);
			this->hipc = ld.raw_handle()->ld_hipc;
			this->lopc = ld.raw_handle()->ld_lopc;
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
			for (auto i_instr = /*m_expr.*/begin(); i_instr != /*m_expr.*/end(); ++i_instr)
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
		loclist::loclist(const core::LocList& ll)
		{
			for (auto i = ll.copied_list.begin(); i != ll.copied_list.end(); ++i)
			{
				push_back(*i->get());
			}
		}
		loclist::loclist(const core::Locdesc& l)
		{
			push_back(*l.handle.get());
		}
		rangelist::rangelist(const core::RangeList& rl)
		{
			for (unsigned i = 0; i < rl.handle.get_deleter().len; ++i)
			{
				push_back(rl.handle.get()[i]);
			}
		}
		loc_expr loclist::loc_for_vaddr(Dwarf_Addr vaddr) const
		{
			for (auto i = this->begin(); i != this->end(); i++)
			{
				if (vaddr >= i->lopc && vaddr < i->hipc) return *i;
			}
			throw No_entry(); // bogus vaddr
		}
		
		// FIXME: what was the point of this method? It's some kind of normalisation
		// so that everything takes the form of adding to a pre-pushed base address.
		// But why? Who needs it?
		loclist absolute_loclist_to_additive_loclist(const loclist& l)
		{
			/* Total HACK, for now: just rewrite DW_OP_fbreg to { DW_OP_consts(n), DW_OP_plus },
			 * i.e. assume the stack pointer is already pushed. */
			loclist new_ll = l;
			for (auto i_l = new_ll.begin(); i_l != new_ll.end(); ++i_l)
			{
				for (auto i_instr = i_l->begin(); i_instr != i_l->end(); ++i_instr)
				{
					if (i_instr->lr_atom == DW_OP_fbreg)
					{
						i_instr->lr_atom = DW_OP_consts; // leave argument the same
						// skip i_instr along one (might now be at end())
						++i_instr;
						// insert the plus, and leave i_instr pointing at it
						i_instr = i_l->insert(i_instr, (Dwarf_Loc) { .lr_atom = DW_OP_plus });
					}
				}
			}
			return new_ll;
		}
// 		boost::icl::interval_map<Dwarf_Addr, vector<expr_instr> > 
// 		loclist::as_interval_map() const
// 		{
// 			boost::icl::interval_map<Dwarf_Addr, vector<expr_instr> > working;
// 			for (auto i_loc_expr = begin(); i_loc_expr != end(); ++i_loc_expr)
// 			{
// 				auto interval = boost::icl::discrete_interval<Dwarf_Addr>::right_open(
// 						i_loc_expr->lopc,
// 						i_loc_expr->hipc
// 					);
// 				working += make_pair(
// 					interval,
// 					*i_loc_expr
// 				);
// 			}
// 			return working;
// 		}
	}
}
