/* A fixture shared object that needs libgreet.so and adds nothing of its
 * own: DLSym on its handle must find greet_answer through it.
 * Copyright (c) 2026 AmiCradle Team. MIT. */
extern int greet_answer(void);
int outer_calls_greet(void) { return greet_answer(); }
