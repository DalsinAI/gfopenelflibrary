/* ElfLoaderDemo: a foreign loader through elf.library 2.0's ElfAddLoader,
 * to show (and test, in the lab) the hook Stage 5's loaders will use
 * (amicradle-guest libraries/opendos/DESIGN-LOADSEG-FORMATS.md).
 *   Run >NIL: ElfLoaderDemo [OSABI=n]    takes x86 (EM_386) ELF files until
 *                                        Ctrl-C, then takes itself out
 * A file it takes loads as an anchor: a 68k segment whose code says what
 * would have run, so the Shell and RunCommand start it as any program;
 * UnLoadSeg hands the anchor back to the loader (its el_Unload), which
 * frees it. At the end it prints how many it loaded and freed (they must
 * match, or ElfRemLoader refuses and it waits for the rest).
 * Copyright (c) 2026 AmiCradle Team. MIT. */
#include "../openelf_amiga.h"
#include "../openelf_public.h"

extern APTR SysBase;
APTR DOSBase;
static APTR Elf;
static ULONG loaded, freed;

#define Wait(sigs) SysCall(-318, sigs, 0, 0, 0)
#define SIGBREAKF_CTRL_C 0x1000UL

/* el_Load (A0 message, A1 loader, A6 elf.library), el_Unload (D1 seglist,
 * A0 user data, A6 DOSBase) and the anchor's 68k entry (D0 length, A0
 * arguments): each from its registers to C */
BPTR demo_load(struct ElfLoadMsg *m, struct ElfLoader *l);
void demo_unload(BPTR seg);
LONG demo_run(void);
__asm__(
    "        .text\n"
    "demo_load_regs:\n"
    "        move.l  %a1,-(%sp)\n"
    "        move.l  %a0,-(%sp)\n"
    "        jsr     demo_load\n"
    "        addq.l  #8,%sp\n"
    "        rts\n"
    "demo_unload_regs:\n"
    "        move.l  %d1,-(%sp)\n"
    "        jsr     demo_unload\n"
    "        addq.l  #4,%sp\n"
    "        rts\n"
    "demo_entry:\n"
    "        movem.l %d2-%d7/%a2-%a6,-(%sp)\n"
    "        jsr     demo_run\n"
    "        movem.l (%sp)+,%d2-%d7/%a2-%a6\n"
    "        rts\n");
extern char demo_load_regs[], demo_unload_regs[], demo_entry[];

static struct ElfLoader loader;

static void say(const char *fmt, ULONG a, ULONG b)
{
    ULONG v[2];
    v[0] = a; v[1] = b;
    VPrintf(fmt, v);
}

#define ANCHOR (8 + ELF_ANCHOR_SIZE + 8)

/* the anchor: [size][next], jmp demo_entry, $4AFC, the magic, el_Unload
 * and its check, the user data, then the file's machine and type */
BPTR demo_load(struct ElfLoadMsg *m, struct ElfLoader *l)
{
    UBYTE *s;
    ULONG fn = (ULONG)l->el_Unload;
    if (m->elm_Type == 2 && m->elm_OSABI == 0) {
        /* refuse one kind, to show the loader's own words reach the Shell */
        const char *w = "ElfLoaderDemo takes x86 objects, not programs";
        ULONG i;
        for (i = 0; w[i] && i + 1 < m->elm_WhySize; i++) m->elm_Why[i] = w[i];
        m->elm_Why[i] = 0;
        SetIoErr(ERROR_OBJECT_WRONG_TYPE);
        return 0;
    }
    if (!(s = AllocMem(ANCHOR, MEMF_PUBLIC | MEMF_CLEAR))) { SetIoErr(ERROR_NO_FREE_STORE); return 0; }
    *(ULONG *)s = ANCHOR;
    *(UWORD *)(s + 8) = 0x4ef9;
    *(ULONG *)(s + 10) = (ULONG)demo_entry;
    *(UWORD *)(s + 14) = 0x4afc;
    *(ULONG *)(s + 16) = ELF_ANCHOR_MAGIC;
    *(ULONG *)(s + 20) = fn;
    *(ULONG *)(s + 24) = ~fn;
    *(ULONG *)(s + 28) = (ULONG)l->el_UserData;
    *(ULONG *)(s + 32) = m->elm_Machine;
    *(ULONG *)(s + 36) = m->elm_Type;
    loaded++;
    l->el_Count++;
    CacheClearU();
    return MKBADDR(s + 4);
}

void demo_unload(BPTR seg)
{
    UBYTE *s = (UBYTE *)BADDR(seg) - 4;
    freed++;
    loader.el_Count--;
    FreeMem(s, *(ULONG *)s);
}

LONG demo_run(void)
{
    APTR dos = OpenLibrary("dos.library", 36);
    if (dos) {
        ULONG v[1];
        v[0] = loaded;
        dos_call3(dos, -954, (ULONG)"ElfLoaderDemo: an x86 program would start here (load %ld)\n", (ULONG)v, 0);
        CloseLibrary(dos);
    }
    return 0;
}

int amiga_main(const char *args, long len)
{
    LONG osabi = ELF_OSABI_ANY, i;
    for (i = 0; i + 6 < len; i++)
        if ((args[i] | 32) == 'o' && args[i + 5] == '=') {
            osabi = 0;
            for (i += 6; i < len && args[i] >= '0' && args[i] <= '9'; i++) osabi = osabi * 10 + (args[i] - '0');
            break;
        }
    if (!(DOSBase = OpenLibrary("dos.library", 36))) return 20;
    if (!(Elf = OpenLibrary(ELFNAME, ELF_VERSION_DISPATCH))) { PutStr("ElfLoaderDemo: can't open elf.library 2\n"); CloseLibrary(DOSBase); return 20; }
    loader.el_Machine = 3;      /* EM_386 */
    loader.el_OSABI = osabi;
    loader.el_Name = "ElfLoaderDemo";
    loader.el_Load = demo_load_regs;
    loader.el_Unload = demo_unload_regs;
    if (lib_call4(Elf, LVO_ElfAddLoader, 0, 0, (ULONG)&loader, 0)) { PutStr("ElfLoaderDemo: an x86 loader is in already\n"); goto out; }
    PutStr("ElfLoaderDemo: x86 loader in; Ctrl-C takes it out\n");
    for (;;) {
        Wait(SIGBREAKF_CTRL_C);
        if (!lib_call4(Elf, LVO_ElfRemLoader, 0, 0, (ULONG)&loader, 0)) break;
        say("ElfLoaderDemo: %ld anchors still out; waiting\n", loader.el_Count, 0);
    }
    say("ElfLoaderDemo: out; loaded %ld, unloaded %ld\n", loaded, freed);
out:
    CloseLibrary(Elf);
    CloseLibrary(DOSBase);
    return 0;
}
