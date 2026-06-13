/* Second CU of the fixture. After `ld -r`, its abbrev table sits at a non-zero
 * offset in the merged .debug_abbrev, and its CU header's debug_abbrev_offset
 * field carries an R_X86_64_32 relocation against .debug_abbrev. If that
 * relocation is not applied (the bug), libdw reads abbrev offset 0 and parses
 * this CU against cu_a's table, mangling every name and type here. */
typedef struct { char b1; double b2; long b3; } beta_t;
beta_t beta_global;
double beta_func(double y) { return y * 2.0; }
