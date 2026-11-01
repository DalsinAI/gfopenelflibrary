#!/bin/sh
# Copyright (c) 2026 AmigaChrome Team. MIT licence (see LICENSE).
# GFOpenELF: the loader core under qemu-m68k. Builds the
# fixtures with the m68k cross GCC (objects, ld -r, -q executables, a PIE,
# shared objects with DT_NEEDED, and the files it must refuse), with clang's
# m68k backend when there is one (LLVM, as Rust uses), and a Rust object
# when a nightly-capable rustc has rust-src; then test_core loads each one,
# runs it, and damages it.
#   sh tests/test_openelf.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
oe=$here/..
fx=$oe/tests/fixtures
tmp=${OPENELF_TMP:-$(mktemp -d)}; [ -n "${OPENELF_TMP:-}" ] || trap 'rm -rf "$tmp"' EXIT; mkdir -p "$tmp"
cc=${M68K_CC:-m68k-linux-gnu-gcc}
ld=${M68K_LD:-m68k-linux-gnu-ld}
qemu=${QEMU_M68K:-qemu-m68k}
command -v "$cc" >/dev/null || { echo "skipped: no $cc"; exit 0; }
F="-m68040 -O2 -ffreestanding -fno-builtin -fno-asynchronous-unwind-tables -fno-unwind-tables
   -fno-stack-protector -Wall -Wextra -Werror -I$fx"
P="-nostdlib -Wl,--no-warn-rwx-segments -Wl,--hash-style=both -Wl,--build-id=none"

# shellcheck disable=SC2086
{
# objects, as AROS and MorphOS load them
$cc $F -fno-pic -fcommon -DWANT_COMMON -DWANT_EXTERN -c "$fx/prog.c" -o "$tmp/prog-gcc.o"
$cc $F -fno-pic -fcommon -DWANT_COMMON -DWANT_EXTERN -ffunction-sections -fdata-sections -c "$fx/prog.c" -o "$tmp/prog-gcc-sections.o"
$ld -r "$tmp/prog-gcc-sections.o" -o "$tmp/prog-gcc-r.o"
$cc $F -fno-pic -DWANT_EXTERN -Dhost_add=nobody_defines_this -c "$fx/prog.c" -o "$tmp/prog-undef.o"
$cc $F -fpic -DWANT_EXTERN -c "$fx/prog.c" -o "$tmp/prog-pic.o"
$cc $F -fno-pic -c "$fx/tls.c" -o "$tmp/tls.o"
# executables: relocations kept, none kept, and position-independent
$cc $F -fno-pic -no-pie -static $P -Wl,-e,_start -Wl,-q -Wl,-n "$fx/prog.c" -o "$tmp/prog-exec-q"
$cc $F -fno-pic -no-pie -static $P -Wl,-e,_start -Wl,-n "$fx/prog.c" -o "$tmp/prog-exec-fixed"
$cc $F -fPIE -pie $P -Wl,-e,_start -Wl,--no-dynamic-linker -Wl,-z,norelro "$fx/prog.c" -o "$tmp/prog-pie"
# executables it must refuse: a strong symbol nobody defined, an entry in data
$cc $F -fno-pic -no-pie -static $P -Wl,-e,_start -Wl,-q -Wl,-n -Wl,--unresolved-symbols=ignore-all -DWANT_EXTERN \
    "$fx/prog.c" -o "$tmp/prog-exec-undef"
$cc $F -fno-pic -no-pie -static $P -Wl,-e,0x700000 -Wl,-q -Wl,-n "$fx/prog.c" -o "$tmp/prog-exec-badentry"
# constructors and destructors: an object, ld -r, -q and a PIE
$cc $F -fno-pic -c "$fx/ctor.c" -o "$tmp/ctor.o"
$ld -r "$tmp/ctor.o" -o "$tmp/ctor-r.o"
$cc $F -fno-pic -no-pie -static $P -Wl,-e,_start -Wl,-q -Wl,-n -DLOCAL_NOTE "$fx/ctor.c" -o "$tmp/ctor-exec-q"
$cc $F -fPIE -pie $P -Wl,-e,_start -Wl,--no-dynamic-linker -Wl,-z,norelro -Wl,--unresolved-symbols=ignore-all "$fx/ctor.c" -o "$tmp/ctor-pie"
# shared objects: main.so needs libfix.so; needs-missing.so needs one that isn't there
$cc $F -fPIC -shared $P "$fx/lib.c" -o "$tmp/libfix.so"
$cc $F -fPIC -shared $P -Wl,-e,_start "$fx/main_so.c" -L"$tmp" -lfix -o "$tmp/main.so"
cp "$tmp/libfix.so" "$tmp/libnothere.so"
$cc $F -fPIC -shared $P "$fx/main_so.c" -L"$tmp" -lnothere -o "$tmp/needs-missing.so"
rm "$tmp/libnothere.so"
}
# a Linux program, and one for another CPU
$cc -O2 "$fx/linux.c" -o "$tmp/linux-dynamic"
cp /bin/true "$tmp/host-x86"
echo "not an ELF file" > "$tmp/not-elf.txt"

# LLVM's m68k backend: an object, and an executable linked by GNU ld.
# Before LLVM 19 it stored through (d16,pc), which no 68k has (the clang 18
# that Ubuntu 24.04 ships does), so older clangs are skipped.
clang_major=$(clang -dumpversion 2>/dev/null | cut -d. -f1 || echo 0)
if [ "${clang_major:-0}" -ge 19 ] && clang --target=m68k-unknown-none-elf -mcpu=68040 -c -x c /dev/null -o "$tmp/probe.o" 2>/dev/null; then
    clang --target=m68k-unknown-none-elf -mcpu=68040 -O2 -ffreestanding -fno-builtin -Wall -Werror -I"$fx" \
        -c "$fx/prog.c" -o "$tmp/prog-clang.o"
    $ld -q -n -e _start --no-warn-rwx-segments "$tmp/prog-clang.o" -o "$tmp/prog-clang-exec-q"
else
    echo "no clang 19 or later with m68k: the clang fixtures are skipped"
fi

# Rust: #![no_std] for m68k-unknown-none-elf, core built from rust-src
if [ "${OPENELF_RUST:-1}" = 1 ] && command -v rustc >/dev/null \
   && [ -d "$(rustc --print sysroot)/lib/rustlib/src/rust/library/core" ]; then
    sh "$oe/rust/build-fixture.sh" "$tmp" || echo "the Rust fixture did not build: skipped"
else
    echo "no rustc with rust-src: the Rust fixtures are skipped"
fi

for build in "-O0" "-Os"; do
    # shellcheck disable=SC2086
    $cc -std=gnu99 -m68040 $build -Wall -Wextra -Werror -static -I"$oe" \
        "$oe/tests/test_core.c" "$oe/elf_core.c" -o "$tmp/test_core"
    echo "== core at $build"
    "$qemu" "$tmp/test_core" "$tmp"
done
