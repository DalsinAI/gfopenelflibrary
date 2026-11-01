#!/bin/sh
# Copyright (c) 2026 AmigaChrome Team. MIT licence (see LICENSE).
# OpenELF in an emulated AmigaOS: build-amiga.sh's
# elf.library (a hunk library) is opened from LIBS: by C/ElfTest under
# amitools' vamos (68040), which loads and runs each ELF example (an
# executable with -q, an object, a PIE, Rust when built), DLOpens
# libgreet.so and calls into it, and is refused a hunk file. vamos runs
# only seglists it loaded itself, so ElfTest's CALL form jumps to the
# seglist as DOS would and frees it as UnLoadSeg would.
#   pip install amitools==0.8.1 machine68k==0.3.0
#   sh tests/test_openelf_vamos.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
oe=$here/..
command -v vamos >/dev/null || { echo "skipped: no vamos (pip install amitools==0.8.1 machine68k==0.3.0)"; exit 0; }
command -v m68k-linux-gnu-gcc >/dev/null || { echo "skipped: no m68k-linux-gnu-gcc"; exit 0; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
OUT=$tmp/drawer sh "$oe/build-amiga.sh" > "$tmp/build.log" 2>&1 || { cat "$tmp/build.log"; exit 1; }
# shared objects DLOpen finds in LIBS:elf/: libouter.so needs libgreet.so,
# and libcyca.so and libcycb.so need each other
fx=$oe/tests/fixtures
so() { m68k-linux-gnu-gcc -m68040 -msoft-float -Os -fPIC -shared -nostdlib -Wl,--hash-style=both -Wl,--build-id=none \
    -Wl,--no-warn-rwx-segments -Wl,-z,max-page-size=16 -Wl,-z,common-page-size=16 -Wl,-z,norelro -Wl,--allow-shlib-undefined -Wl,--no-as-needed "$@"; }
mkdir -p "$tmp/drawer/Libs/elf"
cp "$tmp/drawer/ELF/libgreet.so" "$tmp/drawer/Libs/elf/"
so "$fx/outer.c" -L"$tmp/drawer/Libs/elf" -lgreet -o "$tmp/drawer/Libs/elf/libouter.so"
so -DCYC_NAME=cyc_b "$fx/cyc.c" -o "$tmp/drawer/Libs/elf/libcycb.so"
so -DCYC_NAME=cyc_a "$fx/cyc.c" -L"$tmp/drawer/Libs/elf" -lcycb -o "$tmp/drawer/Libs/elf/libcyca.so"
so -DCYC_NAME=cyc_b "$fx/cyc.c" -L"$tmp/drawer/Libs/elf" -lcyca -o "$tmp/drawer/Libs/elf/libcycb.so"
# an object whose .text says it holds 8 MB of the file: refused for the
# file's size (Seek), before any memory is allocated for it
python3 - "$tmp/drawer/ELF/hello-object" "$tmp/drawer/ELF/past-file" <<'PY'
import struct, sys
b = bytearray(open(sys.argv[1], 'rb').read())
shoff, = struct.unpack_from('>I', b, 32)
shnum, shstrndx = struct.unpack_from('>HH', b, 48)
stroff, = struct.unpack_from('>I', b, shoff + shstrndx * 40 + 16)
for i in range(shnum):
    sh = shoff + i * 40
    name, = struct.unpack_from('>I', b, sh)
    if b[stroff + name:stroff + name + 6] == b'.text\0':
        struct.pack_into('>I', b, sh + 20, 0x800000)
open(sys.argv[2], 'wb').write(b)
PY
cd "$tmp/drawer"
fail=0
# vamos keeps a RAM: volume under $HOME between runs, so each run gets its
# own HOME; keep pip's per-user packages findable when HOME moves
PYTHONUSERBASE=$(python3 -m site --user-base)
export PYTHONUSERBASE
run() { # want, ElfTest args...
    want=$1; shift
    HOME=$tmp/home timeout 60 vamos -C 68040 -V "libs:$tmp/drawer/Libs" C/ElfTest "$@" > "$tmp/out" 2>&1 || true
    if grep -q -- "$want" "$tmp/out"; then echo "ok   ElfTest $*"; else echo "FAIL ElfTest $* (want: $want)"; cat "$tmp/out"; fail=1; fi
}
for f in hello-exec hello-object hello-pie; do
    run "arguments: one two" CALL "ELF/$f" one two
    run "returned 0" CALL "ELF/$f" one two
done
if [ -f ELF/rust-hello ]; then
    run "squares: 1 4 9 16 25 36 49 64 81 100" CALL ELF/rust-hello
    run "returned 0" CALL ELF/rust-hello
fi
run "it returns 42" DL ELF/libgreet.so greet_answer
run "DLClose = 0" DL ELF/libgreet.so greet_answer
run "it returns 42" DL libouter.so greet_answer         # found through what it needs
run "DLClose = 0" DL libouter.so greet_answer
run "could not be loaded" DL libcyca.so cyc_a           # a cycle is refused
run "cut short" CALL ELF/past-file
run "not an ELF file" CALL C/ElfHook
[ "$fail" = 0 ] && echo "all passed"
exit "$fail"
