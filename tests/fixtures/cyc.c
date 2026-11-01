/* Fixture shared objects that need each other (built as libcyca.so and
 * libcycb.so): DLOpen must refuse the cycle, not loop.
 * Copyright (c) 2026 AmiCradle Team. MIT. */
int CYC_NAME(void) { return 1; }
