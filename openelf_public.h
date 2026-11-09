/* elf.library 2.0: its calls, for C programs and the tests.
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
 *  2.0, for dos.library's format dispatch (amicradle-guest
 *  libraries/opendos/DESIGN-LOADSEG-FORMATS.md):
 *  -78  BPTR  ElfLoadSegDOS(BPTR fh, ULONG kind, const LONG *funcs)  D1, D2, A0
 *  -84  LONG  ElfAddLoader(struct ElfLoader *l) A0
 *  -90  LONG  ElfRemLoader(struct ElfLoader *l) A0
 *  -96  STRPTR ElfLoadWhy(void)
 * -102  ULONG  ElfSegCPU(BPTR seg)          D1
 * -108  ULONG  ElfHostCPU(ULONG set)        D0
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
 * ElfErrorString explain the last failure (any task's).
 *
 * ElfLoadSegDOS is dos.library's: its hunk loader found a file it does not
 * load (kind ELFKIND_ELF: the file starts \x7fELF; ELFKIND_HUNK_PPC: a hunk
 * file with a HUNK_PPC_CODE hunk) on the open handle fh; funcs is
 * InternalLoadSeg's function array, or 0 for dos's own Read and AllocMem.
 * A 68k ELF file (EM_68K, any EI_OSABI: AROS's too) is loaded here; any
 * other goes to the loader added for its machine and EI_OSABI, if one is;
 * else it is refused as OS 3.2.3 refuses it (IoErr 212 for ELF, 235 for a
 * PowerPC hunk file) with the reason kept for ElfLoadWhy.
 *
 * ElfAddLoader adds a foreign loader (0, or -1 when one already takes that
 * machine and EI_OSABI); ElfRemLoader takes it out (0, or -1 when it is
 * not in, or still has seglists out: el_Count). ElfLoadWhy answers the
 * calling task's last refusal by ElfLoadSegDOS in words, once (then 0), for
 * the Shell and Workbench to show after "file is not executable". */
#ifndef OPENELF_PUBLIC_H
#define OPENELF_PUBLIC_H

#define ELFNAME "elf.library"
#define ELF_VERSION 1
#define ELF_VERSION_DISPATCH 2  /* ElfLoadSegDOS, the loaders, ElfLoadWhy */

#define LVO_ElfLoadSeg (-30)
#define LVO_ElfIdentify (-36)
#define LVO_DLOpen (-42)
#define LVO_DLSym (-48)
#define LVO_DLClose (-54)
#define LVO_ElfHook (-60)
#define LVO_ElfLastError (-66)
#define LVO_ElfErrorString (-72)
#define LVO_ElfLoadSegDOS (-78)
#define LVO_ElfAddLoader (-84)
#define LVO_ElfRemLoader (-90)
#define LVO_ElfLoadWhy (-96)
#define LVO_ElfSegCPU (-102)
#define LVO_ElfHostCPU (-108)

#define RTLD_LAZY 1             /* accepted; every symbol is bound at load */
#define RTLD_NOW 2
#define RTLD_GLOBAL 0x100       /* accepted; every shared object is global */

/* ElfIdentify's and ElfLastError's answers: elf_core.h's enum oe_error */
#define ELFERR_OK 0
#define ELFERR_NOT_ELF 1
#define ELFERR_CLASS 2
#define ELFERR_MACHINE 3
#define ELFERR_TYPE 4

/* ElfLoadSegDOS's kinds (dos_core.h's OD_LOADFMT_) */
#define ELFKIND_ELF 1
#define ELFKIND_HUNK_PPC 2

/* el_Machine: an ELF e_machine (EM_PPC 20, EM_386 3, EM_ARM 40,
 * EM_X86_64 62, EM_AARCH64 183, ...), or this for a hunk file with
 * HUNK_PPC_CODE hunks (WarpOS) */
