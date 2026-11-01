/* A fixture shared object: data, a function, and a call out to the loader.
 * Copyright (c) 2026 AmiCradle Team. MIT. */
extern int host_add(int a, int b);
int lib_bias = 7;
int lib_mul(int a, int b) { return a * b + lib_bias; }
int lib_calls_host(int x) { return host_add(x, 1); }
