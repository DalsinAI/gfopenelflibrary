/* GFOpenELF's few C-callable Amiga calls, for ELF programs written in
 * languages with no register calls of their own (Rust, as rust/hello uses):
 * dos.library's PutStr and numbers through VPrintf, and memory from one exec pool per program, which
 * suits Rust's allocator (it frees with the size, as FreePooled wants).
 * Copyright (c) 2026 AmigaChrome Team. MIT. */
#include "../openelf_amiga.h"

extern APTR SysBase;
APTR DOSBase;
static APTR pool;

static inline __attribute__((always_inline)) ULONG exec_d0d1d2(int lvo, ULONG vd0, ULONG vd1, ULONG vd2)
{
    register ULONG d0 __asm("d0") = vd0;
    register ULONG d1 __asm("d1") = vd1;
    register ULONG d2 __asm("d2") = vd2;
    register ULONG a0 __asm("a0");
    register ULONG a1 __asm("a1");
    APTR base = SysBase;
    __asm volatile ("move.l %%a6,-(%%sp)\n\tmove.l %5,%%a6\n\tjsr %c6(%%a6)\n\tmove.l (%%sp)+,%%a6" : "+r"(d0), "+r"(d1), "=r"(a0), "=r"(a1), "+r"(d2) : "a"(base), "i"(lvo) : "cc", "memory");
    return d0;
}

/* dos.library and the pool: 0 when either can't be had */
int amiga_open(void)
{
    if (!(DOSBase = OpenLibrary("dos.library", 36))) return 0;
    /* 8 KB puddles, blocks over 2 KB on their own: small allocations stay together */
    if (!(pool = (APTR)exec_d0d1d2(-696, MEMF_ANY, 8192, 2048))) { CloseLibrary(DOSBase); return 0; }
    return 1;
}

void amiga_close(void)
{
    if (pool) SysCall(-702, 0, 0, pool, 0);     /* DeletePool frees everything left */
    if (DOSBase) CloseLibrary(DOSBase);
    pool = 0;
    DOSBase = 0;
}

long amiga_putstr(const char *s) { return (long)PutStr(s); }

/* a number in decimal, through VPrintf */
long amiga_putnum(unsigned long n)
{
    ULONG v[1];
    v[0] = n;
    return (long)VPrintf("%lu", v);
}

void *amiga_alloc(unsigned long size) { return (void *)SysCall(-708, size, 0, pool, 0); }
void amiga_free(void *mem, unsigned long size) { SysCall(-714, size, 0, pool, mem); }
