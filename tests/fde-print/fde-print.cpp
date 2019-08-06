#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <fileno.hpp>
#include <dwarfpp/lib.hpp>
#include <dwarfpp/expr.hpp>
#include <dwarfpp/frame.hpp>
#include <dwarfpp/regs.hpp>
#include <gelf.h>

using std::cout;
using std::cerr;
using std::endl;
using std::ostringstream;
using std::setw;
using std::setfill;
using std::map;
using namespace dwarf;
using dwarf::lib::Dwarf_Addr;
using dwarf::lib::dwarf_regnames_for_elf_machine;
using dwarf::core::Fde;

void print_in_readelf_style(std::ostream& s, const core::FrameSection& fs, core::root_die& r);
void print_in_readelf_style(std::ostream& s, const encap::loc_expr& expr);
void print_in_readelf_style(std::ostream& s, const encap::frame_instrlist& instrs, Dwarf_Addr initial_loc);
int elf_machine;
lib::Dwarf_Debug dbg;

int main(int argc, char **argv)
{
	cout << "Opening " << argv[1] << "..." << endl;
	std::ifstream in(argv[1]);
	core::root_die root(fileno(in));
	
	dbg = root.get_dbg().raw_handle();
	GElf_Ehdr ehdr;
	GElf_Ehdr *ret = gelf_getehdr(root.get_elf(), &ehdr);
	assert(ret != 0);
	elf_machine = ehdr.e_machine;
	
	core::FrameSection fs(root.get_dbg(), true);
	
	ostringstream s;
	print_in_readelf_style(s, fs, root);
	string str = s.str();
	if (getenv("PRINT_FDE")) cerr << str;
	
	const char *data_start = str.c_str();
	const char *data_end = data_start + strlen(data_start);
	const char *data_pos = data_start;
	
	// now diff this string against what readelf gives us
	FILE *pipein = popen((string("diff -u /dev/stdin /dev/fd/3 3<<END\n$( readelf -wf ") + argv[1] + ")\nEND").c_str(), "w");
	assert(pipein);
	
	while (data_pos < data_end)
	{
		size_t nmemb = data_end - data_pos;
		int ret = fwrite(data_pos, 1, nmemb, pipein);
		if (ret < nmemb)
		{
			cerr << "Error! " << strerror(errno);
			break;
		}
		data_pos += ret;
	}
	
	// check the status of diff
	int status = pclose(pipein);
	assert(status == 0);
	cout << "Output compares identical to readelf's -- success!" << endl;
	
	return 0;
}

