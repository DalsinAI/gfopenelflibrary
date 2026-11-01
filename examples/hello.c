/* Hello from an ELF program: built by the m68k ELF GCC (no Amiga
 * toolchain), linked with crt/amiga_start.S, loaded by elf.library.
 *   ld -q (an executable that keeps its relocations), ld -r (an object, as
 *   AROS and MorphOS load), or -pie: all three run the same.
 * Copyright (c) 2026 AmiCradle Team. MIT. */
#include "../openelf_amiga.h"

extern APTR SysBase;
APTR DOSBase;
static int counter;                     /* .bss, cleared by the loader */
static const char *const words[] = { "Hello", "from", "an", "ELF", "program" };

int amiga_main(const char *args, long len)
{
    ULONG v[2];
    int i;
    if (!(DOSBase = OpenLibrary("dos.library", 36))) return 20;
    for (i = 0; i < 5; i++) {
        PutStr(words[i]);
        PutStr(i < 4 ? " " : "\n");
        counter++;
    }
    v[0] = (ULONG)counter;
    v[1] = (ULONG)len;
    VPrintf("words: %ld, argument bytes: %ld\n", v);
    if (len > 1) { PutStr("arguments: "); PutStr(args); }
    CloseLibrary(DOSBase);
    return counter == 5 ? 0 : 10;
}
