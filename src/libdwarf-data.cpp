/* dwarfpp: C++ binding for a useful subset of libdwarf, plus extra goodies.
 * 
 * libdwarf-data.cpp: operator overloads etc. on libdwarf data
 *
 * Copyright (c) 2008--17, Stephen Kell. For licensing information, see the
 * LICENSE file in the root of the libdwarfpp tree.
 */

#include "dwarfpp/libdwarf.hpp"
#include "dwarfpp/abstract.hpp"
#include "dwarfpp/abstract-inl.hpp"
#include "dwarfpp/attr.hpp"
#include "dwarfpp/expr.hpp"

#include <sstream>

namespace dwarf
{
	namespace lib
	{
		bool operator==(const Dwarf_Ranges& e1, const Dwarf_Ranges& e2)
		{
			return e1.dwr_addr1 == e2.dwr_addr1
				&& e1.dwr_addr2 == e2.dwr_addr2
				&& e1.dwr_type == e2.dwr_type;
		}
		bool operator!=(const Dwarf_Ranges& e1, const Dwarf_Ranges& e2)
		{
			return !(e1 == e2);
		}
		std::ostream& operator<<(std::ostream& s, const Dwarf_Ranges& rl)
		{
			switch (rl.dwr_type)
			{
				case DW_RANGES_ENTRY:
					s << "[0x" << std::hex << rl.dwr_addr1 
						<< ", 0x" << std::hex << rl.dwr_addr2 << ")";
				break;
				case DW_RANGES_ADDRESS_SELECTION:
					assert(rl.dwr_addr1 == 0xffffffff || rl.dwr_addr1 == 0xffffffffffffffffULL);
					s << "set base 0x" << std::hex << rl.dwr_addr2;
				break;
				case DW_RANGES_END:
					assert(rl.dwr_addr1 == 0 && rl.dwr_addr2 == 0);
					s << "end";
				break;
				default: assert(false); break;
			}
			return s;
		}
		std::ostream& operator<<(std::ostream& s, const Dwarf_Locdesc& ld)
		{
			s << dwarf::encap::loc_expr(ld);
			return s;
		}	
		std::ostream& operator<<(std::ostream& s, const Dwarf_Loc& l)
		{
			// HACK: we can't infer the DWARF standard from the Dwarf_Loc we've been passed,
			// so use the default.
			s << "0x" << std::hex << l.lr_offset << std::dec
				<< ": " << dwarf::spec::DEFAULT_DWARF_SPEC.op_lookup(l.lr_atom);
			std::ostringstream buf;
			std::string to_append;

			switch (dwarf::spec::DEFAULT_DWARF_SPEC.op_operand_count(l.lr_atom))
			{
				case 2:
					buf << ", " << dwarf::encap::attribute_value(
						l.lr_number2, 
						dwarf::spec::DEFAULT_DWARF_SPEC.op_operand_form_list(l.lr_atom)[1]
					);
					to_append += buf.str();
				case 1:
					buf.clear();
					buf << "(" << dwarf::encap::attribute_value(
						l.lr_number, 
						dwarf::spec::DEFAULT_DWARF_SPEC.op_operand_form_list(l.lr_atom)[0]
					);
					to_append.insert(0, buf.str());
					to_append += ")";
				case 0:
					s << to_append;
					break;
				default: s << "(unexpected number of operands) ";
			}
			s << ";";
			return s;
		}
		bool operator<(const Dwarf_Loc& arg1, const Dwarf_Loc& arg2)
		{
			return arg1.lr_atom <  arg2.lr_atom
			||     (arg1.lr_atom == arg2.lr_atom && arg1.lr_number <  arg2.lr_number)
			||     (arg1.lr_atom == arg2.lr_atom && arg1.lr_number ==  arg2.lr_number && arg1.lr_number2 <  arg2.lr_number2)
			||     (arg1.lr_atom == arg2.lr_atom && arg1.lr_number ==  arg2.lr_number && arg1.lr_number2 == arg2.lr_number2 && 
				arg1.lr_offset < arg2.lr_offset); 
		}
		bool operator==(const Dwarf_Loc& e1, const Dwarf_Loc& e2)
		{
			return e1.lr_atom == e2.lr_atom
				&& e1.lr_number == e2.lr_number
				&& e1.lr_number2 == e2.lr_number2
				&& e1.lr_offset == e2.lr_offset;
		}
		bool operator!=(const Dwarf_Loc& e1, const Dwarf_Loc& e2)
		{
			return !(e1 == e2);
		}
	}
}
