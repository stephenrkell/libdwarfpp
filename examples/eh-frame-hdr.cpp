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
using std::cerr;
using std::endl;
using std::ostringstream;
using std::map;
using namespace dwarf;
using dwarf::lib::Dwarf_Addr;
using dwarf::core::FrameSection;
using boost::optional;

int main(int argc, char **argv)
{
	cout << "Opening " << argv[1] << "..." << endl;
	std::ifstream in(argv[1]);
	core::root_die root(fileno(in));
	auto& fs = root.get_frame_section();
	cout << "Binary search table in .eh_frame_hdr has "
		<< fs.hdr_tbl_nbytes
		<< " bytes of content, encoding 0x"
		<< std::hex << fs.hdr_tbl_encoding << std::endl;
	cout << "The table contains:" << endl;
	for (auto i_pair = fs.hdr_tbl_begin();
		i_pair != fs.hdr_tbl_end();
		++i_pair)
	{
		cout << "location: " << std::hex << "0x" << i_pair->first
			<< " (raw: " << i_pair.dereference_raw().first << ")"
			<< ", FDE address in PT_GNU_EH_FRAME segment: 0x" << i_pair->second
			<< " (vaddr: 0x" << (i_pair->second + fs.hdr_vaddr) << ")" << endl;
	}

	return 0;
}
