#include <fstream>
#include <sstream>
#include <iomanip>
#include <fileno.hpp>
#include <dwarfpp/lib.hpp>
#include <dwarfpp/expr.hpp>
#include <dwarfpp/regs.hpp>
#include <gelf.h>

using std::cout; 
using std::endl;
using std::ostringstream;
using std::setw;
using std::setfill;
using std::map;
using namespace dwarf;
using dwarf::lib::Dwarf_Addr;
using dwarf::encap::register_def;
using dwarf::encap::decode_fde;

void print_in_readelf_style(std::ostream& s, const core::FrameSection& fs, core::root_die& r);
void print_in_readelf_style(std::ostream& s, const encap::loc_expr& expr);
void print_in_readelf_style(std::ostream& s, const encap::frame_instrlist& instrs, Dwarf_Addr initial_loc);
int elf_machine;
lib::Dwarf_Debug dbg;

int main(int argc, char **argv)
{
	cout << "Opening " << argv[0] << "..." << endl;
	std::ifstream in(argv[0]);
	core::root_die root(fileno(in));
	
	dbg = root.get_dbg().raw_handle();
	GElf_Ehdr ehdr;
	GElf_Ehdr *ret = gelf_getehdr(root.get_elf(), &ehdr);
	assert(ret != 0);
	elf_machine = ehdr.e_machine;
	
	core::FrameSection fs(root.get_dbg(), true);
	
	ostringstream s;
	print_in_readelf_style(s, fs, root);
	cout << s.str();
	
	// now diff this string against what readelf gives us

	return 0;
}

void print_in_readelf_style(std::ostream& s, const core::FrameSection::cie& cie, lib::Dwarf_Off cie_offset)
{
	// first line is section-offset, length-not-including-length-field, zeroes, "CIE"
	s.width(8);
	s.fill('0');
	s 	<< setw(8) << setfill('0') << std::hex << cie_offset 
		<< ' ' 
		<< setw(8) << setfill('0') << std::hex << cie.bytes_in_cie 
		<< ' ' 
		<< setw(8) << setfill('0') << 0 << std::dec
		<< " CIE"
		<< endl;
	// cie fields come next
	s 	<< "  Version:               " << (int) cie.version                 << endl
		<< "  Augmentation:          \"" << cie.augmenter << "\""  << endl
		<< "  Code alignment factor: " << cie.code_alignment_factor << endl
		<< "  Data alignment factor: " << cie.data_alignment_factor << endl
		<< "  Return address column: " << cie.return_address_register_rule << endl
		<< "  Augmentation data:     " << endl;
	/* "Augmentation data" includes all the data following the "normal" CIE fields
	 * and within the CIE's extent (specified by its length). BUT libdwarf doesn't
	 * let us get at those bytes very easily. */
	
	s << endl;
	
	/* Now we need to print the "initial instructions". */
	encap::frame_instrlist initial_instrs(cie.dbg, /* FIXME */ 8, cie, cie.initial_instructions, 
		cie.initial_instructions_length);
	print_in_readelf_style(s, initial_instrs, -1);
}

