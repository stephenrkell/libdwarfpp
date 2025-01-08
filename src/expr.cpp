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
			const evaluator::eval_stack& initial_stack)
		: m_stack(initial_stack), spec(spec), p_regs(p_regs), m_tos_state(ADDRESS), frame_base(frame_base)
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
			std::ostringstream s;
			s << expr << std::endl;
#if 0
			std::cerr << "Evaluating '" << s.str() << "' with initial stack size " << m_stack.size()
				<< std::endl;
#endif
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
				switch(i->lr_atom)
				{

/* REMEMBER: we've subclassed std::stack to define operator[] with [0] == tos, etc. */
/* REMEMBER: we can't have both morestack andlessstack in the same scope,
 * since in dwarf-machine.hpp they both define a new variable 'end'. */
#define stk m_stack
#define Operand1   i->lr_number
#define Operand2   i->lr_number2
#define morestack(nwords) \
     for (unsigned n_ = 0; n_ < (nwords); ++n_) { /* FIXME: too indiscriminate*/ m_tos_state = ADDRESS; m_stack.push(0); }
#define lessstack(nwords) \
     for (unsigned n_ = 0; n_ < (nwords); ++n_)  { /* FIXME: too indiscriminate*/ m_tos_state = ADDRESS; m_stack.pop(); }
#define PUSH(x)     morestack(1); stk[0] = (x)
#define POP(y)      Dwarf_Unsigned y = stk[0]; lessstack(1)
/* intrinsics */
#define FBREG          ({ if (!frame_base) goto logic_error; *frame_base; })
#define REGS(n)        ({ if (!p_regs) goto no_regs; p_regs->get(n); })
#define LOADN(n, addr) ({ /* FIXME: need p_mem like p_regs */ throw No_entry(); 0; })
#define LOADN3(n, addr, asid) ({ /* FIXME: need p_mem like p_regs *and* asid */ throw No_entry(); 0; })

#define computed_op_case(num, toks...) \
	case num : { toks } break;
dwarf_expr_computed_ops(computed_op_case)

					// v_(DW_OP_fbreg,   PUSH(((Dwarf_Unsigned) FBREG) + Operand1);)
					case DW_OP_fbreg: {
						if (!frame_base) goto logic_error;
						m_stack.push(*frame_base + i->lr_number);
					} break;
					// we could genetate this one with:
					// v_(DW_OP_call_frame_cfa,   PUSH(FBREG);)
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
					case DW_OP_reg0 ... DW_OP_reg31:
					{
						//int regnum = i->lr_atom - DW_OP_reg0;
						/* The reg family just get the contents of the register.
						 * HMM. I think this is wrong. At the very least, we should
						 * set the state to VALUE if we push the register contents
						 * onto the stack, to denote that we're no longer computing
						 * an address. But really, this is a new state:
						 * we've computed an "address" where memory addresses are
						 * extended with the domain of registers.  */
						//if (!p_regs) goto no_regs;
						//m_stack.push(p_regs->get(regnum));
						//m_stack.push(regnum);
						m_tos_state = NAMED_REGISTER;
					} break;
					case DW_OP_regx:
					{
						//int regnum = i->lr_number;
						//m_stack.push(regnum);
						m_tos_state = NAMED_REGISTER;
					} break;
					case DW_OP_stack_value:
						/* This means that the object has no address, but that the 
						 * DWARF evaluator has just computed its *value*. We record
						 * this. */
						m_tos_state = VALUE;
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
						m_tos_state = IMPLICIT_POINTER;
						implicit_pointer = make_pair(i->lr_number,
							static_cast<Dwarf_Signed>(i->lr_number2));
						break;
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
			const evaluator::stack_t& initial_stack)
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

		/* This is not const because the iterators we return will only be valid
		 * if the storage is nailed down, not a compiler temporary (to which const&
		 * could bind). */
		std::vector< loc_expr::piece > loc_expr::all_pieces()
		{
			/* Split the loc_expr into pieces, and return pairs
			 * of the subexpr and the length of the object segment
			 * that this expression locates (or 0 for "whole object"). 
			 * Note that pieces may
			 * not be nested (DWARF 3 Sec. 2.6.4, read carefully). */
			std::vector< piece > ps;

			std::vector<expr_instr>::iterator done_up_to_here = /*m_expr.*/begin();
			Dwarf_Unsigned done_this_many_bits = 0UL;
			for (auto i_instr = /*m_expr.*/begin(); i_instr != /*m_expr.*/end(); ++i_instr)
			{
				if (i_instr->lr_atom == DW_OP_piece
					|| i_instr->lr_atom == DW_OP_bit_piece)
				{
					ps.push_back(piece(
						done_up_to_here, i_instr, done_this_many_bits,
						/* implicit piece? */ false));
					/* FIXME: we are ignoring the second argument of DW_OP_bit_piece,
					 * and that's wrong. */
					done_this_many_bits += i_instr->lr_number
					 * ((i_instr->lr_atom == DW_OP_piece) ? 8 : 1);
					done_up_to_here = i_instr + 1;
				}
			}
			// if we did any pieces, we should have finished on one
			assert(done_up_to_here == /*m_expr.*/end() || done_this_many_bits == 0UL);
			// if we didn't finish on one, we need to add a singleton to the pieces vector
			if (done_this_many_bits == 0UL) ps.push_back(
				piece(this->begin(), this->end(), 0, /* implicit piece?*/ true));
			return ps;
		}
		loc_expr loc_expr::piece::copy() const
		{
			loc_expr out;
			for (auto i_e = first; i_e != second; ++i_e)
			{ out.push_back(*i_e); }
			return out;
		}

		std::vector<std::pair<loc_expr, Dwarf_Unsigned> > loc_expr::byte_pieces() const
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
		loc_expr loc_expr::piece_for_byte_offset(Dwarf_Off offset) const
		{
			auto ps = byte_pieces();
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
		
		/* This method is used in with_dynamic_location_die::get_dynamic_location()
		 * to provide uniformity between member_die and formal_parameter_die/variable_die:
		 * every location expression that denotes a memory
		 * location should take the form of adding to a pre-pushed base address.
		 * To do this, it rewrites DW_OP_fbreg to an addition.
		 * HOWEVER, is this sane?
		 * - If it occurs in the middle of the expression, the plus
		 * need not connect with the pre-pushed value, so I guess we should
		 * only rewrite a *leading* DW_OP_fbreg. Does a non-leading one ever occur?
		 * Or is there a way to get at the first-pushed stack element directly?
		 * - We also have rewrite_loclist_in_terms_of_cfa, which has comments
		 * saying 'fbreg is special' but never actually does anything about it.
		 * - Does rewriting break DW_OP_pick? Our operand stack has one more element
		 * than it used to. But no: the arg of DW_OP_pick is relative to the top
		 * of the stack, so we're safe.
		 * Can we merge these two? The CFA-based rewriting is much more invasive
		 * and requires a frame section.
		 * Can we flip the normalisation, so that we make member_dies (and
		 * inheritance_dies and maybe others?) have absolute location? That could be
		 * as simple as prepending DW_OP_fbreg and defining the frame base as the
		 * object base. */
		loclist absolute_loclist_to_additive_loclist(const loclist& l)
		{
			/* Total HACK, for now: just rewrite DW_OP_fbreg
			 * to { DW_OP_consts(n), DW_OP_plus },
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
