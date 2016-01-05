#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <boost/optional.hpp>
#include <fileno.hpp>
#include <dwarfpp/lib.hpp>
#include <dwarfpp/expr.hpp>
#include <dwarfpp/frame.hpp>
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
using dwarf::lib::dwarf_regnames_for_elf_machine;
using dwarf::core::Fde;
using dwarf::core::FrameSection;
using dwarf::core::Cie;
using boost::optional;

void print_in_readelf_style(std::ostream& s, const FrameSection& fs, core::root_die& r);
void print_in_readelf_style(std::ostream& s, const FrameSection::instrs_results& result, const Cie& cie);
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
	
	FrameSection fs(root.get_dbg(), true);
	
	ostringstream s;
	print_in_readelf_style(s, fs, root);
	string str = s.str();
	if (getenv("PRINT_DECODED_FDE")) cerr << str;
	
	const char *data_start = str.c_str();
	const char *data_end = data_start + strlen(data_start);
	const char *data_pos = data_start;
	
	// now diff this string against what readelf gives us
	/* NOTE: readelf has an arguable bug whereby it doesn't deduplicate 
	 * address ranges with identical register/CFA definitions. We *do*
	 * this deduplication, as a side effect of our use of interval_map, 
	 * so our output is more compact. To compensate for this, we use 
	 * uniq to filter out successive identical lines, ignoring the first
	 * 16 characters i.e. the base address of the interval. */
	FILE *pipein = popen((string("diff -u /dev/stdin /dev/fd/3 3<<END\n$( readelf -wF ") + argv[1] + " | uniq -s16 )\nEND").c_str(), "w");
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

void print_in_readelf_style(std::ostream& s, const Cie& cie)
{
	// first line is section-offset, length-not-including-length-field, zeroes, "CIE"
	s.width(8);
	s.fill('0');
	s 	<< setw(8) << setfill('0') << std::hex << cie.get_offset()
		<< ' ' 
		<< setw(16) << setfill('0') << std::hex << cie.get_bytes_in_cie()
		<< ' ' 
		<< setw(8) << setfill('0') << 0 << std::dec
		<< " CIE "
		<< "\"" << cie.get_augmenter() << "\" cf=" << cie.get_code_alignment_factor()
		<< " df=" << cie.get_data_alignment_factor()
		<< " ra=" << cie.get_return_address_register_rule() << endl;
	
	/* Now we need to print the "initial instructions" in decoded form. */
	auto result = cie.get_owner().interpret_instructions(
		cie, 
		0, 
		cie.get_initial_instructions(),
		cie.get_initial_instructions_length()
	);
	// encap::frame_instrlist initial_instrs(cie, /* FIXME */ 8, cie.initial_instructions_seq());
	/* Add the unfinished row as if it were a real row. */
	result.add_unfinished_row(/* HACK */ result.unfinished_row_addr + 1);

	print_in_readelf_style(s, result, cie);
}

