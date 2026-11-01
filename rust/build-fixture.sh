#!/bin/sh
# The Rust fixture as a 68k ELF object: core built from rust-src for
# m68k-unknown-none-elf (68040, LLVM's m68k backend, soft float), the
# staticlib's objects joined with ld -r and trimmed to what _start uses.
#   sh build-fixture.sh OUTDIR      -> OUTDIR/prog-rust.o, OUTDIR/prog-rust-exec-q
# Copyright (c) 2026 AmiCradle Team. MIT.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
out=$1
ld=${M68K_LD:-m68k-linux-gnu-ld}
target=${CARGO_TARGET_DIR:-$out/rust-target}
( cd "$here/fixture" && RUSTC_BOOTSTRAP=1 RUSTFLAGS="-C target-cpu=M68040" \
    cargo build -q --release --target m68k-unknown-none-elf \
    -Zbuild-std=core,alloc,compiler_builtins -Zbuild-std-features=compiler-builtins-mem \
    --target-dir "$target" )
"$ld" -r --gc-sections -u _start -e _start --whole-archive \
    "$target/m68k-unknown-none-elf/release/libopenelf_fixture.a" -o "$out/prog-rust-full.o"
# without the LLVM bitcode rustc embeds
"${M68K_OBJCOPY:-m68k-linux-gnu-objcopy}" --remove-section=.llvmbc --remove-section=.llvmcmd \
    "$out/prog-rust-full.o" "$out/prog-rust.o"
# and as an executable with its relocations: host_add from a C object here
"${M68K_CC:-m68k-linux-gnu-gcc}" -m68040 -O2 -fno-pic -c "$here/fixture/host.c" -o "$out/rust-host.o"
"$ld" -q -N -e _start --no-warn-rwx-segments "$out/prog-rust.o" "$out/rust-host.o" -o "$out/prog-rust-exec-q"
echo "$out/prog-rust.o ($(wc -c < "$out/prog-rust.o") bytes)"