#define ELFM_HUNK_PPC 0x10000UL
#define ELF_OSABI_ANY (-1L)
#define ELF_OSABI_AROS 15

/* What a loader is handed: the file, open. With elm_Funcs 0 (LoadSeg,
 * NewLoadSeg, the Shell) it is a DOS handle at the file's start, to Read and
 * Seek. With InternalLoadSeg's functions an ELF file has been read whole
 * into elm_Image (they only read on), and its segments should come from
 * elm_Funcs' alloc. A hunk file (ELFKIND_HUNK_PPC) is passed only with
 * elm_Funcs 0: dos has read into it. elm_Header holds the first bytes. */
struct ElfLoadMsg {
    BPTR elm_File;
    const LONG *elm_Funcs;      /* InternalLoadSeg's read, alloc, free; 0: dos's own */
    ULONG elm_Kind;             /* ELFKIND_ */
    const UBYTE *elm_Header;    /* the file's first bytes */
    ULONG elm_HeaderLen;
    ULONG elm_Machine;          /* e_machine, or ELFM_HUNK_PPC */
    LONG elm_OSABI;             /* EI_OSABI, -1 for a hunk file */
    ULONG elm_Type;             /* e_type (ET_REL 1, ET_EXEC 2, ET_DYN 3), 0 for a hunk file */
    const UBYTE *elm_Image;     /* with InternalLoadSeg's functions: the whole file, read in */
    ULONG elm_ImageSize;
    STRPTR elm_Why;             /* a refusal's words go here, for ElfLoadWhy */
    ULONG elm_WhySize;
};

/* A foreign loader (Stage 5: PowerPC, x86, ARM; AROS's personality).
 * el_Load is called with A0 = the message, A1 = the loader, A6 =
 * elf.library's base; it answers a seglist (the anchor, below) or 0 with
 * IoErr() set. The seglist is the loader's: its first segment is the
 * anchor, which dos.library's UnLoadSeg recognises and hands to el_Unload
 * (D1 = the seglist, A0 = el_UserData, A6 = DOSBase) instead of freeing it. */
struct ElfLoader {
    struct ElfLoader *el_Next;  /* elf.library's */
    ULONG el_Machine;
    LONG el_OSABI;              /* the EI_OSABI it takes, or ELF_OSABI_ANY */
    CONST_STRPTR el_Name;       /* "PowerPC (WarpOS)": for messages */
    APTR el_Load;
    APTR el_Unload;
    ULONG el_Count;             /* seglists out: the loader keeps it */
    APTR el_UserData;
};

/* The anchor: a foreign seglist's first segment, laid out as any segment
 * ([size][next] then the contents), whose contents start:
 *   +0  $4EF9 entry.l   jmp to the program's 68k entry (a gate, for a
 *                       native program), so RunCommand and CreateProc run it
 *   +6  $4AFC           (never reached)
 *   +8  ELF_ANCHOR_MAGIC
 *   +12 el_Unload       what UnLoadSeg calls
 *   +16 ~el_Unload      the check
 *   +20 el_UserData     handed to el_Unload in A0
 *   +24 e_machine       the CPU this seglist is (EM_68K 4 when there is
 *                       no anchor: a hunk file is always 68k). ElfSegCPU.
 *   +28 ...             the loader's own
 * (dos_seg.cpp, od_UnLoadSeg) */
#define ELF_ANCHOR_MAGIC 0x4C534658UL   /* "LSFX" */
#define ELF_ANCHOR_SIZE 24
#define ELF_ANCHOR_CPU 24               /* contents +24: e_machine */

/* ElfHostCPU(0) answers the host's CPU as an ELF e_machine: 4, 20, 62 or
 * 183. A real 680x0, and a host that has not said, answer 4. ElfHostCPU
 * with any other value records it. The names live in elf_cpu.h, which
 * this header does not include. hostlib ABI 2's probe uses HL_ARCH_*;
 * the board maps those across when that ABI is in force. */

#endif
