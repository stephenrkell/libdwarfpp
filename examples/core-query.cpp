/* Test program for libdwarfpp. */

#include <cstdio>
#include <cassert>
#include <string>
#include <vector>
#include <dwarfpp/lib.hpp>

using std::vector;
using std::string;
using std::cout;
using std::cin;
using std::endl;

int main(int argc, char **argv)
{
	// open the file passed in on the command-line
	assert(argc > 1);
	FILE* f = fopen(argv[1], "r");
	
	dwarf::core::root_die r(fileno(f));
	
	cout << "Enter DWARF pathname, followed by newline. Ctrl-D to exit." << endl;
	
	string path;
	while (cin.good())
	{
		std::cin >> path;
		vector<string> split_path;
		string::size_type pos = 0;
		do
		{
			string::size_type next_pos = path.find("::", pos);
			if (next_pos == string::npos) next_pos = path.length();
			split_path.push_back(string(path, pos, next_pos - pos));
			if (next_pos != string::npos) pos = next_pos + 2;
		} while (pos < path.length());
		
		vector<dwarf::core::iterator_base > results;
		r.resolve_all_visible_from_root(split_path.begin(), split_path.end(),
			results, 1);
		if (results.size() == 1) cout << *results.begin(); else cout << "not found!";
		cout << endl;
	}
}

