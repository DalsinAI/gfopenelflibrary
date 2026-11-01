/* A fixture shared object with an entry that needs libfix.so (DT_NEEDED):
 * its functions through the PLT, its data through the GOT.
 * Copyright (c) 2026 AmiCradle Team. MIT. */
#include "fixture.h"
extern int lib_mul(int a, int b);
extern int lib_calls_host(int x);
extern int lib_bias;
int main_value = 100;
int _start(const struct fx_api *api)
{
    int ok = 0;
    if (lib_mul(6, 7) == 49) ok |= 1;
    if (lib_bias == 7) { lib_bias = 0; if (lib_mul(6, 7) == 42) ok |= 2; }
    if (lib_calls_host(41) == 42) ok |= 4;
    if (api->hello("so") == 42) ok |= 8;
    return ok == 15 ? FX_ALL : ok;
}
