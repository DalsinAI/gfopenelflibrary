/* OpenELF: the few Exec and DOS facts elf.library uses, in our own words,
 * so it builds with any m68k ELF GCC and no NDK (CLEAN-ROOM.md: names,
 * values, offsets and registers are interface facts).
 * Copyright (c) 2026 AmigaChrome Team. MIT. */
#ifndef OPENELF_AMIGA_H
#define OPENELF_AMIGA_H

typedef unsigned char UBYTE;
typedef signed char BYTE;
typedef unsigned short UWORD;
typedef short WORD;
typedef unsigned long ULONG;
typedef long LONG;
typedef void *APTR;
typedef long BPTR;              /* a pointer shifted right by 2 */
typedef char *STRPTR;
typedef const char *CONST_STRPTR;

#define MKBADDR(p) ((BPTR)((ULONG)(p) >> 2))
#define BADDR(b) ((APTR)((ULONG)(b) << 2))

struct Node {                   /* 14 bytes */
    struct Node *ln_Succ, *ln_Pred;
    UBYTE ln_Type;
    BYTE ln_Pri;
    char *ln_Name;
};

struct MinList { APTR mlh_Head, mlh_Tail, mlh_TailPred; };

struct Library {                /* 34 bytes */
    struct Node lib_Node;
    UBYTE lib_Flags, lib_pad;
    UWORD lib_NegSize, lib_PosSize, lib_Version, lib_Revision;
    APTR lib_IdString;
    ULONG lib_Sum;
    UWORD lib_OpenCnt;
} __attribute__((packed, aligned(2)));

struct Resident {               /* 26 bytes */
    UWORD rt_MatchWord;
    struct Resident *rt_MatchTag;
    APTR rt_EndSkip;
    UBYTE rt_Flags, rt_Version, rt_Type;
    BYTE rt_Pri;
    char *rt_Name, *rt_IdString;
    APTR rt_Init;
} __attribute__((packed, aligned(2)));

/* A SignalSemaphore is 46 bytes; we only pass its address to Exec. */
struct SignalSemaphore { UWORD body[23]; };

#define RTC_MATCHWORD 0x4afc
#define RTF_AUTOINIT 0x80
#define NT_LIBRARY 9
#define LIBF_CHANGED 2
#define LIBF_SUMUSED 4
#define LIBF_DELEXP 8
#define MEMF_ANY 0
#define MEMF_PUBLIC 1
#define MEMF_CLEAR 0x10000UL

#define MODE_OLDFILE 1005
#define OFFSET_BEGINNING (-1)
#define OFFSET_END 1
#define ERROR_NO_FREE_STORE 103
#define ERROR_FILE_NOT_OBJECT 121
#define ERROR_BAD_HUNK 122
#define ERROR_OBJECT_NOT_FOUND 205
#define ERROR_OBJECT_WRONG_TYPE 212

/* A library call: jsr lvo(a6) with the arguments in their registers.
 * D0, D1, A0 and A1 are the callee's to change. */
static inline __attribute__((always_inline)) ULONG lib_call0(APTR base, int lvo)
{
    register ULONG d0 __asm("d0");
    register ULONG d1 __asm("d1");
    register ULONG a0 __asm("a0");
    register ULONG a1 __asm("a1");
    __asm volatile ("move.l %%a6,-(%%sp)\n\tmove.l %4,%%a6\n\tjsr %c5(%%a6)\n\tmove.l (%%sp)+,%%a6" : "=r"(d0), "=r"(d1), "=r"(a0), "=r"(a1) : "a"(base), "i"(lvo) : "cc", "memory");
    return d0;
}

/* d0, d1, a0, a1 in; d0 out; the rest kept (D2-D7/A2-A5 by the callee). */
static inline __attribute__((always_inline)) ULONG lib_call4(APTR base, int lvo, ULONG vd0, ULONG vd1, ULONG va0, ULONG va1)
{
    register ULONG d0 __asm("d0") = vd0;
    register ULONG d1 __asm("d1") = vd1;
    register ULONG a0 __asm("a0") = va0;
    register ULONG a1 __asm("a1") = va1;
    __asm volatile ("move.l %%a6,-(%%sp)\n\tmove.l %4,%%a6\n\tjsr %c5(%%a6)\n\tmove.l (%%sp)+,%%a6" : "+r"(d0), "+r"(d1), "+r"(a0), "+r"(a1) : "a"(base), "i"(lvo) : "cc", "memory");
    return d0;
}

