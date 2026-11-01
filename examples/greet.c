/* A shared object for DLOpen: ElfTest DL libgreet.so greet_answer
 * Copyright (c) 2026 AmiCradle Team. MIT. */
static int calls;
int greet_constructed;
__attribute__((constructor)) static void made(void) { greet_constructed = 1; }
int greet_answer(void) { calls++; return greet_constructed ? 41 + calls : -1; }