void print_in_readelf_style(std::ostream& s, const encap::frame_instrlist& instrs, Dwarf_Addr initial_loc)
{
	Dwarf_Addr loc = initial_loc;
	auto reg = [](int regnum) {
		std::ostringstream s;
		s << "r" << regnum << " (" << dwarf_regnames_for_elf_machine(elf_machine)[regnum]
					<< ")";
		return s.str();
	};
	
	for (auto i_instr = instrs.begin(); i_instr != instrs.end(); ++i_instr)
	{
		const char *opcode_name;
		int encoded_opcode = i_instr->fp_base_op << 6 | i_instr->fp_extended_op;
		int ret = lib::dwarf_get_CFA_name(encoded_opcode, &opcode_name);
		assert(ret == DW_DLV_OK);
		if (string(opcode_name) == "DW_CFA_extended") opcode_name = "DW_CFA_nop";
		s << "  " << opcode_name;
		
		switch (encoded_opcode)
		{
			// "packed" two-bit opcodes
			case DW_CFA_advance_loc: 
			case DW_CFA_advance_loc1:
			case DW_CFA_advance_loc2:
			case DW_CFA_advance_loc4:
				loc += i_instr->fp_offset_or_block_len;
				s << ": " << i_instr->fp_offset_or_block_len << " to " << setw(8) << setfill('0') << loc;
				break;
			case DW_CFA_offset:
				s << ": " << reg(i_instr->fp_register) 
					<< " at cfa" << std::showpos << (int) i_instr->fp_offset_or_block_len << std::noshowpos;
				break;
			case DW_CFA_restore_extended: goto register_only;
			case DW_CFA_undefined: goto register_only;
			case DW_CFA_same_value: goto register_only;
			case DW_CFA_def_cfa_register: goto register_only;
			case DW_CFA_restore: goto register_only;
			register_only:
				s << ": " << reg(i_instr->fp_register);
				break;
			// DW_CFA_extended and DW_CFA_nop are the same value, BUT
			case DW_CFA_nop: goto no_args;      // this is a full zero byte
			// extended opcodes follow
			case DW_CFA_remember_state: goto no_args;
			case DW_CFA_restore_state: goto no_args;
			no_args:
				break;
			case DW_CFA_set_loc:
				goto unsupported_for_now; // FIXME
				break;

			case DW_CFA_offset_extended_sf: goto register_and_offset;
			case DW_CFA_def_cfa_sf: goto register_and_offset;
			case DW_CFA_register: goto register_and_offset;
			case DW_CFA_offset_extended: goto register_and_offset;
			case DW_CFA_def_cfa: goto register_and_offset;
			case DW_CFA_val_offset: goto register_and_offset;
			case DW_CFA_val_offset_sf: goto register_and_offset;
			register_and_offset: // FIXME: second register goes where? I've put it in fp_offset_or_block_len
				s << ": " << reg(i_instr->fp_register) << " ofs " << i_instr->fp_offset_or_block_len;
				break;

			case DW_CFA_def_cfa_offset_sf: goto offset_only;
			case DW_CFA_def_cfa_offset: goto offset_only;
			offset_only:
				s << ": " << i_instr->fp_offset_or_block_len;
				break;

			case DW_CFA_expression:
				goto unsupported_for_now; // FIXME

			case DW_CFA_def_cfa_expression: goto expression;
			case DW_CFA_val_expression: goto expression;
			expression:
				s << " (";
				print_in_readelf_style(s, encap::loc_expr(dbg, i_instr->fp_expr_block, i_instr->fp_offset_or_block_len));
				s << ")";
				break;

			default: goto unsupported_for_now;
			unsupported_for_now:
				s << "FIXME";
				break;
		}
		
		s << endl;
	}
}

void print_in_readelf_style(std::ostream& s, const encap::loc_expr& expr)
{
	for (auto i_instr = expr.begin(); i_instr != expr.end(); ++i_instr)
	{
		if (i_instr != expr.begin()) s << "; ";
		s << spec::DEFAULT_DWARF_SPEC.op_lookup(i_instr->lr_atom);
		if (i_instr->lr_atom >= DW_OP_breg0 && i_instr->lr_atom <= DW_OP_breg31)
		{
			s << " (" 
				<< dwarf_regnames_for_elf_machine(elf_machine)[i_instr->lr_atom - DW_OP_breg0]
				<< ")";
		}
		if (i_instr->lr_atom >= DW_OP_reg0 && i_instr->lr_atom <= DW_OP_reg31)
		{
			s << " (" 
				<< dwarf_regnames_for_elf_machine(elf_machine)[i_instr->lr_atom - DW_OP_reg0]
				<< ")";
		}
		for (int i = 0; i < spec::DEFAULT_DWARF_SPEC.op_operand_count(i_instr->lr_atom); ++i)
		{
			assert(i < 2); // no opcode has >2 arguments
			if (i == 0) s << ": ";
			s << ((i == 0) ? i_instr->lr_number : i_instr->lr_number2);
		}
	}
}
void print_in_readelf_style(std::ostream& s, lib::Dwarf_Fde fde, lib::Dwarf_Debug dbg, lib::Dwarf_Off cie_offset)
{
	core::FrameSection::cie cie(dbg, fde);
	
	// auto decoded = decode_fde(dbg, fde);
	// don't decode -- that's for -wF

	// first line is section-offset, length-not-including-length-field, zeroes, "FDE"
	core::FrameSection::fde_range fde_range(fde);
	s.width(8);
	s.fill('0');
	s 	<< setw(8) << setfill('0') << std::hex << fde_range.fde_offset
		<< ' ' 
		<< setw(8) << setfill('0') << std::hex << fde_range.fde_byte_length
		<< ' ' 
		<< setw(8) << setfill('0') << 0 << std::dec
		<< " FDE cie="
		<< setw(8) << setfill('0') << std::hex << cie_offset
		<< " pc="
		<< setw(8) << setfill('0') << std::hex << fde_range.low_pc
		<< ".." 
		<< setw(8) << setfill('0') << std::hex << (fde_range.low_pc + fde_range.func_length)
		<< endl;
	
	/* Now we need to print the instructions. */
	lib::Dwarf_Ptr instrbytes;
	lib::Dwarf_Unsigned len;
	int fde_ret = dwarf_get_fde_instr_bytes(fde, &instrbytes, &len, &core::current_dwarf_error);
	assert(fde_ret == DW_DLV_OK);
	encap::frame_instrlist instrs(cie.dbg, /* FIXME */ 8, cie, instrbytes, len);
	print_in_readelf_style(s, instrs, fde_range.low_pc);
}

