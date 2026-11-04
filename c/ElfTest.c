/* ElfTest: elf.library's calls, for the lab.
 *   ElfTest FILE [ARGS]     ElfLoadSeg FILE, run it with RunCommand, unload
 *   ElfTest CALL FILE [ARGS] the same, but jumps to the seglist itself with
 *                           D0/A0 set, as DOS would (for emulators whose
 *                           RunCommand only runs their own seglists)
 *   ElfTest DL FILE SYMBOL  DLOpen FILE, DLSym SYMBOL, call it as int f(void)
 *   ElfTest DOS [FUNCS] [X86] FILE   elf.library 2.0's ElfLoadSegDOS, as
 *                           GFOpenDOS calls it: FILE opened and its first 4
 *                           bytes read, then the dispatch (FUNCS: through
 *                           InternalLoadSeg-style functions; X86: with a test
 *                           loader added for EM_386 whose anchor returns 42);
 *                           the seglist is called as with CALL, or IoErr()
 *                           and ElfLoadWhy are printed
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

/* InternalLoadSeg's functions for DOS FUNCS: each the system call itself */
__asm__(
    "        .text\n"
    "et_read:  jmp -42(%a6)\n"
    "et_alloc: jmp -198(%a6)\n"
    "et_free:  jmp -210(%a6)\n"
    "et_load_regs:\n"                  /* el_Load: A0 message, A1 loader */
    "        move.l  %a1,-(%sp)\n"
    "        move.l  %a0,-(%sp)\n"
    "        jsr     et_load\n"
    "        addq.l  #8,%sp\n"
    "        rts\n"
    "et_anchor_code:\n"
    "        moveq   #42,%d0\n"
    "        rts\n");
extern char et_read[], et_alloc[], et_free[], et_load_regs[], et_anchor_code[];
static const LONG et_funcs[3] = { (LONG)et_read, (LONG)et_alloc, (LONG)et_free };

/* the X86 test loader: an anchor whose entry returns 42 */
static struct ElfLoader et_loader;
static ULONG et_seen_machine, et_seen_type;
BPTR et_load(struct ElfLoadMsg *m, struct ElfLoader *l)
{
    ULONG *s = AllocMem(8 + ELF_ANCHOR_SIZE, MEMF_PUBLIC | MEMF_CLEAR);
    UBYTE *c;
    if (!s) return 0;
    et_seen_machine = m->elm_Machine;
    et_seen_type = m->elm_Type;
    s[0] = 8 + ELF_ANCHOR_SIZE;
    c = (UBYTE *)(s + 2);
    *(UWORD *)c = 0x4ef9;
    *(ULONG *)(c + 2) = (ULONG)et_anchor_code;
    *(UWORD *)(c + 6) = 0x4afc;
    *(ULONG *)(c + 8) = ELF_ANCHOR_MAGIC;
    *(ULONG *)(c + 12) = (ULONG)l->el_Unload;
    *(ULONG *)(c + 16) = ~(ULONG)l->el_Unload;
    l->el_Count++;
    return MKBADDR(s + 1);
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
    if (!first) PutStr("usage: ElfTest FILE [ARGS] | ElfTest DL FILE SYMBOL | ElfTest DOS [FUNCS] [X86] FILE\n"), rc = 5;
    else if ((first[0] | 32) == 'd' && (first[1] | 32) == 'o' && (first[2] | 32) == 's' && !first[3]) {
        char *w = word(&p), *file = 0;
        int funcs = 0, x86 = 0;
        APTR e2 = 0;
        for (; w; w = word(&p)) {
            if ((w[0] | 32) == 'f' && (w[1] | 32) == 'u' && (w[2] | 32) == 'n') funcs = 1;
            else if ((w[0] | 32) == 'x' && w[1] == '8' && w[2] == '6' && !w[3]) x86 = 1;
            else file = w;
        }
        if (!file || !(e2 = OpenLibrary(ELFNAME, ELF_VERSION_DISPATCH))) { PutStr(file ? "ElfTest: can't open elf.library 2\n" : "usage: ElfTest DOS [FUNCS] [X86] FILE\n"); rc = 5; }
        else {
            BPTR fh = Open(file, MODE_OLDFILE);
            UBYTE b[4];
            ULONG seg;
            if (x86) {
                et_loader.el_Machine = 3;
                et_loader.el_OSABI = ELF_OSABI_ANY;
                et_loader.el_Name = "ElfTest's x86 loader";
                et_loader.el_Load = et_load_regs;
                say("ElfAddLoader = %ld\n", lib_call4(e2, LVO_ElfAddLoader, 0, 0, (ULONG)&et_loader, 0), 0);
            }
            if (!fh || Read(fh, b, 4) != 4) { PutStr("ElfTest: can't read the file\n"); rc = 10; }
            else {
                register ULONG d0 __asm("d0");
                register ULONG d1 __asm("d1") = (ULONG)fh;
                register ULONG d2 __asm("d2") = 1;      /* ELFKIND_ELF */
                register ULONG a0 __asm("a0") = funcs ? (ULONG)et_funcs : 0;
                register ULONG a1 __asm("a1");
                __asm volatile ("move.l %%a6,-(%%sp)\n\tmove.l %6,%%a6\n\tjsr -78(%%a6)\n\tmove.l (%%sp)+,%%a6"
                                : "=r"(d0), "+r"(d1), "+r"(d2), "+r"(a0), "=r"(a1) : "0"(0), "a"(e2) : "cc", "memory");
                seg = d0;
                if (!seg) {
                    LONG err = IoErr();
                    ULONG wy = lib_call4(e2, LVO_ElfLoadWhy, 0, 0, 0, 0);
                    say("ElfLoadSegDOS %s: IoErr %ld\n", (ULONG)file, (ULONG)err);
                    say("why: %s\n", wy ? wy : (ULONG)"(none)", 0);
                    rc = 10;
                } else {
                    char nl[2] = { '\n', 0 };
                    say("ElfLoadSegDOS %s = a seglist\n", (ULONG)file, 0);
                    if (x86) say("the loader saw machine %ld, type %ld\n", et_seen_machine, et_seen_type);
                    say("returned %ld\n", (ULONG)call_seglist(seg, nl, 1), 0);
                    while (seg) {
                        ULONG *s = (ULONG *)(seg << 2) - 1;
                        seg = s[1];
                        FreeMem(s, s[0]);       /* et_alloc is AllocMem */
                    }
                    if (x86) et_loader.el_Count--;
                }
            }
            if (fh) Close(fh);
            if (x86) say("ElfRemLoader = %ld\n", lib_call4(e2, LVO_ElfRemLoader, 0, 0, (ULONG)&et_loader, 0), 0);
            CloseLibrary(e2);
        }
    }
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
