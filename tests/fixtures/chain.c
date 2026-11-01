/* Fixture shared objects in a chain (libchain0.so needs libchain1.so, which
 * needs libchain2.so, ...): DLOpen must stop a chain deeper than it allows
 * instead of running the caller's stack out.
 * Copyright (c) 2026 AmiCradle Team. MIT. */
int CHAIN_NAME(void) { return 42; }
