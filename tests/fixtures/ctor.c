/* A fixture program with constructors and destructors in three priorities
 * and data aligned to 32: env->program gives it a start routine.
 * Copyright (c) 2026 AmiCradle Team. MIT. */
#ifdef LOCAL_NOTE
/* an executable can't import: its destructors note to themselves */
int dtor_seq;
static void host_note(int v) { dtor_seq = dtor_seq * 10 + v; }
#else
extern void host_note(int v);       /* the loader's resolve answers it */
#endif
int ctor_seq;
int ctor_aligned __attribute__((aligned(32))) = 5;
__attribute__((constructor(101))) static void first(void) { ctor_seq = ctor_seq * 10 + 1; }
__attribute__((constructor(200))) static void second(void) { ctor_seq = ctor_seq * 10 + 2; }
__attribute__((constructor)) static void third(void) { ctor_seq = ctor_seq * 10 + 3; }
/* destructors run the other way: the default priority first, 101 last */
__attribute__((destructor(101))) static void last_out(void) { host_note(1); }
__attribute__((destructor)) static void first_out(void) { host_note(3); }
int _start(void)
{
    int ok = 0;
    if (ctor_seq == 123) ok |= 1;
    if (((unsigned long)&ctor_aligned & 31) == 0 && ctor_aligned == 5) ok |= 2;
    return ok == 3 ? 0x3ff : ok;
}
