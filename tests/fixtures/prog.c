/* A fixture program: ten checks of what a loader must get right, one bit
 * each. Built as an object (gcc and clang), an object linked with ld -r,
 * an executable with its relocations (-q) and a PIE (tests/test_openelf.sh).
 * Copyright (c) 2026 AmiCradle Team. MIT. */
#include "fixture.h"

#ifdef WANT_EXTERN
extern int host_add(int a, int b);          /* the loader's resolve answers it */
#endif

static int counter;                         /* .bss */
#ifdef WANT_COMMON
int common_var;                             /* a COMMON symbol, with -fcommon */
#else
static int common_var;
#endif
/* global, so the compiler can't fold them away: the loader must place them */
const char *names[] = { "one", "two", "three" };    /* addresses in data */
static int sq(int x) { return x * x; }
static int cube(int x) { return x * x * x; }
int (*ops[])(int) = { sq, cube };                   /* function pointers in data */
unsigned char big[3000] = { 1, 2, 3 };              /* data of its own */
char text[] = "relocated";

__attribute__((noinline)) static int far_away(int x) { return x + 1000; }
__attribute__((noinline)) int len(const char *s) { int n = 0; while (s[n]) n++; return n; }

int _start(const struct fx_api *api)
{
    int ok = 0;
    if (names[1][0] == 't' && names[2][4] == 'e') ok |= 1;
    if (ops[0](3) == 9 && ops[1](3) == 27) ok |= 2;
    if (counter == 0) { counter++; counter++; if (counter == 2) ok |= 4; }
    if (common_var == 0) { common_var = 5; if (common_var == 5) ok |= 8; }
    if (api->hello("ELF") == 43) ok |= 16;
    if (len(text) == 9) ok |= 32;
    if (big[0] == 1 && big[2] == 3 && big[2999] == 0) ok |= 64;
    if (far_away(1) == 1001) ok |= 128;
#ifdef WANT_EXTERN
    if (host_add(2, 3) == 5) ok |= 256;
#else
    ok |= 256;
#endif
    if (api->version == 1) ok |= 512;
    return ok;
}
