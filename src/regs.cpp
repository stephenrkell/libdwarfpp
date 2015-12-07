#include "regs.hpp"
#include "lib.hpp"

namespace dwarf
{
namespace lib 
{

const char *dwarf_regnames_x86[] = {
	"eax",
	"ecx",
	"edx",
	"ebx",
	"esp",
	"ebp",
	"esi",
	"edi",
	"eip",
	"eflags",
	"trapno",
	"sto",
	nullptr
};

const char *dwarf_regnames_x86_64[] = {
	"rax",
	"rdx",
	"rcx",
	"rbx",
	"rsi",
	"rdi",
	"rbp",
	"rsp",
	"r8",
	"r9",
	"r10",
	"r11",
	"r12",
	"r13",
	"r14",
	"r15",
	"rip",
	nullptr
};

const char **dwarf_regnames_for_elf_machine(int e_machine)
{
	switch (e_machine)
	{
		case /* EM_386 */           3:           /* Intel 80386 */
			return dwarf_regnames_x86;
		case /* EM_X86_64 */       62:              /* AMD x86-64 architecture */
			return dwarf_regnames_x86_64;
		default:
		return nullptr;
	}
}

dwarf::encap::loc_expr dwarf_stack_pointer_expr_for_elf_machine(int e_machine,
	dwarf::lib::Dwarf_Addr lopc, dwarf::lib::Dwarf_Addr hipc)
{
	using dwarf::encap::loc_expr;
	using dwarf::lib::Dwarf_Unsigned;
	switch (e_machine)
	{
		case /* EM_386 */           3:           /* Intel 80386 */
			return loc_expr((Dwarf_Unsigned[]) { DW_OP_breg0 + DWARF_X86_ESP, 0 }, lopc, hipc);
		case /* EM_X86_64 */       62:              /* AMD x86-64 architecture */
			return loc_expr((Dwarf_Unsigned[]) { DW_OP_breg0 + DWARF_X86_64_RSP, 0 }, lopc, hipc);
		default:
		return loc_expr();
	}
}

}
}
