#include <cstdio>
#include <cassert>
#include <string>
#include <vector>
#include <set>
#include <sstream>
#include <cmath>

#include <boost/algorithm/string.hpp>

#include <indenting_ostream.hpp>
#include <dwarfpp/encap.hpp>
#include <dwarfpp/cxx_model.hpp>
#include <dwarfpp/cxx_dependency_order.hpp>
#include <srk31/algorithm.hpp>

using namespace srk31;
using namespace dwarf;
using namespace dwarf::lib;
using std::vector;
using std::set;
using std::map;
using std::string;
using std::cout;
using std::cerr;
using std::endl;
using std::hex;
using std::dec;
using std::ostringstream;
using std::istringstream;
using std::stack;
using std::deque;
using boost::optional;
using boost::shared_ptr;
using boost::dynamic_pointer_cast;
using namespace dwarf::lib;

#include <boost/graph/graph_concepts.hpp>   
int main(int argc, char **argv)
{
	using dwarf::tool::dwarfidl_cxx_target;
	
	boost::function_requires< boost::IncidenceGraphConcept<dwarf::tool::cpp_dependency_order> >();
	boost::function_requires< boost::VertexListGraphConcept<dwarf::tool::cpp_dependency_order> >();	

	// open the file passed in on the command-line
	assert(argc > 1);
	FILE* f = fopen(argv[1], "r");
	
	// construct a dwarf::file
	dwarf::lib::file df(fileno(f));
	
	// encapsulate the DIEs
	dwarf::encap::file def(fileno(f));	
	auto all_cus = def.ds().toplevel();

	indenting_ostream& s = srk31::indenting_cout;
	
	vector<string> compiler_argv = dwarf::tool::cxx_compiler::default_compiler_argv(true);
	compiler_argv.push_back("-fno-eliminate-unused-debug-types");
	compiler_argv.push_back("-fno-eliminate-unused-debug-symbols");
	
	dwarfidl_cxx_target target(" ::cake::unspecified_wordsize_type", // HACK: Cake-specific
		s, compiler_argv);
	
	target.emit_all_decls(all_cus);

	return 0;
}
