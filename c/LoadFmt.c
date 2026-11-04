/* LoadFmt: what LoadSeg, NewLoadSeg and InternalLoadSeg make of a file, for
 * the lab (amicradle-guest libraries/opendos/DESIGN-LOADSEG-FORMATS.md).
 *   LoadFmt FILE...          each file through the three calls: whether a
 *                            seglist came back and IoErr(), then UnLoadSeg
 *   LoadFmt LOOP N FILE      LoadSeg and UnLoadSeg N times, and the free
 *                            memory before and after (a leak check)
 *   LoadFmt WHY FILE         LoadSeg, then PrintFault(IoErr()) and, when
 *                            elf.library is in memory, its reason
 * Prints no addresses, so a run on OS 3.2.3 and one on the open ROM can be
 * compared line by line. Built like ElfTest: m68k ELF GCC, mkhunk.py.
 * Copyright (c) 2026 AmiCradle Team. MIT. */
#include "../openelf_amiga.h"
#include "../openelf_public.h"

extern APTR SysBase;
APTR DOSBase;

#define AvailMem(flags) SysCall(-216, 0, flags, 0, 0)
#define FindName(list, name) ((APTR)SysCall(-276, 0, 0, list, name))
#define NewLoadSeg(name, tags) ((BPTR)DosCall(-768, name, tags, 0))
#define LoadSeg(name) ((BPTR)DosCall(-150, name, 0, 0))
#define PrintFault(code, head) DosCall(-474, code, head, 0)

/* InternalLoadSeg's three functions, as dos calls them: read with D1 fh,
 * D2 buffer, D3 length and A6 DOSBase; alloc with D0 size, D1 flags and A6
 * SysBase; free with A1 memory, D0 size and A6 SysBase. Each is the system
 * call itself, so the stubs only jump into it. */
__asm__(
    "        .text\n"
    "lf_read:  jmp -42(%a6)\n"
    "lf_alloc: jmp -198(%a6)\n"
    "lf_free:  jmp -210(%a6)\n");
extern char lf_read[], lf_alloc[], lf_free[];
static const LONG lf_funcs[3] = { (LONG)lf_read, (LONG)lf_alloc, (LONG)lf_free };

static BPTR internal_loadseg(BPTR fh)
{
    register ULONG d0 __asm("d0") = (ULONG)fh;
    register ULONG d1 __asm("d1");
    register ULONG a0 __asm("a0") = 0;
    register ULONG a1 __asm("a1") = (ULONG)lf_funcs;
    register ULONG a2 __asm("a2") = 0;
    __asm volatile ("move.l %%a6,-(%%sp)\n\tmove.l %5,%%a6\n\tjsr -756(%%a6)\n\tmove.l (%%sp)+,%%a6"
                    : "+r"(d0), "=r"(d1), "+r"(a0), "+r"(a1), "+r"(a2) : "a"(DOSBase) : "cc", "memory");
    return (BPTR)d0;
}

static void internal_unloadseg(BPTR seg)
{
    register ULONG d0 __asm("d0");
    register ULONG d1 __asm("d1") = (ULONG)seg;
    register ULONG a0 __asm("a0");
    register ULONG a1 __asm("a1") = (ULONG)lf_free;
    __asm volatile ("move.l %%a6,-(%%sp)\n\tmove.l %4,%%a6\n\tjsr -762(%%a6)\n\tmove.l (%%sp)+,%%a6"
                    : "=r"(d0), "+r"(d1), "=r"(a0), "+r"(a1) : "a"(DOSBase) : "cc", "memory");
}

static void say(const char *fmt, ULONG a, ULONG b, ULONG c)
{
    ULONG v[3];
    v[0] = a; v[1] = b; v[2] = c;
    VPrintf(fmt, v);
}

/* the number of segments, so two runs can compare shapes without addresses */
static ULONG nsegs(BPTR seg)
{
    ULONG n = 0;
    while (seg && n < 10000) { seg = *(BPTR *)BADDR(seg); n++; }
    return n;
}

