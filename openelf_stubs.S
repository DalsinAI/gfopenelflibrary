| GFOpenELF: elf.library's first code, RomTag and register stubs.
| Copyright (c) 2026 AmiCradle Team. MIT.
|
| Each library call arrives with its arguments in registers (elf.lvo) and
| A6 = the library base; its stub pushes them and calls the C function in
| openelf_lib.c, which keeps D2-D7/A2-A6 and answers in D0, the same rule
| the Amiga's calls follow. Linked first, so "start" is the hunk's first
| code: run as a program, the library returns -1 and does nothing.

        .text
        .globl  start
start:
        moveq   #-1,%d0
        rts

        .globl  elf_romtag
elf_romtag:
        .word   0x4afc                  | RTC_MATCHWORD
        .long   elf_romtag
        .long   elf_endskip
        .byte   0x80                    | RTF_AUTOINIT
        .byte   2                       | version (the library's: exec may set lib_Version from it)
        .byte   9                       | NT_LIBRARY
        .byte   0                       | priority
        .long   lib_name
        .long   lib_id
        .long   lib_inittable
        .even

| init: D0 = base, A0 = seglist, A6 = SysBase
        .globl  stub_init
stub_init:
        move.l  %a6,-(%sp)
        move.l  %a0,-(%sp)
        move.l  %d0,-(%sp)
        jsr     lib_init_c
        lea     12(%sp),%sp
        rts

        .macro  BASE_ONLY name, cfn
        .globl  \name
\name:
        move.l  %a6,-(%sp)
        jsr     \cfn
        addq.l  #4,%sp
        rts
        .endm

        BASE_ONLY stub_open, lib_open_c
        BASE_ONLY stub_close, lib_close_c
        BASE_ONLY stub_expunge, lib_expunge_c

        .globl  stub_null
stub_null:
        moveq   #0,%d0
        rts

        .macro  ONE name, r1
        .globl  stub_\name
stub_\name:
        move.l  %\r1,-(%sp)
        jsr     L_\name
        addq.l  #4,%sp
        rts
        .endm

        .macro  TWO name, r1, r2
        .globl  stub_\name
stub_\name:
        move.l  %\r2,-(%sp)
        move.l  %\r1,-(%sp)
        jsr     L_\name
        addq.l  #8,%sp
        rts
        .endm

        ONE     ElfLoadSeg, d1
        TWO     ElfIdentify, a0, d0
        TWO     DLOpen, a0, d0
        TWO     DLSym, a0, a1
        ONE     DLClose, a0
        ONE     ElfHook, d0
        TWO     ElfLastError, a0, d0
        ONE     ElfErrorString, d0
        ONE     ElfAddLoader, a0
        ONE     ElfRemLoader, a0

        .globl  stub_ElfLoadSegDOS
stub_ElfLoadSegDOS:
        move.l  %a0,-(%sp)
        move.l  %d2,-(%sp)
        move.l  %d1,-(%sp)
        jsr     L_ElfLoadSegDOS
        lea     12(%sp),%sp
        rts

        .globl  stub_ElfLoadWhy
stub_ElfLoadWhy:
        jmp     L_ElfLoadWhy

        ONE     ElfSegCPU, d1
        ONE     ElfHostCPU, d0

| LONG elf_call_read(fn, fh, buf, len, DOSBase): InternalLoadSeg's read
        .globl  elf_call_read
elf_call_read:
        movem.l %d2-%d3/%a6,-(%sp)
        move.l  16(%sp),%a1
        move.l  20(%sp),%d1
        move.l  24(%sp),%d2
        move.l  28(%sp),%d3
        move.l  32(%sp),%a6
        jsr     (%a1)
        movem.l (%sp)+,%d2-%d3/%a6
        rts

| ULONG elf_call_alloc(fn, size, flags, SysBase): its alloc, in D0
        .globl  elf_call_alloc
elf_call_alloc:
        move.l  %a6,-(%sp)
        move.l  8(%sp),%a1
        move.l  12(%sp),%d0
        move.l  16(%sp),%d1
        move.l  20(%sp),%a6
        jsr     (%a1)
        move.l  (%sp)+,%a6
        rts

| void elf_call_free(fn, mem, size, SysBase): its free
        .globl  elf_call_free
elf_call_free:
        move.l  %a6,-(%sp)
        move.l  8(%sp),%a0
        move.l  12(%sp),%a1
        move.l  16(%sp),%d0
        move.l  20(%sp),%a6
        jsr     (%a0)
        move.l  (%sp)+,%a6
        rts

| ULONG elf_call_a0a1(APTR fn, APTR a0, APTR a1, APTR a6): a foreign
| loader's el_Load
        .globl  elf_call_a0a1
elf_call_a0a1:
        movem.l %d2-%d7/%a2-%a6,-(%sp)
        move.l  48(%sp),%a2
        move.l  52(%sp),%a0
        move.l  56(%sp),%a1
        move.l  60(%sp),%a6
        jsr     (%a2)
        movem.l (%sp)+,%d2-%d7/%a2-%a6
        rts

| dos.library's LoadSeg as patched: D1 = name, A6 = DOSBase
        .globl  elf_loadseg_patch
elf_loadseg_patch:
        move.l  %a6,-(%sp)
        move.l  %d1,-(%sp)
        jsr     elf_loadseg_c
        addq.l  #8,%sp
        rts

| NewLoadSeg: D1 = name, D2 = tags, A6 = DOSBase
        .globl  elf_newloadseg_patch
elf_newloadseg_patch:
        move.l  %a6,-(%sp)
        move.l  %d2,-(%sp)
        move.l  %d1,-(%sp)
        jsr     elf_newloadseg_c
        lea     12(%sp),%sp
        rts

| ULONG elf_call_d1(APTR fn, ULONG d1, APTR a6): the original LoadSeg
        .globl  elf_call_d1
elf_call_d1:
        move.l  %a6,-(%sp)
        move.l  8(%sp),%a1
        move.l  12(%sp),%d1
        move.l  16(%sp),%a6
        jsr     (%a1)
        move.l  (%sp)+,%a6
        rts

| ULONG elf_call_d1d2(APTR fn, ULONG d1, ULONG d2, APTR a6): NewLoadSeg
        .globl  elf_call_d1d2
elf_call_d1d2:
        movem.l %d2/%a6,-(%sp)
        move.l  12(%sp),%a1
        move.l  16(%sp),%d1
        move.l  20(%sp),%d2
        move.l  24(%sp),%a6
        jsr     (%a1)
        movem.l (%sp)+,%d2/%a6
        rts

        .section .note.GNU-stack,"",@progbits
