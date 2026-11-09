/* SPDX-License-Identifier: MIT */
/* Host test for elf_cpu.c. A small ELF header per e_machine, a wrong CPU,
 * a hunk (not ELF), and an anchor that records the CPU.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *   cc -Wall -Werror -o test_elf_cpu tests/test_elf_cpu.c elf_cpu.c && ./test_elf_cpu
 */
#include <stdio.h>
#include <string.h>
#include "../elf_cpu.h"

static int fails;

static void expect(const char *name, int cond)
{
    if (!cond) { fprintf(stderr, "FAIL %s\n", name); fails++; }
}

static void hdr(uint8_t *b, int le, uint16_t type, uint16_t machine, uint8_t osabi)
{
    memset(b, 0, 20);
    b[0] = 0x7f; b[1] = 'E'; b[2] = 'L'; b[3] = 'F';
    b[4] = 1; b[5] = (uint8_t)(le ? 1 : 2); b[6] = 1; b[7] = osabi;
    if (le) { b[16] = (uint8_t)type; b[17] = (uint8_t)(type >> 8); b[18] = (uint8_t)machine; b[19] = (uint8_t)(machine >> 8); }
    else { b[16] = (uint8_t)(type >> 8); b[17] = (uint8_t)type; b[18] = (uint8_t)(machine >> 8); b[19] = (uint8_t)machine; }
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

/* [size][next][jmp entry][$4AFC][magic][fn][~fn][data][cpu], as a loader's anchor */
static void anchor(uint8_t *s, uint32_t fn, uint32_t machine)
{
    memset(s, 0, 40);
    put32(s, 40);
    put32(s + 8, 0x4EF90000); /* the 4EF9 is the first word of contents, at s+8 */
    s[8] = 0x4E; s[9] = 0xF9;
    put32(s + 10, 0);
    s[14] = 0x4A; s[15] = 0xFC;
    put32(s + 16, ELF_ANCHOR_MAGIC_CPU);
    put32(s + 20, fn);
    put32(s + 24, ~fn);
    put32(s + 32, machine);
}

int main(void)
{
    uint8_t b[20], s[40];
    uint32_t machine = 0, type = 0;
    int osabi = -1;
    char why[96];
    int run;

    hdr(b, 0, 2, ELF_CPU_68K, 0);
    expect("68k header", elf_read_header(b, 20, &machine, &type, &osabi) == 0 && machine == ELF_CPU_68K && type == 2);
    hdr(b, 1, 1, ELF_CPU_PPC, 0);
    expect("ppc header", elf_read_header(b, 20, &machine, &type, &osabi) == 0 && machine == ELF_CPU_PPC && type == 1);
    hdr(b, 1, 2, ELF_CPU_X86_64, 0);
    expect("x86-64 header", elf_read_header(b, 20, &machine, &type, &osabi) == 0 && machine == ELF_CPU_X86_64);
    hdr(b, 1, 1, ELF_CPU_AARCH64, 15);
    expect("aarch64 header", elf_read_header(b, 20, &machine, &type, &osabi) == 0 && machine == ELF_CPU_AARCH64 && osabi == 15);
    hdr(b, 1, 1, ELF_CPU_386, 0);
    expect("i386 header", elf_read_header(b, 20, &machine, &type, &osabi) == 0 && machine == ELF_CPU_386);
    expect("short", elf_read_header(b, 10, &machine, &type, &osabi) != 0);
    b[0] = 'H';
    expect("hunk is not ELF", elf_read_header(b, 20, &machine, &type, &osabi) != 0);

    anchor(s, 0x1000, ELF_CPU_X86_64);
    expect("anchor cpu", elf_seg_cpu(s + 4, 40) == ELF_CPU_X86_64);
    expect("stamp ppc", elf_seg_stamp(s + 4, 40, ELF_CPU_PPC) == 0 && elf_seg_cpu(s + 4, 40) == ELF_CPU_PPC);
    expect("no anchor is 68k", elf_seg_cpu(s + 4, 8) == ELF_CPU_68K);
    s[8] = 0;
    expect("broken anchor is 68k", elf_seg_cpu(s + 4, 40) == ELF_CPU_68K);

    run = elf_dispatch(ELF_CPU_68K, 0, why, sizeof why);
    expect("68k on a real machine is AC090", run == ELF_RUN_AC090);
    run = elf_dispatch(ELF_CPU_68K, ELF_CPU_X86_64, why, sizeof why);
    expect("68k under an x86-64 host is AC090", run == ELF_RUN_AC090);
    run = elf_dispatch(ELF_CPU_PPC, ELF_CPU_X86_64, why, sizeof why);
    expect("ppc is AC460", run == ELF_RUN_AC460 && strstr(why, "AC460"));
    run = elf_dispatch(ELF_CPU_PPC, ELF_CPU_PPC, why, sizeof why);
    expect("ppc on a ppc host is native", run == ELF_RUN_NATIVE);
    run = elf_dispatch(ELF_CPU_X86_64, ELF_CPU_X86_64, why, sizeof why);
    expect("x86-64 on its host is native", run == ELF_RUN_NATIVE && strstr(why, "ABI 2"));
    run = elf_dispatch(ELF_CPU_X86_64, ELF_CPU_68K, why, sizeof why);
    expect("x86-64 with no emulator", run == ELF_RUN_REFUSE && strcmp(why, "This program is x86-64 code; no x86 emulator yet") == 0);
    run = elf_dispatch(ELF_CPU_AARCH64, ELF_CPU_X86_64, why, sizeof why);
    expect("aarch64 refused", run == ELF_RUN_REFUSE && strcmp(why, "This program is AArch64 code; no AArch64 emulator yet") == 0);
    run = elf_dispatch(99, ELF_CPU_X86_64, why, sizeof why);
    expect("unknown machine", run == ELF_RUN_REFUSE && strcmp(why, "This program is code for machine 99; no emulator yet") == 0);

    if (fails) { fprintf(stderr, "%d failed\n", fails); return 1; }
    printf("elf_cpu: all checks passed\n");
    return 0;
}
