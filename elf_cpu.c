/* SPDX-License-Identifier: MIT */
/* elf_cpu.h. Copyright (c) 2026 AmiCradle Team. MIT. */
#include "elf_cpu.h"

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint16_t u16(const uint8_t *p, int le) { return le ? (uint16_t)(p[0] | (p[1] << 8)) : be16(p); }

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static void copy_why(char *why, size_t n, const char *s)
{
    size_t i = 0;
    if (!why || !n) return;
    while (s[i] && i + 1 < n) { why[i] = s[i]; i++; }
    why[i] = 0;
}

int elf_read_header(const uint8_t *b, size_t n, uint32_t *machine, uint32_t *type, int *osabi)
{
    int le;
    if (!b || n < 20 || b[0] != 0x7f || b[1] != 'E' || b[2] != 'L' || b[3] != 'F') return -1;
    if (b[5] != 1 && b[5] != 2) return -1;
    le = b[5] == 1;
    if (machine) *machine = u16(b + 18, le);
    if (type) *type = u16(b + 16, le);
    if (osabi) *osabi = b[7];
    return 0;
}

/* contents start 4 bytes after the BPTR. The anchor check is dos's. */
static int anchor_ok(const uint8_t *p, uint32_t size, uint32_t need)
{
    if (!p || size < need) return 0;
    if (be16(p + 4) != 0x4EF9 || be16(p + 10) != 0x4AFC) return 0;
    if (be32(p + 12) != ELF_ANCHOR_MAGIC_CPU) return 0;
    if (be32(p + 16) != ~be32(p + 20)) return 0;
    return 1;
}

uint32_t elf_seg_cpu(const uint8_t *bptr, uint32_t size)
{
    if (!anchor_ok(bptr, size, 36)) return ELF_CPU_68K;
    return be32(bptr + 28);
}

int elf_seg_stamp(uint8_t *bptr, uint32_t size, uint32_t machine)
{
    if (!anchor_ok(bptr, size, 36)) return -1;
    put_be32(bptr + 28, machine);
    return 0;
}

static const char *cpu_what(uint32_t m)
{
    switch (m) {
    case ELF_CPU_386: return "x86";
    case ELF_CPU_PPC: return "PowerPC";
    case ELF_CPU_PPC64: return "64-bit PowerPC";
    case ELF_CPU_ARM: return "ARM";
    case ELF_CPU_X86_64: return "x86-64";
    case ELF_CPU_AARCH64: return "AArch64";
    case ELF_CPU_68K: return "68k";
    default: return 0;
    }
}

/* The emulator that does not exist yet. x86-64 and i386 are both "x86":
 * AC0x86 is not built. */
static const char *emu_what(uint32_t m)
{
    if (m == ELF_CPU_X86_64 || m == ELF_CPU_386) return "x86";
    return cpu_what(m);
}

static void add(char *buf, size_t n, const char *s)
{
    size_t i = 0;
    while (buf[i]) i++;
    while (*s && i + 1 < n) buf[i++] = *s++;
    buf[i] = 0;
}

int elf_dispatch(uint32_t machine, uint32_t host, char *why, size_t why_n)
{
    const char *what;
    char buf[96];
    if (!host) host = ELF_CPU_68K;
    if (machine == ELF_CPU_68K || machine == 0) {
        copy_why(why, why_n, "68k code: AC090");
        return ELF_RUN_AC090;
    }
    if (machine == host) {
        copy_why(why, why_n, "the host's own CPU: the hostlib ABI 2 route, not connected yet");
        return ELF_RUN_NATIVE;
    }
    if (machine == ELF_CPU_PPC) {
        copy_why(why, why_n, "This program is PowerPC code; AC460 is not connected");
        return ELF_RUN_AC460;
    }
    what = cpu_what(machine);
    buf[0] = 0;
    if (!what) {
        char rev[12];
        int k = 0, i;
        uint32_t v = machine;
        char num[12];
        do num[k++] = (char)('0' + v % 10); while ((v /= 10) && k < 11);
        for (i = 0; i < k; i++) rev[i] = num[k - 1 - i];
        rev[k] = 0;
        add(buf, sizeof buf, "This program is code for machine ");
        add(buf, sizeof buf, rev);
        add(buf, sizeof buf, "; no emulator yet");
    } else {
        add(buf, sizeof buf, "This program is ");
        add(buf, sizeof buf, what);
        add(buf, sizeof buf, " code; no ");
        add(buf, sizeof buf, emu_what(machine));
        add(buf, sizeof buf, " emulator yet");
    }
    copy_why(why, why_n, buf);
    return ELF_RUN_REFUSE;
}
