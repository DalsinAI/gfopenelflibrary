/* What the test loader hands a loaded fixture's entry, and what every
 * fixture's checks add up to.
 * Copyright (c) 2026 AmiCradle Team. MIT. */
#ifndef OPENELF_FIXTURE_H
#define OPENELF_FIXTURE_H
struct fx_api {
    int (*hello)(const char *s);    /* returns the string's length plus 40 */
    int version;
};
#define FX_ALL 0x3ff                /* prog.c's ten checks */
#endif
