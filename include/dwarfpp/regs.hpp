#ifndef DWARFPP_REGS_HPP_
#define DWARFPP_REGS_HPP_

// DWARF x86 register numbers pilfered from libunwind/src/x86/unwind_i.h
#define DWARF_X86_EAX     0
#define DWARF_X86_ECX     1
#define DWARF_X86_EDX     2
#define DWARF_X86_EBX     3
#define DWARF_X86_ESP     4
#define DWARF_X86_EBP     5
#define DWARF_X86_ESI     6
#define DWARF_X86_EDI     7
#define DWARF_X86_EIP     8
#define DWARF_X86_EFLAGS  9
#define DWARF_X86_TRAPNO  10
#define DWARF_X86_ST0     11

// similar for x86-64
#define DWARF_X86_64_RAX     0
#define DWARF_X86_64_RDX     1
#define DWARF_X86_64_RCX     2
#define DWARF_X86_64_RBX     3
#define DWARF_X86_64_RSI     4
#define DWARF_X86_64_RDI     5
#define DWARF_X86_64_RBP     6
#define DWARF_X86_64_RSP     7
#define DWARF_X86_64_R8      8
#define DWARF_X86_64_R9      9
#define DWARF_X86_64_R10     10
#define DWARF_X86_64_R11     11
#define DWARF_X86_64_R12     12
#define DWARF_X86_64_R13     13
#define DWARF_X86_64_R14     14
#define DWARF_X86_64_R15     15
#define DWARF_X86_64_RIP     16

#endif
