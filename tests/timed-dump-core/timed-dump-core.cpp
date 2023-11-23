#include <fstream>
#include <fileno.hpp>
#include <dwarfpp/lib.hpp>
#include <dwarfpp/attr.hpp>

using std::cout; 
using std::endl;
using namespace dwarf;

int main(int argc, char **argv)
{
	cout << "Opening " << argv[0] << "..." << endl;
	std::ifstream in(argv[0]);
	core::root_die root(fileno(in));

	int status0 = system("./exit-centiseconds.sh sh -c '../../contrib/libdwarf/prefix/bin/dwarfdump ../../examples/dwarfppdump >/dev/null 2>/dev/null'");
	int status1 = system("./exit-centiseconds.sh sh -c 'readelf -wi ../../examples/dwarfppdump >/dev/null 2>/dev/null'");
	int status2 = system("./exit-centiseconds.sh sh -c '../../examples/dwarfppdump ../../examples/dwarfppdump >/dev/null 2>/dev/null'");
	
	int exit_status0 = WEXITSTATUS(status0);
	int exit_status1 = WEXITSTATUS(status1);
	int exit_status2 = WEXITSTATUS(status2);
	
	cout << "Centiseconds for dwarfdump: " << exit_status0 << endl;
	cout << "Centiseconds for readelf: " << exit_status1 << endl;
	cout << "Centiseconds for our code: " << exit_status2 << endl;
	
	/* This is only likely to pass for an optimised build. */
	assert(exit_status2 < 1.4 * exit_status0); 
	return 0;
}
