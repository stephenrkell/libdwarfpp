#include <fstream>
#include <fileno.hpp>
#include <dwarfpp/lib.hpp>
#include <dwarfpp/attr.hpp>

using std::cout; 
using std::endl;
using namespace dwarf;
using core::iterator_base;
using core::member_die;

struct Foo
{
	int blah;
	Foo *next;
};

struct Foo f;

int main(int argc, char **argv)
{
	cout << "Opening " << argv[0] << "..." << endl;
	std::ifstream in(argv[0]);
	core::root_die root(fileno(in));

	cout << "Searching for struct Foo..." << endl;
	
	vector<string> path = { "siblings-core.cpp", "Foo" };
	auto found = root.resolve(root.begin(), path.begin(), path.end());
	assert(found != iterator_base::END);

	cout << "struct Foo's children are..." << endl;
	
	auto children = found.children_here();
	for (auto i = children.first; i != children.second; ++i)
	{
		cout << i;
	}
	
	cout << "struct Foo's DW_TAG_member children are..." << endl;
	
	auto member_children = found.children_here().subseq_of<member_die>();
	assert(member_children.first.base().base() != member_children.second.base().base());
	assert(member_children.first.base() != member_children.second.base());
	assert(member_children.first != member_children.second);
	int count = 0;
	for (auto i = member_children.first; i != member_children.second; ++i, ++count)
	{
		cout << i.base().base();
	}
	assert(count >= 2);
	
	return 0;
}