/* DOS calls take D1, D2, D3 */
static inline __attribute__((always_inline)) ULONG dos_call3(APTR base, int lvo, ULONG vd1, ULONG vd2, ULONG vd3)
{
    register ULONG d0 __asm("d0");
    register ULONG d1 __asm("d1") = vd1;
    register ULONG d2 __asm("d2") = vd2;
    register ULONG d3 __asm("d3") = vd3;
    register ULONG a0 __asm("a0");
    register ULONG a1 __asm("a1");
    __asm volatile ("move.l %%a6,-(%%sp)\n\tmove.l %6,%%a6\n\tjsr %c7(%%a6)\n\tmove.l (%%sp)+,%%a6" : "=r"(d0), "+r"(d1), "=r"(a0), "=r"(a1), "+r"(d2), "+r"(d3)
                    : "a"(base), "i"(lvo) : "cc", "memory");
    return d0;
}

/* DOS calls with four: D1, D2, D3, D4 */
static inline __attribute__((always_inline)) ULONG dos_call4(APTR base, int lvo, ULONG vd1, ULONG vd2, ULONG vd3, ULONG vd4)
{
    register ULONG d0 __asm("d0");
    register ULONG d1 __asm("d1") = vd1;
    register ULONG d2 __asm("d2") = vd2;
    register ULONG d3 __asm("d3") = vd3;
    register ULONG d4 __asm("d4") = vd4;
    register ULONG a0 __asm("a0");
    register ULONG a1 __asm("a1");
    __asm volatile ("move.l %%a6,-(%%sp)\n\tmove.l %7,%%a6\n\tjsr %c8(%%a6)\n\tmove.l (%%sp)+,%%a6" : "=r"(d0), "+r"(d1), "=r"(a0), "=r"(a1), "+r"(d2), "+r"(d3), "+r"(d4)
                    : "a"(base), "i"(lvo) : "cc", "memory");
    return d0;
}

/* Exec */
#define SysCall(lvo, d0, d1, a0, a1) lib_call4(SysBase, lvo, (ULONG)(d0), (ULONG)(d1), (ULONG)(a0), (ULONG)(a1))
#define Forbid() lib_call0(SysBase, -132)
#define Permit() lib_call0(SysBase, -138)
#define AllocMem(size, flags) ((APTR)SysCall(-198, size, flags, 0, 0))
#define FreeMem(mem, size) SysCall(-210, size, 0, 0, mem)
#define Remove(node) SysCall(-252, 0, 0, 0, node)
#define CloseLibrary(lib) SysCall(-414, 0, 0, 0, lib)
#define SetFunction(lib, off, fn) ((APTR)SysCall(-420, fn, 0, off, lib))
#define OpenLibrary(name, ver) ((APTR)SysCall(-552, ver, 0, 0, name))
#define InitSemaphore(s) SysCall(-558, 0, 0, s, 0)
#define ObtainSemaphore(s) SysCall(-564, 0, 0, s, 0)
#define ReleaseSemaphore(s) SysCall(-570, 0, 0, s, 0)
#define CacheClearU() lib_call0(SysBase, -636)

/* DOS */
#define DosCall(lvo, d1, d2, d3) dos_call3(DOSBase, lvo, (ULONG)(d1), (ULONG)(d2), (ULONG)(d3))
#define Open(name, mode) ((BPTR)DosCall(-30, name, mode, 0))
#define Close(fh) DosCall(-36, fh, 0, 0)
#define Read(fh, buf, len) ((LONG)DosCall(-42, fh, buf, len))
#define Seek(fh, pos, mode) ((LONG)DosCall(-66, fh, pos, mode))
#define IoErr() ((LONG)DosCall(-132, 0, 0, 0))
#define SetIoErr(code) ((LONG)DosCall(-462, code, 0, 0))
#define UnLoadSeg(seg) DosCall(-156, seg, 0, 0)
#define RunCommand(seg, stack, args, len) ((LONG)dos_call4(DOSBase, -504, (ULONG)(seg), (ULONG)(stack), (ULONG)(args), (ULONG)(len)))
#define PutStr(s) DosCall(-948, s, 0, 0)
#define VPrintf(fmt, argv) DosCall(-954, fmt, argv, 0)
#define LVO_LoadSeg (-150)
#define LVO_NewLoadSeg (-768)

#endif
