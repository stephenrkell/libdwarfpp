#include <dwarfpp/encap.hpp>
//#include <dwarfpp/util.hpp>
#include <functional>

// test die assignment

int main(int argc, char **argv)
{
	// open the file passed in on the command-line
	assert(argc > 1);
	FILE* f = fopen(argv[1], "r");
	
	// construct a dwarf::file
	dwarf::file df(fileno(f));

	dwarf::abi_information info(df);
	
	Dwarf_Off start_offset = info.get_dies().begin()->first;	
	dwarf::print_action action(info);
	namespace w = dwarf::walker;
	w::depthfirst_walker<dwarf::print_action> print_walk(action);
	
	// print out before
	print_walk(info.get_dies(), start_offset);
	
	// first first two structure types
	
	typedef w::func1_do_nothing<Dwarf_Half> do_nothing_t;
	
	typedef w::siblings_upward_walker<do_nothing_t, 
		w::tag_equal_to_matcher_t> first_walker_t;
		
	const w::tag_equal_to_matcher_t matcher = w::matcher_for_tag_equal_to(DW_TAG_structure_type);
	
	Dwarf_Off first_struct = *(w::find_first_match<w::tag_equal_to_matcher_t, first_walker_t>(
		info.get_dies(), 0UL, matcher));
	
	typedef w::siblings_upward_walker<w::capture_func<Dwarf_Off>, 
		w::tag_equal_to_matcher_t> second_walker_t;
		
	w::capture_func<Dwarf_Off> capture;
	w::select_until_captured until_captured(capture);
	second_walker_t second_find_walker(capture, matcher, until_captured);
	
	second_find_walker(info.get_dies(), first_struct);	
	Dwarf_Off second_struct = *(capture.captured);
	
	// swap them
	std::swap(info.get_dies()[first_struct], info.get_dies()[second_struct]);
	
	// print out now
	print_walk(info.get_dies(), start_offset);
}
