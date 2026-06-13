/* libdwarfpp's FrameSection locates an FDE by PC and then walks subsequent
 * array entries assuming ascending low_pc (see the DWARF .eh_frame_hdr
 * "sorted in increasing order by the initial location" note in frame.hpp).
 * 
 * A linked .eh_frame is *not* necessarily emitted in that order, so the libdw
 * backend's build_fde_list() must sort the FDEs by low_pc.
 */
#include <fstream>
#include <cassert>
#include <iostream>
#include <fileno.hpp>
#include <dwarfpp/lib.hpp>
#include <dwarfpp/frame.hpp>

using std::cout;
using std::endl;
using namespace dwarf;
using dwarf::lib::Dwarf_Addr;
using dwarf::core::FrameSection;

int main(int argc, char **argv)
{
	cout << "Opening " << argv[1] << "..." << endl;
	std::ifstream in(argv[1]);
	core::root_die root(fileno(in));

	FrameSection fs(root.get_dbg(), /* use_eh */ true);

	unsigned long count = 0;
	Dwarf_Addr prev_low_pc = 0;
	for (auto i_fde = fs.fde_begin(); i_fde != fs.fde_end(); ++i_fde, ++count)
	{
		Dwarf_Addr low_pc = i_fde->get_low_pc();
		assert(low_pc >= prev_low_pc
			&& "FDEs are not in non-decreasing low_pc order "
			   "(build_fde_list must sort by initial location)");
		prev_low_pc = low_pc;
	}

	cout << "Checked " << count << " FDEs; all in non-decreasing low_pc order."
		<< endl;
	assert(count > 1 && "expected more than one FDE in this executable");
	return 0;
}
