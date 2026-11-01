/* elf.library 1.0: its calls, for C programs and the tests.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * LVO   call                                   registers
 *  -30  BPTR  ElfLoadSeg(STRPTR name)           D1
 *  -36  LONG  ElfIdentify(APTR buf, ULONG len)  A0, D0
 *  -42  APTR  DLOpen(STRPTR name, ULONG flags)  A0, D0
 *  -48  APTR  DLSym(APTR handle, STRPTR name)   A0, A1
 *  -54  LONG  DLClose(APTR handle)              A0
 *  -60  LONG  ElfHook(LONG on)                  D0
 *  -66  LONG  ElfLastError(STRPTR buf, ULONG n) A0, D0
 *  -72  STRPTR ElfErrorString(LONG err)         D0
 *
 * ElfLoadSeg returns an ordinary seglist: UnLoadSeg frees it, RunCommand
 * and CreateProc run it. ElfIdentify answers an OE_ERR_ code (0 = a 68k ELF
 * file elf.library loads) from the first bytes (52 are enough). DLOpen
 * finds a name with no path in LIBS:elf/ and then the current directory,
 * opens what it needs (DT_NEEDED) first, binds every symbol at once and
 * runs its constructors; a name already open is counted again. DLSym with
 * a NULL handle searches every open shared object. ElfHook(1) makes
 * dos.library's LoadSeg and NewLoadSeg load ELF programs after the hunk
 * format has said no; ElfHook(0) takes it out. ElfLastError and
 * ElfErrorString explain the last failure (any task's). */
#ifndef OPENELF_PUBLIC_H
#define OPENELF_PUBLIC_H

#define ELFNAME "elf.library"
#define ELF_VERSION 1

#define LVO_ElfLoadSeg (-30)
#define LVO_ElfIdentify (-36)
#define LVO_DLOpen (-42)
#define LVO_DLSym (-48)
#define LVO_DLClose (-54)
#define LVO_ElfHook (-60)
#define LVO_ElfLastError (-66)
#define LVO_ElfErrorString (-72)

#define RTLD_LAZY 1             /* accepted; every symbol is bound at load */
#define RTLD_NOW 2
#define RTLD_GLOBAL 0x100       /* accepted; every shared object is global */

/* ElfIdentify's and ElfLastError's answers: elf_core.h's enum oe_error */
#define ELFERR_OK 0
#define ELFERR_NOT_ELF 1
#define ELFERR_CLASS 2
#define ELFERR_MACHINE 3
#define ELFERR_TYPE 4

#endif
