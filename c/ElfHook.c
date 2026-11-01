/* ElfHook: ELF programs from the Shell and Workbench. Opens elf.library and
 * has it patch LoadSeg (hunks are still tried first).
 *   ElfHook         installs the hook (again is fine)
 *   ElfHook OFF     takes it out, when nobody patched over it
 * Copyright (c) 2026 AmiCradle Team. MIT. */
#include "../openelf_amiga.h"
#include "../openelf_public.h"

extern APTR SysBase;
APTR DOSBase;

static LONG elf_hook(APTR base, LONG on) { return (LONG)lib_call4(base, LVO_ElfHook, (ULONG)on, 0, 0, 0); }

int amiga_main(const char *args, long len)
{
    APTR elf;
    LONG off = 0, was, i;
    for (i = 0; i + 2 < len; i++)
        if ((args[i] | 32) == 'o' && (args[i + 1] | 32) == 'f' && (args[i + 2] | 32) == 'f') off = 1;
    if (!(DOSBase = OpenLibrary("dos.library", 36))) return 20;
    if (!(elf = OpenLibrary(ELFNAME, ELF_VERSION))) {
        PutStr("ElfHook: can't open elf.library 1\n");
        CloseLibrary(DOSBase);
        return 20;
    }
    was = elf_hook(elf, off ? 0 : 1);
    if (off)
        PutStr(was == -1 ? "ElfHook: another patch sits on LoadSeg; left in\n"
               : was ? "ElfHook: ELF loading off\n" : "ElfHook: was not on\n");
    else
        PutStr(was ? "ElfHook: ELF loading already on\n" : "ElfHook: ELF loading on (hunks first, then ELF)\n");
    CloseLibrary(elf);          /* the hook keeps its own count */
    CloseLibrary(DOSBase);
    return 0;
}
