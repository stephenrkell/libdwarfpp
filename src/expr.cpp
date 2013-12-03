/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * expr.cpp: basic C++ abstraction of DWARF expressions.
 *
 * Copyright (c) 2010, Stephen Kell.
 */

#include <limits>
#include <map>
#include <set>
#include <boost/optional.hpp>
#include <boost/icl/interval_map.hpp>

#include "lib.hpp"
#include "expr.hpp" 

using std::map;
using std::pair;
using std::make_pair;
using std::set;
using std::string;
using std::cerr;
using std::endl;
using dwarf::core::FrameSection;
using dwarf::spec::opt;
using boost::icl::interval;

namespace dwarf
{
	namespace lib
	{
		evaluator::evaluator(const encap::loclist& loclist,
			Dwarf_Addr vaddr,
			const ::dwarf::spec::abstract_def& spec,
			regs *p_regs,
			boost::optional<Dwarf_Signed> frame_base,
			const std::stack<Dwarf_Unsigned>& initial_stack)
		: m_stack(initial_stack), spec(spec), p_regs(p_regs), frame_base(frame_base)
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
			cerr << "Vaddr 0x" << std::hex << vaddr << std::dec
				<< " is not covered by any loc expr in " << loclist << endl;
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
		loclist::loclist(const dwarf::lib::loclist& dll)
		{
			for (int i = 0; i != dll.len(); i++)
			{
				push_back(loc_expr(dll[i])); 
			}
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
		loclist absolute_loclist_to_additive_loclist(const loclist& l)
		{
			/* Total HACK, for now: just rewrite DW_OP_fbreg to DW_OP_plus_uconst. */
			loclist new_ll = l;
			for (auto i_l = new_ll.begin(); i_l != new_ll.end(); ++i_l)
			{
				for (auto i_instr = i_l->begin(); i_instr != i_l->end(); ++i_instr)
				{
					if (i_instr->lr_atom == DW_OP_fbreg)
					{
						i_instr->lr_atom = DW_OP_plus_uconst;
					}
				}
			}
			return new_ll;
		}
		
		// FIXME temporary helper
		static
		boost::icl::interval_map<Dwarf_Addr, map<int /* regnum */, pair< int /* == regnum */, int /* + offset */ > > >
		decode_fde(Dwarf_Fde fde)
		{
			return boost::icl::interval_map<Dwarf_Addr, map<int /* regnum */, pair< int /* == regnum */, int /* + offset */ > > >(); 
		}
		loclist rewrite_loclist_in_terms_of_cfa(
			const loclist& l, 
			const FrameSection& fs, 
			const boost::icl::interval_map<Dwarf_Addr, Dwarf_Unsigned>& containing_intervals,
			dwarf::spec::opt<const loclist&> opt_fbreg // fbreg is special -- loc exprs can refer to it
			)
		{
			/* First
			
			 * - compute a map from vaddrs to CFA expressions of the form (reg + offset). 
			 * - for some vaddrs, CFA might not expressible this way 
			 
			 * Then for each vaddr range in the locexpr
			 
			 * - note any breg(n) opcodes
			 * - see if we can compute them from CFA instead
			 * - if so, rewrite them as a { cfa, push, plus } operation
			 
			 * HMM. In general we seem to be building a constraint graph
			 * s.t. two nodes (n1, n2) are connected by an edge labelled k
			 * if n2 == n1 + k.
			 *
			 * NOTE that every edge has an opposite-direction edge whose weight
			 * is the negation of the first weight.
			 
			 * NOTE that in general, this graph changes with each instruction. 
			 * So what we are labelling the intervals with is really the edge set.
			 
			 * Can we relate all registers this way, including cfa as a pseudo-reg, 
			 * then look for a path from cfa to the referenced register?
			 
			 * YES, this is a nice formulation.
			 
			 * We also add the loc expr of interest itself, as another node, from which
			 * we will try to find paths to the CFA.
			 
			 * We still have to collapse identical vaddr ranges at the end, because edge
			 * sets refer to *all* regs and we only care about one (the loc expr).
			 
			 * What about fbreg? It is just another node (with definition providing the edges)
			 */
			
			struct edge
			{
				int from_reg;
				int to_reg;
				int difference;
				
				edge(const pair<const int /* regnum */, pair< int /* == regnum */, int /* + offset */ > >& map_entry)
				{
					from_reg = map_entry.first;
					to_reg = map_entry.second.first;
					difference = map_entry.second.second;
				}
				
				bool operator<(const edge& e) const
				{
					return make_pair(from_reg, make_pair(to_reg, difference)) < make_pair(e.from_reg, make_pair(e.to_reg, e.difference));
				}
				
				bool operator!=(const edge& e) const
				{
					return e < *this || *this < e;
				}
				bool operator==(const edge& e) const { return !(*this != e); }
			};
			
			boost::icl::interval_map<Dwarf_Addr, set< edge > > edges;
			
			/* Walk our FDEs starting from the lowest addr in the interval. */
			
			Dwarf_Fde current = 0;
			Dwarf_Addr hipc = 0;
			for (auto i_int = containing_intervals.begin(); i_int != containing_intervals.end(); ++i_int)
			{
				assert((hipc == 0  && current == 0) || hipc > i_int->first.lower());
				Dwarf_Addr lopc;
				
				// walk all FDEs that overlap this interval
				
				if (current == 0)
				{
					// we don't have a FDE that overlaps this interval
					Dwarf_Addr lopc;
					int ret = dwarf_get_fde_at_pc(fs.handle.get_deleter().fde_data, i_int->first.lower(), &current, &lopc, &hipc, &core::current_dwarf_error);
					assert(ret == DW_DLV_OK);
				}
				
				// while there is some overlap with our interval
				while (lopc < i_int->first.upper() && hipc > i_int->first.lower())
				{
					// decode the table into rows
					// FIXME: instead of this naive map, need to encode that a reg might be defined by a DWARF expr...
					boost::icl::interval_map<Dwarf_Addr, map<int /* regnum */, pair< int /* == regnum */, int /* + offset */ > > > rows
					 = decode_fde(current);
					
					// process each row
					for (auto i_row = rows.begin(); i_row != rows.end(); ++i_row)
					{
						// add an entry to our interval map
						edges += make_pair(
								interval<Dwarf_Addr>::right_open(
									/* intersection of this interval and the *row*'s (not FDE's) interval */
									std::max(lopc, i_int->first.lower()), 
									std::min(hipc, i_int->first.upper())
								), 
								/* set of edge definitions in this row */
								std::set<edge>(i_row->second.begin(), i_row->second.end()) // i.e. pair<int, pair<int, int> > i.e. (src, dst, weight)
							);
					}
					
					// get the next FDE
					int ret = dwarf_get_fde_at_pc(fs.handle.get_deleter().fde_data, hipc, &current, &lopc, &hipc, &core::current_dwarf_error);
					assert(ret == DW_DLV_OK);
				}
				
				// leave 'current' since it might be useful on the next iteration
				
			} // end for interval

			// FIXME: now do the rewrites and coalesce

			return l; // FIXME
		}
		
		loclist loclist::NO_LOCATION;
	}
}
