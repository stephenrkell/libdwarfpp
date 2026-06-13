/* First CU of the fixture. Its abbrev table ends up at offset 0 in the merged
 * .debug_abbrev, so it parses correctly even if the CU-header abbrev-offset
 * relocation is not applied. Deliberately structurally different from cu_b so
 * the two CUs get distinct abbrev tables. */
typedef struct { long a1; int a2; } alpha_t;
alpha_t alpha_global;
int alpha_func(int x) { return x + 1; }
