#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. MIT licence (see LICENSE).
# The files LoadSeg's format dispatch is checked with (amicradle-guest
# libraries/opendos/DESIGN-LOADSEG-FORMATS.md), into one drawer:
#   hunk-hello        examples/hello.c as a hunk program (mkhunk.py)
#   elf68k-exec       the same as a 68k ELF executable (ld -q)
#   elf68k-object     ... as an object (ld -r)
#   elf68k-pie        ... position-independent (-pie)
#   aros68k-object    elf68k-object marked as AROS's (EI_OSABI 15, ABI version 1)
#   ppc-object        a PowerPC ELF object (EM_PPC 20, ET_REL: PowerUP's form)
#   ppc-exec          a PowerPC ELF executable (ET_EXEC)
#   i386-object, x86_64-object, arm-object, aarch64-object   ELF objects for
#                     the Stage 5 cores (EM 3, 62, 40, 183)
#   aros-i386-object, aros-x86_64-object, aros-arm-object, aros-aarch64-object,
#   aros-ppc-object   the same marked as AROS's
#   warpos-hunk       a hunk program with a HUNK_PPC_CODE hunk (WarpOS's mixed form)
#   text-file, empty-file
# The other CPUs' objects come from clang (any clang has those back ends);
# without clang a 52- or 64-byte header of each kind stands in.
#   OUT=drawer sh tests/mkformats.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
oe=$here/..
OUT=${OUT:-$PWD/formats}
CC=${CC:-m68k-linux-gnu-gcc}
LD=${LD:-m68k-linux-gnu-ld}
mkdir -p "$OUT"
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
F="-std=gnu11 -m68040 -msoft-float -Os -ffreestanding -fno-builtin -fno-pic -fomit-frame-pointer
   -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-stack-protector -Wall -Werror -I$oe"
# shellcheck disable=SC2086
{
"$CC" -m68040 -c "$oe/crt/amiga_start.S" -o "$tmp/start.o"
"$CC" $F -c "$oe/examples/hello.c" -o "$tmp/hello.o"
"$LD" -q -N -Ttext=0 --no-warn-rwx-segments --build-id=none -e _start "$tmp/start.o" "$tmp/hello.o" -o "$tmp/hello-0.elf"
python3 "$oe/tools/mkhunk.py" "$tmp/hello-0.elf" "$OUT/hunk-hello"
"$LD" -q -N -e _start --no-warn-rwx-segments --build-id=none "$tmp/start.o" "$tmp/hello.o" -o "$OUT/elf68k-exec"
"$LD" -r -e _start "$tmp/start.o" "$tmp/hello.o" -o "$OUT/elf68k-object"
"$CC" -m68040 -c -fPIE "$oe/crt/amiga_start.S" -o "$tmp/start-pie.o"
"$CC" $F -fPIE -c "$oe/examples/hello.c" -o "$tmp/hello-pie.o"
"$LD" -pie -e _start --no-dynamic-linker -z norelro --hash-style=both --no-warn-rwx-segments --build-id=none \
  "$tmp/start-pie.o" "$tmp/hello-pie.o" -o "$OUT/elf68k-pie"
}

echo 'int start(int a) { return a + 1; }' > "$tmp/k.c"
have_clang=0
command -v clang >/dev/null && clang --target=powerpc-unknown-linux-gnu -c "$tmp/k.c" -o "$tmp/probe.o" 2>/dev/null && have_clang=1
obj() { # name clang-target
  if [ $have_clang = 1 ]; then clang --target="$2" -O1 -fno-pic -c "$tmp/k.c" -o "$OUT/$1"; return; fi
  python3 -I - "$OUT/$1" "$2" <<'EOF'
import struct, sys
out, t = sys.argv[1], sys.argv[2]
m = {"powerpc": (20, 1, 2), "i386": (3, 1, 1), "x86_64": (62, 2, 1), "armv7a": (40, 1, 1), "aarch64": (183, 2, 1)}[t.split("-")[0]]
mach, cls, data = m
e = "<" if data == 1 else ">"
ident = b"\x7fELF" + bytes([cls, data, 1, 0]) + bytes(8)
if cls == 1: h = ident + struct.pack(e + "HHIIIIIHHHHHH", 1, mach, 1, 0, 0, 0, 0, 52, 0, 0, 40, 0, 0)
else: h = ident + struct.pack(e + "HHIQQQIHHHHHH", 1, mach, 1, 0, 0, 0, 0, 64, 0, 0, 64, 0, 0)
open(out, "wb").write(h)
EOF
}
obj ppc-object powerpc-unknown-linux-gnu
obj i386-object i386-unknown-linux-gnu
obj x86_64-object x86_64-unknown-linux-gnu
obj arm-object armv7a-unknown-linux-gnueabi
obj aarch64-object aarch64-unknown-linux-gnu
if [ $have_clang = 1 ] && command -v ld.lld >/dev/null; then
  ld.lld -m elf32ppc -e start "$OUT/ppc-object" -o "$OUT/ppc-exec"
else
  # an ET_EXEC PowerPC header standing in
  python3 -I - "$OUT/ppc-object" "$OUT/ppc-exec" <<'EOF'
import sys
d = bytearray(open(sys.argv[1], "rb").read()); d[16:18] = b"\x00\x02"; open(sys.argv[2], "wb").write(d)
EOF
fi

# AROS's mark (its collect-aros stamps EI_OSABI 15, EI_ABIVERSION 1 on the
# final link, which is a relocatable object on every CPU)
aros() { python3 -I - "$OUT/$1" "$OUT/$2" <<'EOF'
import sys
d = bytearray(open(sys.argv[1], "rb").read()); d[7] = 15; d[8] = 1; open(sys.argv[2], "wb").write(d)
EOF
}
aros elf68k-object aros68k-object
for c in i386 x86_64 arm aarch64 ppc; do aros "$c-object" "aros-$c-object"; done

# WarpOS's mixed form: a 68k code hunk, then a HUNK_PPC_CODE (1257) hunk
python3 -I - "$OUT/warpos-hunk" <<'EOF'
import struct, sys
L = lambda *v: struct.pack(">%dI" % len(v), *v)
code68k = bytes.fromhex("70004e75")             # moveq #0,d0; rts
ppc = bytes.fromhex("38600000" "4e800020")       # li r3,0; blr
f = L(0x3F3, 0, 2, 0, 1, 1, 2)                   # HUNK_HEADER: no names, 2 hunks, sizes in longs
f += L(0x3E9, 1) + code68k + L(0x3F2)            # HUNK_CODE, HUNK_END
f += L(0x4E9, 2) + ppc + L(0x3F2)                # HUNK_PPC_CODE, HUNK_END
open(sys.argv[1], "wb").write(f)
EOF
echo "not a program" > "$OUT/text-file"
: > "$OUT/empty-file"
ls -l "$OUT"
