#!/bin/sh
# GFOpenELF for the Amiga, with the m68k ELF GCC alone (gcc-m68k-linux-gnu;
# no NDK, no Amiga toolchain): every piece is linked at 0 with its
# relocations kept and the hunk ones made by tools/mkhunk.py.
#   Libs/elf.library     the loader (a hunk file: the hunk loader loads it)
#   C/ElfHook            turns ELF loading in LoadSeg on (OFF: off)
#   C/ElfTest            ElfLoadSeg + RunCommand, or DLOpen + DLSym
#   ELF/hello-exec       examples/hello.c, an executable with -q (ELF)
#   ELF/hello-object     the same as an object (ld -r), as AROS and MorphOS load
#   ELF/hello-pie        the same, position-independent (-pie)
#   ELF/libgreet.so      a shared object for ElfTest DL
#   ELF/rust-hello       rust/hello, Rust (core + alloc) on an exec pool, as an
#                        object, when rustc has rust-src (RUSTC_BOOTSTRAP for -Zbuild-std)
# OUT= the drawer (default build-amiga); 68040, soft float, -Os.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUT=${OUT:-"$HERE/build-amiga"}
CC=${CC:-m68k-linux-gnu-gcc}
LD=${LD:-m68k-linux-gnu-ld}
NM=${NM:-m68k-linux-gnu-nm}
OBJ="$OUT/obj"
mkdir -p "$OUT/Libs" "$OUT/C" "$OUT/ELF" "$OBJ"
CFLAGS="-std=gnu11 -m68040 -msoft-float -Os -ffreestanding -fno-builtin -fno-pic -fomit-frame-pointer
  -fno-common -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-stack-protector
  -fno-tree-loop-distribute-patterns -fno-delete-null-pointer-checks -fno-strict-aliasing
  -Wall -Wextra -Werror -I$HERE"
LDQ="-q -N -Ttext=0 --no-warn-rwx-segments --build-id=none"
cc() { # shellcheck disable=SC2086
  "$CC" $CFLAGS "$@"; }
as() { "$CC" -m68040 -c "$@"; }
noundef() {
  und=$("$NM" -u "$1" || true)
  [ -z "$und" ] || { echo "undefined in $1: $und" >&2; exit 1; }
}

# elf.library: the stubs first (its "start" and RomTag), the core last
as "$HERE/openelf_stubs.S" -o "$OBJ/stubs.o"
cc -c "$HERE/openelf_lib.c" -o "$OBJ/lib.o"
cc -c "$HERE/elf_core.c" -o "$OBJ/core.o"
# shellcheck disable=SC2086
"$LD" $LDQ -e start "$OBJ/stubs.o" "$OBJ/lib.o" "$OBJ/core.o" -o "$OBJ/elf.library.elf"
noundef "$OBJ/elf.library.elf"
python3 "$HERE/tools/mkhunk.py" "$OBJ/elf.library.elf" "$OUT/Libs/elf.library"

# the tools, hunk programs built the same way
as "$HERE/crt/amiga_start.S" -o "$OBJ/start.o"
for t in ElfHook ElfTest; do
  cc -c "$HERE/c/$t.c" -o "$OBJ/$t.o"
  # shellcheck disable=SC2086
  "$LD" $LDQ -e _start "$OBJ/start.o" "$OBJ/$t.o" -o "$OBJ/$t.elf"
  noundef "$OBJ/$t.elf"
  python3 "$HERE/tools/mkhunk.py" "$OBJ/$t.elf" "$OUT/C/$t"
done

# the ELF examples: these stay ELF
cc -c "$HERE/examples/hello.c" -o "$OBJ/hello.o"
cc -c "$HERE/crt/amiga_shim.c" -o "$OBJ/shim.o"
"$LD" -q -N -e _start --no-warn-rwx-segments --build-id=none "$OBJ/start.o" "$OBJ/hello.o" -o "$OUT/ELF/hello-exec"
"$LD" -r -e _start "$OBJ/start.o" "$OBJ/hello.o" -o "$OUT/ELF/hello-object"
"$CC" -m68040 -c -fPIE "$HERE/crt/amiga_start.S" -o "$OBJ/start-pie.o"
# shellcheck disable=SC2086
"$CC" $CFLAGS -fPIE -c "$HERE/examples/hello.c" -o "$OBJ/hello-pie.o"
"$LD" -pie -e _start --no-dynamic-linker -z norelro --hash-style=both --no-warn-rwx-segments --build-id=none \
  "$OBJ/start-pie.o" "$OBJ/hello-pie.o" -o "$OUT/ELF/hello-pie"
# shellcheck disable=SC2086
"$CC" $CFLAGS -fPIC -shared -nostdlib -Wl,--hash-style=both -Wl,--build-id=none -Wl,--no-warn-rwx-segments \
  -Wl,-z,max-page-size=16 -Wl,-z,common-page-size=16 -Wl,-z,norelro "$HERE/examples/greet.c" -o "$OUT/ELF/libgreet.so"

rm -f "$OUT/ELF/rust-hello"    # never a stale one from an earlier build
if command -v cargo >/dev/null && [ -d "$(rustc --print sysroot 2>/dev/null)/lib/rustlib/src/rust/library/alloc" ]; then
  ( cd "$HERE/rust/hello" && RUSTC_BOOTSTRAP=1 RUSTFLAGS="-C target-cpu=M68040" \
      cargo build -q --release --target m68k-unknown-none-elf \
      -Zbuild-std=core,alloc,compiler_builtins -Zbuild-std-features=compiler-builtins-mem \
      --target-dir "$OBJ/rust-target" )
  # as an object: LLVM's m68k code linked into an executable by GNU ld gave
  # wrong output in the emulated lab where the same code as an object ran
  # right (elf.library's image was byte-identical to GNU ld's own link at
  # that address, so the fault is the code's; README.md, "Rust")
  "$LD" -r --gc-sections -u _start -e _start "$OBJ/start.o" "$OBJ/shim.o" \
    "$OBJ/rust-target/m68k-unknown-none-elf/release/libopenelf_hello.a" -o "$OBJ/rust-hello.o"
  # without the LLVM bitcode rustc embeds and the symbols no relocation uses
  "${OBJCOPY:-m68k-linux-gnu-objcopy}" --remove-section=.llvmbc --remove-section=.llvmcmd --strip-unneeded \
    "$OBJ/rust-hello.o" "$OUT/ELF/rust-hello"
else
  echo "no cargo with rust-src: ELF/rust-hello not built" >&2
fi

rm -rf "$OBJ"
for f in "$OUT"/Libs/* "$OUT"/C/* "$OUT"/ELF/*; do
  printf '%-40s %7s bytes\n' "${f#"$OUT"/}" "$(wc -c < "$f")"
done
