#include <fstream>
#include <vector>
#include <fileno.hpp>
#include <dwarfpp/lib.hpp>

using std::cout;
using std::endl;
using std::vector;
using namespace dwarf;
using namespace dwarf::core;

static bool is_abstract_origin_fp_subprogram(iterator_df<type_describing_subprogram_die> sub)
{
	auto fps = sub->children().subseq_of<formal_parameter_die>();
	for (auto i_fp = fps.first; i_fp != fps.second; ++i_fp)
	{
		if (!i_fp->get_type() && i_fp->find_type()) return true;
	}
	return false;
}

int main(int argc, char **argv)
{
	const char *path = "deep.os";
	cout << "Opening \"" << path << "\"..." << endl;
	std::ifstream in(path);
	assert(in && "cannot open input");
	root_die root(fileno(in));

	vector<iterator_df<type_describing_subprogram_die> > interesting;
	for (iterator_df<> i = root.begin(); i != root.end(); ++i)
	{
		if (!i.is_a<type_describing_subprogram_die>()) continue;
		auto sub = i.as_a<type_describing_subprogram_die>();
		if (is_abstract_origin_fp_subprogram(sub)) interesting.push_back(sub);
	}

	cout << "Found " << interesting.size()
	     << " subprogram(s) with abstract_origin-only formal parameters." << endl;
	assert(interesting.size() >= 2
		&& "expected >= 2 concrete-instance subprograms (two duplicate CUs)");

	auto a = interesting[0], b = interesting[1];

	/* Show exactly what trips the old assertion:get_type() is empty,
	 * while find_type() (the fix) resolves the type through  DW_AT_abstract_origin. */
	auto fps = a->children().subseq_of<formal_parameter_die>();
	for (auto i_fp = fps.first; i_fp != fps.second; ++i_fp)
	{
		cout << "  fp \"" << (i_fp.name_here() ? *i_fp.name_here() : "(anon)")
		     << "\": get_type()=" << (i_fp->get_type() ? "present" : "ABSENT")
		     << " find_type()=" << (i_fp->find_type() ? "present" : "absent")
		     << "  <- pre-fix asserts on the ABSENT get_type()" << endl;
	}

	/* THE TRIGGER: compare two such subprogram types. */
	cout << "Comparing two such subprogram types for equality..." << endl;
	bool eq = (*a == *b);
	cout << "equal() returned " << (eq ? "EQUAL" : "UNEQUAL")
	     << " (no crash)." << endl;

	cout << "OK" << endl;
	return 0;
}
