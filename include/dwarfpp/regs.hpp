#ifndef DWARFPP_REGS_HPP_
#define DWARFPP_REGS_HPP_

// DWARF x86 register numbers pilfered from libunwind/src/x86/unwind_i.h
enum dwarf_regs_x86
{
	DWARF_X86_EAX  =  0,
	DWARF_X86_ECX  =  1,
	DWARF_X86_EDX  =  2,
	DWARF_X86_EBX     = 3,
	DWARF_X86_ESP     = 4,
	DWARF_X86_EBP     = 5,
	DWARF_X86_ESI     = 6,
	DWARF_X86_EDI     = 7,
	DWARF_X86_EIP     = 8,
	DWARF_X86_EFLAGS  = 9,
	DWARF_X86_TRAPNO  = 10,
	DWARF_X86_ST0     = 11
};
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
	"sto"
};

// similar for x86-64
enum dwarf_regs_x86_64
{
	DWARF_X86_64_RAX     = 0,
	DWARF_X86_64_RDX     = 1,
	DWARF_X86_64_RCX     = 2,
	DWARF_X86_64_RBX     = 3,
	DWARF_X86_64_RSI     = 4,
	DWARF_X86_64_RDI     = 5,
	DWARF_X86_64_RBP     = 6,
	DWARF_X86_64_RSP     = 7,
	DWARF_X86_64_R8      = 8,
	DWARF_X86_64_R9      = 9,
	DWARF_X86_64_R10     = 10,
	DWARF_X86_64_R11     = 11,
	DWARF_X86_64_R12     = 12,
	DWARF_X86_64_R13     = 13,
	DWARF_X86_64_R14     = 14,
	DWARF_X86_64_R15     = 15,
	DWARF_X86_64_RIP     = 16
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
	"rip"
};

inline const char **dwarf_regnames_for_elf_machine(int e_machine)
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

#endif