void print_in_readelf_style(std::ostream& s, const core::FrameSection& fs, core::root_die& r)
{
	s << "Contents of the .eh_frame section:\n\n";
	/* Decode all the CIEs and FDEs in the frame section in a readelf -wF style, 
	 * and diff them against what that command says. */
	/* PROBLEM: libdwarf doesn't let us get a CIE's offset, except through an FDE.
	 * So walk all FDEs now and record the CIE offsets. */
	map<lib::Dwarf_Off, set<lib::Dwarf_Off> > fde_offsets_by_cie_offset;
	map<int, int> cie_offsets_by_index;
	for (auto i_fde = fs.fde_begin(); i_fde != fs.fde_end(); ++i_fde)
	{
		core::FdeRange range(*i_fde);
		fde_offsets_by_cie_offset[range.cie_offset].insert(range.fde_offset);
		lib::Dwarf_Signed index;
		lib::Dwarf_Cie cie;
		int cie_ret = dwarf_get_cie_of_fde(*i_fde, 
			&cie, &core::current_dwarf_error);
		assert(cie_ret == DW_DLV_OK);
		int index_ret = dwarf_get_cie_index(cie, &index, &core::current_dwarf_error);
		assert(index_ret == DW_DLV_OK);
		cie_offsets_by_index[index] = range.cie_offset;
	}
	// do we have any orphan CIEs?
	assert(cie_offsets_by_index.size() == fs.cie_end() - fs.cie_begin());
	
	core::FrameSection::cie_iterator i_cie = fs.cie_begin();
	core::FrameSection::fde_iterator i_fde = fs.fde_begin();
	while (!(i_cie == fs.cie_end() && i_fde == fs.fde_end()))
	{
		// print whichever has the lower offset
		lib::Dwarf_Off fde_offset = (i_fde == fs.fde_end()) 
			? std::numeric_limits<lib::Dwarf_Off>::max() : core::FdeRange(*i_fde).fde_offset;
		lib::Dwarf_Off cie_offset = (i_cie == fs.cie_end())
			? std::numeric_limits<lib::Dwarf_Off>::max() : cie_offsets_by_index[i_cie - fs.cie_begin()];
		
		if (cie_offset < fde_offset)
		{
			core::Cie cie(r.get_dbg().raw_handle(), *i_cie);
			print_in_readelf_style(s, cie, cie_offset);
			s << endl;
			++i_cie;
		} 
		else
		{
			// now print the FDE
			auto fde = *i_fde;
			core::FdeRange fde_range(fde);
			print_in_readelf_style(s, fde, r.get_dbg().raw_handle(), fde_range.cie_offset);
			s << endl;
			++i_fde;
		}
	}
}