void print_in_readelf_style(std::ostream& s, const FrameSection::instrs_results& result, const Cie& cie)
{
	s << setw(2 * cie.get_address_size()) << setfill(' ') << std::left << "   LOC" << std::right << ' ';
		
	int ra_rule_number = cie.get_return_address_register_rule();
	
	// enumerate our columns
	set<int> all_columns;
	all_columns.insert(DW_FRAME_CFA_COL3);
	for (auto i_row = result.rows.begin(); i_row != result.rows.end(); ++i_row)
	{	
		for (auto i_reg = i_row->second.begin(); i_reg != i_row->second.end(); ++i_reg)
		{
			all_columns.insert(i_reg->first);
		}
	}
	
	typedef std::function<void(int, optional< pair<int, FrameSection::register_def> >)>
	 visitor_function;
	
	auto visit_columns = [all_columns, ra_rule_number, &s](
		 visitor_function visit, 
		 optional<const set< pair<int, FrameSection::register_def> > &> opt_i_row
		) {
		
		auto get_column = [&opt_i_row](int col) {
			
			if (!opt_i_row) return optional< pair<int, FrameSection::register_def> >();
			else
			{
				map<int, FrameSection::register_def> m(opt_i_row->begin(), opt_i_row->end());
				auto found = m.find(col);
				return found != m.end() ? make_pair(found->first, found->second) : optional< pair<int, FrameSection::register_def> >();
			}
		};
		
		// always visit CFA column
		visit(DW_FRAME_CFA_COL3, get_column(DW_FRAME_CFA_COL3));
		
		// visit other columns that exist, except the ra rule
		for (auto i_col = all_columns.begin(); i_col != all_columns.end(); ++i_col)
		{
			if (*i_col != DW_FRAME_CFA_COL3 && *i_col != ra_rule_number)
			{
				visit(*i_col, get_column(*i_col));
			}
		}
		
		// always visit the ra rule 
		visit(ra_rule_number, get_column(ra_rule_number));
	};

	// print the column headers
	visitor_function column_header_visitor = [all_columns, ra_rule_number, &s]
		(int col, optional< pair<int, FrameSection::register_def> > unused) -> void {
		if (col == DW_FRAME_CFA_COL3) s << "CFA    ";
		else if (col == ra_rule_number) s << setw(0) << "  ra      ";
		else s << setw(5) << dwarf_regnames_for_elf_machine(elf_machine)[col] << ' ';
	};
	
	visit_columns(column_header_visitor, /* nullptr */ optional<const set<pair<int, FrameSection::register_def> > &>());
	s << endl;
	
	visitor_function print_row_column_visitor = [all_columns, ra_rule_number, &s]
		(int col, optional< pair<int, FrameSection::register_def> > found_col)  -> void {
		
		s << setfill(' ') << setw(col == DW_FRAME_CFA_COL3 ? 8 : col == ra_rule_number ? 6 : 5);
		if (!found_col) s << std::left << "u" << std::right;
		else
		{
			switch (found_col->second.k)
			{
				case FrameSection::register_def::INDETERMINATE:
				case FrameSection::register_def::UNDEFINED: 
					s << std::left << "u" << std::right;
					break;

				case FrameSection::register_def::REGISTER: {
					ostringstream exprs;
					int regnum = found_col->second.register_plus_offset_r().first;
					string regname;
					if (regnum == DW_FRAME_CFA_COL3) regname = "X"; // should not happen? 
						// DON'T confuse this case with 'c', which means SAVED_AT_OFFSET_FROM_CFA
					else regname = dwarf_regnames_for_elf_machine(elf_machine)[regnum];
					exprs << regname << std::showpos << std::dec << setw(0) << (int) found_col->second.register_plus_offset_r().second;
					
					s << std::left << exprs.str() << std::right;
				} break;

				case FrameSection::register_def::SAVED_AT_OFFSET_FROM_CFA: {
					ostringstream exprs;
					exprs << "c" << std::showpos << std::dec << found_col->second.saved_at_offset_from_cfa_r();
					s << std::left << exprs.str() << std::right;
				} break;
				case FrameSection::register_def::SAVED_AT_EXPR:
					s << std::left << "exp" << std::right;
					break;
				case FrameSection::register_def::VAL_IS_OFFSET_FROM_CFA:
				case FrameSection::register_def::VAL_OF_EXPR: 
				default:
					s << std::left << "BLAH" << std::right;
					break;
			}
		}
		
		if (col != ra_rule_number) s << ' ';
	};
	
	// print the row contents
	for (auto i_int = result.rows.begin(); i_int != result.rows.end(); ++i_int)
	{
		s << std::hex << setfill('0') << setw(2 * cie.get_address_size())
			<< i_int->first.lower() << ' ';
		
		visit_columns(print_row_column_visitor, i_int->second);
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
void print_in_readelf_style(std::ostream& s, const Fde& fde)
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
		
	/* Now we need to print the "initial instructions" in decoded form. */
	auto result = fde.decode();
	result.add_unfinished_row(fde.get_low_pc() + fde.get_func_length());

	print_in_readelf_style(s, result, *fde.find_cie());
}

void print_in_readelf_style(std::ostream& s, const FrameSection& fs, core::root_die& r)
{
	typedef FrameSection::cie_iterator cie_iterator;
	typedef FrameSection::fde_iterator fde_iterator;
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

