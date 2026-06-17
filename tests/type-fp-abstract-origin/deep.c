/* 
 *
 * `helper` is both:
 *   - inlined into `caller` (because it is small), and
 *   - emitted out-of-line (because its address is taken via `sink`).
 *
 * That forces gcc to split `helper`'s debug info into:
 *   - an *abstract instance*  (DW_TAG_subprogram with DW_AT_inline), whose
 *     formal parameters carry the real DW_AT_type, and
 *   - one or more *concrete instances* (a DW_TAG_subprogram /
 *     DW_TAG_inlined_subroutine carrying DW_AT_abstract_origin), whose formal
 *     parameters carry only DW_AT_abstract_origin and NO DW_AT_type.
 *
 * It is those concrete-instance formal parameters that make get_type()
 * return void/absent and trip assertion.
 */

static int helper(int a, long b) { return a + (int) b; }

int (*volatile sink)(int, long);

int caller(int x)
{
	sink = helper;        /* address taken -> out-of-line concrete instance */
	return helper(x, x);  /* also inlined here -> inlined concrete instance */
}
