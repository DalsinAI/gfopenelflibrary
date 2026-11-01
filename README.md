# GFOpenELF: elf.library

elf.library 1.0 loads 68k ELF programs and shared objects on AmigaOS 3.2.3
and GFOpenBench, so what GCC's and LLVM's ELF toolchains build (C, C++ and
Rust) runs with no conversion to the hunk format. Our own code, MIT, written
from the System V ABI (ELF), the m68k psABI's relocation list and our own
design, never from another loader's source (`CLEAN-ROOM.md`). The design and
its reasons are in the design document linked from amigachrome-guest #446.

This repository is `DalsinAI/openamigaelflibrary`. elf.library began in
`DalsinAI/amigachrome-guest` as `libraries/openelf` (#446, #449) and moved
here with its history on 23 October 2026.

**Load order** (Dale, 22 October 2026): the Amiga's own hunk format is tried
first, then ELF; when neither matches, the original error goes back
unchanged, so whatever the caller tries next (datatypes, a script) still
runs.

| File | What it is |
| --- | --- |
| `elf_core.c`, `.h` | The loader, plain C, no Amiga headers (10.0 KB at -Os): `ET_REL` objects (AROS and MorphOS load programs this way) with their sections packed into segments of at most 32 KB so they fit fragmented memory; `ET_EXEC` executables linked with their relocations kept (`ld -q`); `ET_DYN` shared objects and PIEs with their dynamic relocations, `DT_NEEDED`, constructors and destructors. Sections keep the alignment they ask (up to 4 KB). A program with constructors or destructors gets a start routine of its own: preinit, init (an object's numbered `.init_array.N` sections in order), the program's entry with D0 and A0 as they came, then the destructors the other way. It refuses, with a reason, what it can't load: other CPUs, Linux programs (`PT_INTERP`), thread-local storage, `-fpic` objects (GOT relocations), executables with no relocations, executables with a symbol nothing defines or an entry outside their code. |
| `openelf_lib.c`, `openelf_stubs.S` | The library: RomTag, jump table, AllocMem, the file through DOS, shared objects kept by name, the LoadSeg hook. |
| `openelf_amiga.h` | The Exec and DOS facts the library uses, in our own words, so it builds with no NDK. |
| `openelf_public.h`, `elf.lvo` | The calls, LVO by LVO. |
| `c/ElfHook.c` | `ElfHook` turns ELF loading in LoadSeg on, `ElfHook OFF` off. |
| `c/ElfTest.c` | The lab tool: `ElfTest FILE` (ElfLoadSeg, RunCommand, UnLoadSeg), `ElfTest CALL FILE` (the same, jumping to the seglist itself, for emulators), `ElfTest DL FILE SYMBOL`. |
| `crt/amiga_start.S` | The start of an ELF program: saves SysBase, calls `int amiga_main(const char *args, long len)`. |
| `crt/amiga_shim.c` | C-callable PutStr, a number through VPrintf, and memory from one exec pool, for languages with no register calls (Rust). |
| `examples/` | `hello.c` (built as `-q`, as an object and as a PIE) and `greet.c` (a shared object with a constructor). |
| `rust/hello` | Rust on AmigaOS: core and alloc, the global allocator on an exec pool (it frees with the size, as FreePooled wants). |
| `rust/fixture`, `tests/` | The tests' fixtures and `test_core.c`. |
| `tools/mkhunk.py` | An ELF linked at 0 with `-q` as a one-hunk load file: how elf.library itself and the tools are built. |
| `tests/test_openelf.sh`, `tests/test_openelf_vamos.sh` | The tests (below), run by `.github/workflows/check-openelf.yml`. |
| `build-amiga.sh` | The drawer: `Libs/elf.library`, `C/ElfHook`, `C/ElfTest`, `ELF/` examples. Needs only `gcc-m68k-linux-gnu` (and Rust with `rust-src` for `ELF/rust-hello`). 68040, soft float, -Os. |

## The API

| LVO | Call | Registers |
| --- | --- | --- |
| -30 | `BPTR ElfLoadSeg(STRPTR name)` | D1 |
| -36 | `LONG ElfIdentify(APTR buf, ULONG len)` | A0, D0 |
| -42 | `APTR DLOpen(STRPTR name, ULONG flags)` | A0, D0 |
| -48 | `APTR DLSym(APTR handle, STRPTR name)` | A0, A1 |
| -54 | `LONG DLClose(APTR handle)` | A0 |
| -60 | `LONG ElfHook(LONG on)` | D0 |
| -66 | `LONG ElfLastError(STRPTR buf, ULONG len)` | A0, D0 |
| -72 | `STRPTR ElfErrorString(LONG err)` | D0 |

`ElfLoadSeg` returns an ordinary seglist (UnLoadSeg frees it; RunCommand and
CreateProc run it). `DLOpen` finds a name with no path in `LIBS:elf/` and
then the current directory, opens its `DT_NEEDED` first, binds every symbol
at once (`RTLD_NOW`; every object is global) and runs its constructors.
Objects that need each other round a cycle are refused, as is one that needs
more than 16, and a chain of needs more than 16 deep or deeper than the
caller's stack has room for (each level takes about 1.2 KB of it, more
with DOS's own calls). `DLSym` on a handle looks in that object and then in what it
needs.
OS 4.1's elf.library is an interface, not a jump table, so no 68k library
can match it byte for byte; the DL calls keep its names and dlopen's
meaning, so source that uses them ports by changing how the library is
called.

## Building ELF programs for AmigaOS

With the Debian/Ubuntu `gcc-m68k-linux-gnu` (or any m68k ELF GCC):

    m68k-linux-gnu-gcc -m68040 -msoft-float -Os -ffreestanding -fno-pic -c prog.c
    m68k-linux-gnu-ld -q -N -e _start crt/amiga_start.o prog.o -o prog       # executable, relocations kept
    m68k-linux-gnu-ld -r -e _start crt/amiga_start.o prog.o -o prog          # or an object

`-N` keeps the linker from padding between text and data to a page. A PIE
(`-fPIE`, `ld -pie --no-dynamic-linker`) works too; `-fpic` objects do not
(build objects with `-fno-pic`).

## Rust

`rust/hello` builds with stable rustc and `rust-src`
(`RUSTC_BOOTSTRAP=1 cargo build -Zbuild-std=core,alloc,compiler_builtins
--target m68k-unknown-none-elf -C target-cpu=M68040`): LLVM's m68k backend,
soft float, then GNU ld. elf.library loads and runs it.

**The open problem is LLVM, not loading.** With rustc 1.97 (LLVM 22.1),
core's integer formatting (`format!("{}", 30u32)`) crashes or prints
wrong digits, with every target CPU, and with no elf.library involved
(linked by GNU ld and run under qemu-m68k directly). Some code linked into
an executable also behaved differently from the same code as an object;
elf.library's image there was byte-identical to GNU ld's own link at the
same address, so the loader is not the cause. So `rust/hello` prints numbers
through the C side. LLVM's small code model also reaches data with 16-bit
PC-relative offsets, which caps a program near 32 KB; `-C code-model=large`
uses absolute addresses instead. The way on is GCC's mature m68k backend
under rustc (rustc_codegen_gcc), or LLVM fixes; the loader is ready for
either.

## Tests

- `tests/test_openelf.sh`: the core under qemu-m68k, at -O0 and -Os.
  It builds the fixtures (GCC objects, `ld -r`, `-q`, PIE, shared objects
  with `DT_NEEDED`, a Rust object and executable; clang 19 or later when
  present), loads and runs each one, checks every refusal, and loads each
  file cut short and with bytes changed (about 2,500 damaged files) with
  every allocation counted.
- `tests/test_openelf_vamos.sh`: the built `elf.library` in amitools'
  vamos (68040): the examples run from `ElfTest`, `libgreet.so` through
  DLOpen, `greet_answer` found through `libouter.so`, which needs it, a
  cycle of shared objects refused, and a hunk file refused.
- CI: `.github/workflows/check-openelf.yml`.

Seen on a real OS 3.2.3 (22 October 2026, a copy of Instance-23, Kick
47.115): `ElfHook` on, the three C examples run from the Shell, `DLOpen` of
`libgreet.so` returns 42, a text file is still "not executable", and
`ElfHook OFF` turns it off; the library takes 12.6 KB of fast RAM. Not yet
seen: starting an ELF program from a Workbench icon.
