/* ElfTest: elf.library's calls, for the lab.
 *   ElfTest FILE [ARGS]     ElfLoadSeg FILE, run it with RunCommand, unload
 *   ElfTest CALL FILE [ARGS] the same, but jumps to the seglist itself with
 *                           D0/A0 set, as DOS would (for emulators whose
 *                           RunCommand only runs their own seglists)
 *   ElfTest DL FILE SYMBOL  DLOpen FILE, DLSym SYMBOL, call it as int f(void)
 * Prints each step and elf.library's reason for any failure.
 * Copyright (c) 2026 AmiCradle Team. MIT. */
#include "../openelf_amiga.h"
#include "../openelf_public.h"

extern APTR SysBase;
APTR DOSBase;
static APTR Elf;

#define E4(lvo, d0, d1, a0, a1) lib_call4(Elf, lvo, (ULONG)(d0), (ULONG)(d1), (ULONG)(a0), (ULONG)(a1))

static void say(const char *fmt, ULONG a, ULONG b)
{
    ULONG v[2];
    v[0] = a; v[1] = b;
    VPrintf(fmt, v);
}

static void why(void)
{
    char name[64];
    LONG e = (LONG)E4(LVO_ElfLastError, sizeof name, 0, name, 0);
    say("  elf.library: %s (%s)\n", E4(LVO_ElfErrorString, e, 0, 0, 0), (ULONG)name);
}

/* a seglist's first code with the argument line in A0 and its length in D0 */
static LONG call_seglist(ULONG seg, const char *args, ULONG len)
{
    register ULONG d0 __asm("d0") = len;
    register const char *a0 __asm("a0") = args;
    register ULONG a1 __asm("a1") = (seg << 2) + 4;
    register ULONG d1 __asm("d1");
    __asm volatile ("movem.l %%d2-%%d7/%%a2-%%a6,-(%%sp)\n\tjsr (%%a1)\n\tmovem.l (%%sp)+,%%d2-%%d7/%%a2-%%a6"
                    : "+r"(d0), "+r"(a0), "+r"(a1), "=r"(d1) : : "cc", "memory");
    return (LONG)d0;
}

/* the next word of the line, NUL-terminated in place */
static char *word(char **p)
{
    char *s = *p, *w;
    while (*s == ' ' || *s == '\t') s++;
    if (!*s || *s == '\n') return 0;
    w = s;
    while (*s && *s != ' ' && *s != '\t' && *s != '\n') s++;
    if (*s) *s++ = 0;
    *p = s;
    return w;
}

int amiga_main(const char *args, long len)
{
    static char line[512];
    char *p = line, *first;
    long i;
    int rc = 0;
    for (i = 0; i < len && i < (long)sizeof line - 1; i++) line[i] = args[i];
    line[i] = 0;
    if (!(DOSBase = OpenLibrary("dos.library", 36))) return 20;
    if (!(Elf = OpenLibrary(ELFNAME, ELF_VERSION))) { PutStr("ElfTest: can't open elf.library 1\n"); CloseLibrary(DOSBase); return 20; }
    first = word(&p);
    if (!first) PutStr("usage: ElfTest FILE [ARGS] | ElfTest DL FILE SYMBOL\n"), rc = 5;
    else if ((first[0] | 32) == 'd' && (first[1] | 32) == 'l' && !first[2]) {
        char *file = word(&p), *sym = word(&p);
        APTR h;
        if (!file || !sym) { PutStr("usage: ElfTest DL FILE SYMBOL\n"); rc = 5; }
        else if (!(h = (APTR)E4(LVO_DLOpen, RTLD_NOW, 0, file, 0))) { say("DLOpen %s failed\n", (ULONG)file, 0); why(); rc = 10; }
        else {
            ULONG f = E4(LVO_DLSym, 0, 0, h, sym);
            say("DLOpen %s = $%lx\n", (ULONG)file, (ULONG)h);
            if (!f) { say("DLSym %s: not found\n", (ULONG)sym, 0); rc = 10; }
            else say("DLSym %s = $%lx, ", (ULONG)sym, f), say("it returns %ld\n", (ULONG)((int (*)(void))f)(), 0);
            say("DLClose = %ld\n", E4(LVO_DLClose, 0, 0, h, 0), 0);
        }
    } else {
        ULONG seg;
        int direct = 0;
        if ((first[0] | 32) == 'c' && (first[1] | 32) == 'a' && (first[2] | 32) == 'l' && (first[3] | 32) == 'l' && !first[4]) {
            direct = 1;
            first = word(&p);
            if (!first) { PutStr("usage: ElfTest CALL FILE [ARGS]\n"); goto out; }
        }
        char rest[512];
        long n = 0;
        seg = dos_call3(Elf, LVO_ElfLoadSeg, (ULONG)first, 0, 0);
        if (!seg) { say("ElfLoadSeg %s failed\n", (ULONG)first, 0); why(); rc = 10; }
        else {
            while (*p && n < (long)sizeof rest - 2) rest[n++] = *p++;
            rest[n++] = '\n';
            rest[n] = 0;
            say("ElfLoadSeg %s = seglist $%lx\n", (ULONG)first, seg << 2);
            say("returned %ld\n", (ULONG)(direct ? call_seglist(seg, rest, n) : RunCommand(seg, 16384, rest, n)), 0);
            if (!direct) UnLoadSeg(seg);
            else while (seg) {
                /* what UnLoadSeg does: each segment's size is the longword before its link */
                ULONG *s = (ULONG *)(seg << 2) - 1;
                seg = s[1];
                FreeMem(s, s[0]);
            }
        }
    }
out:
    CloseLibrary(Elf);
    CloseLibrary(DOSBase);
    return rc;
}
