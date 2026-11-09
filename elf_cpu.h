/* SPDX-License-Identifier: MIT */
#ifndef ELF_CPU_H
#define ELF_CPU_H
/* Which CPU an ELF names, and which machine runs it.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * A hunk file is not ELF: the caller records it as 68k. An ELF file's
 * e_machine is recorded with its seglist (the anchor's word at contents
 * +24, which is 28 bytes past the BPTR). No Amiga headers: the host test
 * and elf.library build this file.
 *
 * team5/design-ethos-processing-20261109.md. hostlib ABI 2 is not in
 * force; ELF_RUN_NATIVE is that route, and this file does not implement it.
 */
#include <stddef.h>
#include <stdint.h>

#define ELF_CPU_68K     4u      /* EM_68K */
#define ELF_CPU_PPC     20u     /* EM_PPC */
#define ELF_CPU_PPC64   21u     /* EM_PPC64 */
#define ELF_CPU_386     3u      /* EM_386 */
#define ELF_CPU_ARM     40u     /* EM_ARM */
#define ELF_CPU_X86_64  62u     /* EM_X86_64 */
#define ELF_CPU_AARCH64 183u    /* EM_AARCH64 */
#define ELF_ANCHOR_MAGIC_CPU 0x4C534658u

enum {
    ELF_RUN_AC090 = 1,          /* 68k: AC090, or a real 680x0 */
    ELF_RUN_AC460 = 2,          /* 32-bit PowerPC, and the host is not one */
    ELF_RUN_NATIVE = 3,         /* the file's CPU is the host's: hostlib ABI 2 */
    ELF_RUN_REFUSE = 4
};

/* 0 when b holds an ELF header (20 bytes is enough). machine, type and
 * osabi may be NULL. -1 when it is not ELF or is cut short. */
int elf_read_header(const uint8_t *b, size_t n, uint32_t *machine, uint32_t *type, int *osabi);

/* The CPU recorded on a seglist, or ELF_CPU_68K when it has no anchor
 * (a hunk file, a 68k ELF file). bptr is the byte the BPTR names; size is
 * the long before it, in bytes. */
uint32_t elf_seg_cpu(const uint8_t *bptr, uint32_t size);

/* Write machine at the anchor's CPU word. 0, or -1 when it is not an
 * anchor with room for the word. */
int elf_seg_stamp(uint8_t *bptr, uint32_t size, uint32_t machine);

/* Which machine runs this CPU. host is an ELF e_machine, or 0 for a real
 * 680x0 (treated as ELF_CPU_68K). why receives the refusal, or the route
 * that is not connected; it may be NULL. */
int elf_dispatch(uint32_t machine, uint32_t host, char *why, size_t why_n);

#endif