/* elf.library's own words for its last refusal, when it is in memory */
static void elf_why(void)
{
    APTR elf;
    char name[64];
    LONG e;
    Forbid();
    elf = FindName((char *)SysBase + 378, ELFNAME);      /* ExecBase LibList */
    Permit();
    if (!elf || ((struct Library *)elf)->lib_Version < 2) { PutStr("  elf.library: not in memory, or 1.x\n"); return; }
    if (!(elf = OpenLibrary(ELFNAME, 2))) return;
    e = (LONG)lib_call4(elf, LVO_ElfLastError, sizeof name, 0, (ULONG)name, 0);
    say("  elf.library: %ld %s (%s)\n", (ULONG)e, lib_call4(elf, LVO_ElfErrorString, (ULONG)e, 0, 0, 0), (ULONG)name);
    CloseLibrary(elf);
}

/* each answer, with IoErr() taken at once (printing changes it) */
static void show(const char *file, const char *call, BPTR seg)
{
    LONG e = seg ? 0 : IoErr();
    say("%s: %s %s", (ULONG)file, (ULONG)call, (ULONG)(seg ? "seglist" : "0"));
    say(" %ld IoErr %ld\n", seg ? nsegs(seg) : 0, (ULONG)e, 0);
}

static void one(const char *file)
{
    BPTR seg, fh;
    seg = LoadSeg(file);
    show(file, "LoadSeg", seg);
    if (seg) UnLoadSeg(seg);
    seg = NewLoadSeg(file, 0);
    show(file, "NewLoadSeg", seg);
    if (seg) UnLoadSeg(seg);
    if (!(fh = Open(file, MODE_OLDFILE))) { say("%s: can't open (%ld)\n", (ULONG)file, (ULONG)IoErr(), 0); return; }
    seg = internal_loadseg(fh);
    show(file, "InternalLoadSeg", seg);
    if (seg) internal_unloadseg(seg);
    Close(fh);
}

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

static int is(const char *a, const char *b)
{
    while (*a && *b && (*a | 32) == (*b | 32)) a++, b++;
    return !*a && !*b;
}

int amiga_main(const char *args, long len)
{
    static char line[512];
    char *p = line, *w;
    long i;
    for (i = 0; i < len && i < (long)sizeof line - 1; i++) line[i] = args[i];
    line[i] = 0;
    if (!(DOSBase = OpenLibrary("dos.library", 36))) return 20;
    w = word(&p);
    if (!w) PutStr("usage: LoadFmt FILE... | LOOP N FILE | WHY FILE\n");
    else if (is(w, "loop")) {
        char *n = word(&p), *file = word(&p);
        ULONG count = 0, k, before, after, ok = 0;
        while (n && *n >= '0' && *n <= '9') count = count * 10 + (ULONG)(*n++ - '0');
        if (!file) { PutStr("usage: LoadFmt LOOP N FILE\n"); goto out; }
        /* one load first, so a library it opens is in memory before counting */
        { BPTR s = LoadSeg(file); if (s) UnLoadSeg(s); }
        before = AvailMem(0);
        for (k = 0; k < count; k++) {
            BPTR s = LoadSeg(file);
            if (s) { ok++; UnLoadSeg(s); }
        }
        after = AvailMem(0);
        say("%s: %ld of %ld loads", (ULONG)file, ok, count);
        say(", free memory %ld before, %ld after\n", before, after, 0);
        say("LEAK %ld bytes\n", before - after, 0, 0);
    } else if (is(w, "why")) {
        char *file = word(&p);
        BPTR s;
        if (!file) { PutStr("usage: LoadFmt WHY FILE\n"); goto out; }
        s = LoadSeg(file);
        if (s) { say("%s: loaded, %ld segments\n", (ULONG)file, nsegs(s), 0); UnLoadSeg(s); }
        else { LONG e = IoErr(); say("%s: IoErr %ld\n", (ULONG)file, (ULONG)e, 0); PrintFault(e, file); elf_why(); }
    } else {
        do one(w); while ((w = word(&p)));
    }
out:
    CloseLibrary(DOSBase);
    return 0;
}
