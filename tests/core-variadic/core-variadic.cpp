#include <fstream>
#include <fileno.hpp>
#include <dwarfpp/lib.hpp>
#include <dwarfpp/attr.hpp>

using std::cout; 
using std::endl;
using namespace dwarf;

int variadic_we_should_find(const char *arg, ...);
int variadic_we_should_find(const char *arg, ...)
{
	cout << "Hello from a variadic function." << endl;
}

int main(int argc, char **argv)
{
	using namespace dwarf::core; 
	
	cout << "Opening " << argv[0] << "..." << endl;
	std::ifstream in(argv[0]);
	core::root_die root(fileno(in));

	cout << "Searching for variadic functions..." << endl;
	for (auto i = root.begin(); i != root.end(); ++i)
	{
		if (i.tag_here() == DW_TAG_subprogram)
		{
			iterator_df<subprogram_die> i_subp = i;
			if (i_subp->is_variadic(root)) cout << "Variadic: " << i_subp/*->summary()*/ << endl; 
			else cout << "Not variadic: " << i_subp/*->summary()*/ << endl;
		} 
	}
	
	return 0;
}
