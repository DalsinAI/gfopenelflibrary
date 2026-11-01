| GFOpenELF's program start, for ELF programs (and the hunk tools built the
| same way): AmigaDOS starts a program with D0 = the argument line's length,
| A0 = the line, and SysBase at address 4; this saves SysBase and calls
|     int amiga_main(const char *args, long len)
| whose answer is the return code. Link it first.
| Copyright (c) 2026 AmigaChrome Team. MIT.
        .text
        .globl  _start
_start:
#ifdef __PIC__
        lea     SysBase(%pc),%a1        | a PIE: no absolute stores
        move.l  4.w,(%a1)
#else
        move.l  4.w,SysBase
#endif
        move.l  %d0,-(%sp)
        move.l  %a0,-(%sp)
        bsr.l   amiga_main
        addq.l  #8,%sp
        rts

        .section .note.GNU-stack,"",@progbits
        .bss
        .globl  SysBase
        .align  4
SysBase:
        .skip   4
