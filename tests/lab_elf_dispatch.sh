#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. MIT licence (see LICENSE).
# The ELF dispatch, ready for a lab the moment one is running.
# It does not start a lab, and it does not take a lab path on the command
# line: lab_instance.py stop kills every process whose command line names
# that lab. Set LAB to the lab folder.
#
#   sh tests/test_openelf_vamos.sh          the same sentences, under vamos
#   LAB=/path/to/lab sh tests/lab_elf_dispatch.sh
#
# What a pass is, in the lab's output:
#   a PowerPC ELF     "This program is PowerPC code; AC460 is not connected"
#                     (the AC460 route, no loader in elf.library yet), or
#                     "ElfLoadSegDOS ... = a seglist" once a loader takes it
#   an x86-64 ELF     "This program is x86-64 code; no x86 emulator yet"
#   an AArch64 ELF    "This program is AArch64 code; no AArch64 emulator yet"
#   a Hunk file       "Hello from an ELF program"  (68k, run by DOS)
# Instance-5, Instance-40 and the card are refused.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
oe=$here/..

case "${LAB:-}" in
*"Instance-5"*|*"Instance-40"*|*"instances/5"*|*"instances/40"*)
    echo "refused: Instance-5 and Instance-40 are not labs" >&2; exit 2;;
esac
case "${LAB:-}" in
/dev/sd*|/dev/mmc*) echo "refused: that path is a card" >&2; exit 2;;
esac

echo "=== vamos: the sentences ==="
sh "$here/test_openelf_vamos.sh"

if [ -z "${LAB:-}" ]; then
    echo "no LAB: the files are not copied. When a lab is running:"
    echo "  LAB=/path/to/lab sh tests/lab_elf_dispatch.sh"
    echo "Start it first, from a command that does not name this script:"
    echo "  lab_instance.py start <lab> --who Team6 --purpose \"ELF dispatch\" --binary <a1200native>"
    exit 0
fi

[ -d "$LAB/devices/harddisks/DH0" ] || { echo "not a lab: $LAB" >&2; exit 2; }
command -v m68k-linux-gnu-gcc >/dev/null || { echo "no m68k-linux-gnu-gcc: the lab files were not built" >&2; exit 2; }

tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
OUT=$tmp/drawer sh "$oe/build-amiga.sh" >"$tmp/build.log" 2>&1 || { cat "$tmp/build.log"; exit 1; }
OUT=$tmp/fmt sh "$oe/tests/mkformats.sh" >"$tmp/fmt.log" 2>&1 || { cat "$tmp/fmt.log"; exit 1; }

DH0=$LAB/devices/harddisks/DH0
D=$DH0/ElfDispatch
mkdir -p "$D" "$DH0/C" "$DH0/Libs"
cp "$tmp/drawer/C/ElfTest" "$DH0/C/ElfTest"
cp "$tmp/drawer/Libs/elf.library" "$DH0/Libs/elf.library"
cp "$tmp/fmt/ppc-exec" "$tmp/fmt/x86_64-object" "$tmp/fmt/aarch64-object" "$tmp/fmt/hunk-hello" "$D/"
echo "copied ElfTest, elf.library and the four files into the lab"

# labrun.sh lives with LabAgent. AMICRADLE is the tree; otherwise a checkout beside this one.
bench=${AMICRADLE:-$oe/../amicradle}/tools/labbench/labrun.sh
if [ ! -x "$bench" ] && [ ! -f "$bench" ]; then
    echo "files are in the lab. labrun.sh was not found (set AMICRADLE). Run, with LAB set:"
    echo "  sh tools/labbench/labrun.sh <this>/tests/elf-dispatch.script"
    exit 0
fi

out=$(mktemp)
if ! sh "$bench" "$here/elf-dispatch.script" 180 >"$out" 2>&1; then
    cat "$out"; echo "labrun failed"; exit 1
fi
cat "$out"

part() { awk -v h="--- $1 ---" 'index($0,h){p=1;next} /^--- /{if(p)exit} p' "$out"; }
fail=0
check() { # name, file of that part, want, and whether a seglist also passes
    name=$1; want=$2; also=$3
    body=$(part "$name")
    if printf '%s\n' "$body" | grep -q -- "$want"; then echo "ok   $name"; return; fi
    if [ -n "$also" ] && printf '%s\n' "$body" | grep -q -- "$also"; then echo "ok   $name (a loader took it)"; return; fi
    echo "FAIL $name"
    printf '%s\n' "$body"
    fail=1
}
check ppc "AC460 is not connected" "= a seglist"
check x86-64 "This program is x86-64 code; no x86 emulator yet" ""
check aarch64 "This program is AArch64 code; no AArch64 emulator yet" ""
check hunk "Hello from an ELF program" ""
# a refused ELF must not have been run
for bad in x86-64 aarch64; do
    if part "$bad" | grep -q "= a seglist"; then echo "FAIL $bad was loaded"; fail=1; fi
done
rm -f "$out"
[ "$fail" = 0 ] && echo "elf dispatch: the lab agreed"
exit "$fail"
