/* Regression test for unbounded recursion in structural type equality and
 * summary-code computation (stack overflow on deeply nested types).
 *
 * The input "deep2.os" (built by include.mk) is two identical CUs each
 * defining a ~1000-level nested struct, so equality must recurse through the
 * whole nesting on the first comparison (before any cache is warm). 
 */

#include <fstream>
#include <iostream>
#include <vector>
#include <cstdlib>
#include <pthread.h>
#include <fileno.hpp>
#include <dwarfpp/lib.hpp>

using std::cout;
using std::cerr;
using std::endl;
using namespace dwarf;
using namespace dwarf::core;

/* We only need a shallow stack regardless of type-nesting depth. */
static const size_t SMALL_STACK_BYTES = 256 * 1024;

static int worker_result = 1;

static void *worker(void *)
{
	std::ifstream in("deep2.os");
	if (!in) { cerr << "could not open deep2.os" << endl; return nullptr; }
	root_die root(fileno(in));

	/* Find the deeply-nested struct type. It is the type of the global
	 * variable 'g'; there are two copies (one per CU). We deliberately do NOT
	 * touch their constituents first, so the first comparison/summary must
	 * recurse through the full nesting depth. */
	std::vector<iterator_df<type_die> > deep_types;
	for (auto i = root.begin(); i != root.end(); ++i)
	{
		if (!i.is_a<variable_die>()) continue;
		auto v = i.as_a<variable_die>();
		auto nm = v.name_here();
		if (nm && *nm == "g" && v->get_type()) deep_types.push_back(v->get_type());
	}
	if (deep_types.size() < 2)
	{
		cerr << "expected two copies of the deep type, found " << deep_types.size() << endl;
		return nullptr;
	}
	auto ta = deep_types[0], tb = deep_types[1];

	/* (1) summary_code() on the deepest type must not overflow the stack. */
	cout << "Computing summary_code() of the deep type..." << endl;
	opt<uint32_t> code_a = ta->summary_code();
	opt<uint32_t> code_b = tb->summary_code();
	assert(code_a && code_b);
	assert(*code_a == *code_b); // identical types -> identical summary codes
	cout << "summary_code ok (0x" << std::hex << *code_a << std::dec << ")" << endl;

	/* (2) equal() on the deepest type vs its duplicate must not overflow the
	 * stack, and must (correctly) report them equal. The caches are cold for
	 * these two roots' sub-pairs, so without the fix this recurses to full
	 * depth. */
	cout << "Comparing the deep type with its duplicate..." << endl;
	bool eq = (*ta == *tb);
	assert(eq);
	cout << "equal() ok (deep duplicates compare equal)" << endl;

	/* (3) A structurally-keyed type_set over all type DIEs must complete and
	 * deduplicate the two CUs' identical types. */
	cout << "Building a type_set over all type DIEs..." << endl;
	type_set ts;
	unsigned seen = 0;
	for (auto i = root.begin(); i != root.end(); ++i)
	{
		if (!i.is_a<type_die>()) continue;
		++seen;
		ts.insert(i.as_a<type_die>());
	}
	cout << "type_set built: saw " << seen << " type DIEs, "
		<< ts.size() << " unique by structural equality" << endl;
	/* The two CUs are identical, so dedup must roughly halve the count. */
	assert(ts.size() < seen);

	worker_result = 0;
	return nullptr;
}

int main(int argc, char **argv)
{
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	if (pthread_attr_setstacksize(&attr, SMALL_STACK_BYTES) != 0)
	{ perror("pthread_attr_setstacksize"); return 2; }
	pthread_t th;
	if (pthread_create(&th, &attr, worker, nullptr) != 0)
	{ perror("pthread_create"); return 2; }
	pthread_join(th, nullptr);
	pthread_attr_destroy(&attr);

	if (worker_result == 0)
		cout << "PASS: deep summary_code/equal/type_set completed on a "
			<< (SMALL_STACK_BYTES / 1024) << " KiB stack" << endl;
	else
		cerr << "FAIL" << endl;
	return worker_result;
}