void print_in_readelf_style(std::ostream& s, const core::Cie& cie)
{
	// first line is section-offset, length-not-including-length-field, zeroes, "CIE"
	s.width(8);
	s.fill('0');
	s 	<< setw(8) << setfill('0') << std::hex << cie.get_offset()
		<< ' ' 
		<< setw(16) << setfill('0') << std::hex << cie.get_bytes_in_cie()
		<< ' ' 
		<< setw(8) << setfill('0') << 0 << std::dec
		<< " CIE"
		<< endl;
	// cie fields come next
	s 	<< "  Version:               " << (int) cie.get_version()                 << endl
		<< "  Augmentation:          \"" << cie.get_augmenter() << "\""  << endl
		<< "  Code alignment factor: " << cie.get_code_alignment_factor() << endl
		<< "  Data alignment factor: " << cie.get_data_alignment_factor() << endl
		<< "  Return address column: " << cie.get_return_address_register_rule() << endl
		<< "  Augmentation data:     ";
	auto augbytes = cie.get_augmentation_bytes();
	for (auto i_byte = augbytes.begin(); i_byte != augbytes.end(); ++i_byte)
	{
		if (i_byte != augbytes.begin()) s << ' ';
		s << std::hex << setw(2) << setfill('0') << (unsigned) *i_byte;
	}
	s << std::dec << endl;
	
	s << endl;
	
	/* Now we need to print the "initial instructions". */
	encap::frame_instrlist initial_instrs(cie, /* FIXME */ 8, cie.initial_instructions_seq());
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
				s << ": " << i_instr->fp_offset_or_block_len << " to " << setw(16) << setfill('0') << std::hex << loc << std::dec;
				break;
			case DW_CFA_offset:
				s << ": " << reg(i_instr->fp_register) 
					<< " at cfa" << std::showpos << (lib::Dwarf_Signed)(i_instr->fp_offset_or_block_len) << std::noshowpos;
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
void print_in_readelf_style(std::ostream& s, const core::Fde& fde)
{
	// auto decoded = decode_fde(dbg, fde);
	// don't decode -- that's for -wF

	// first line is section-offset, length-not-including-length-field, id, "FDE"
	s.width(8);
	s.fill('0');
	s 	<< setw(8) << setfill('0') << std::hex << fde.get_fde_offset()
		<< ' ' 
		<< setw(16) << setfill('0') << std::hex << fde.get_fde_byte_length()
		<< ' ' 
		<< setw(8) << setfill('0') << fde.get_id() << std::dec
		<< " FDE cie="
		<< setw(8) << setfill('0') << std::hex << fde.find_cie()->get_cie_offset()
		<< " pc="
		<< setw(16) << setfill('0') << std::hex << fde.get_low_pc()
		<< ".." 
		<< setw(16) << setfill('0') << std::hex << (fde.get_low_pc() + fde.get_func_length())
		<< endl;
	auto augbytes = fde.get_augmentation_bytes();
	if (augbytes.size() > 0) 
	{
		s << "  Augmentation data:     ";
		for (auto i_byte = augbytes.begin(); i_byte != augbytes.end(); ++i_byte)
		{
			if (i_byte != augbytes.begin()) s << ' ';
			s << std::hex << setw(2) << setfill('0') << (unsigned) *i_byte;
		}
		s << endl << endl;
	}
	s << std::dec;
	
	/* Now we need to print the instructions. */
	// TODO: eliminate this once the assertion below assures me that it's the same as 
	// instrbytes etc..
	auto instr_seq = fde.instr_bytes_seq();
	encap::frame_instrlist instrs(*fde.find_cie(), /* FIXME */ 8, instr_seq);
	print_in_readelf_style(s, instrs, fde.get_low_pc());
}

void print_in_readelf_style(std::ostream& s, const core::FrameSection& fs, core::root_die& r)
{
	typedef core::FrameSection::cie_iterator cie_iterator;
	typedef core::FrameSection::fde_iterator fde_iterator;
	/* In what order does readelf print the CIEs and FDEs? 
	 * Answer: strictly offset order. I seem to be doing it in 
	 * a crazy order. This is because our FDEs aren't coming out
	 * in offset order. Sort them first! */
	std::vector< cie_iterator > cies;
	for (auto i_cie = fs.cie_begin(); i_cie != fs.cie_end(); ++i_cie) cies.push_back(i_cie);
	std::vector< fde_iterator > fdes;
	for (auto i_fde = fs.fde_begin(); i_fde != fs.fde_end(); ++i_fde) fdes.push_back(i_fde);
	
	std::sort(cies.begin(), cies.end(), [&fs](const cie_iterator& arg1, const cie_iterator& arg2) {
		assert(arg1 != fs.cie_end());
		assert(arg2 != fs.cie_end());
		return arg1->get_offset() < arg2->get_offset();
	});
	
	std::sort(fdes.begin(), fdes.end(), [&fs](const fde_iterator& arg1, const fde_iterator& arg2) {
		assert(arg1 != fs.fde_end());
		assert(arg2 != fs.fde_end());
		return arg1->get_offset() < arg2->get_offset();
	});

	s << "Contents of the .eh_frame section:\n\n";
	auto i_i_cie = cies.begin();
	auto i_i_fde = fdes.begin();
	lib::Dwarf_Off cur_off = 0;
	unsigned size_of_length_field = fs.is_64bit ? 12 : 4;
	
	while (!(i_i_cie == cies.end() && i_i_fde == fdes.end()))
	{
		auto i_cie = *i_i_cie; 
		auto i_fde = *i_i_fde; 
		
		// print whichever has the lower offset
		lib::Dwarf_Off fde_offset = (i_i_fde == fdes.end()) 
			? std::numeric_limits<lib::Dwarf_Off>::max() : i_fde->get_offset();
		lib::Dwarf_Off cie_offset = (i_i_cie == cies.end())
			? std::numeric_limits<lib::Dwarf_Off>::max() : i_cie->get_offset();
		
		if (cie_offset < fde_offset)
		{
			cur_off = cie_offset + size_of_length_field;
			print_in_readelf_style(s, *i_cie);
			s << endl;
			++i_i_cie;
			cur_off += i_cie->get_bytes_in_cie();
		}
		else
		{
			cur_off = fde_offset + size_of_length_field;
			print_in_readelf_style(s, *i_fde);
			s << endl;
			++i_i_fde;
			cur_off += i_fde->get_fde_byte_length();
		}
	}
	
	s << setw(8) << setfill('0') << std::hex << cur_off << std::dec << " ZERO terminator" << endl;
}

