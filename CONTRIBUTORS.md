# Contributors

## Creator and maintainer

- **SacredTrees** ([@SacredTrees](https://github.com/SacredTrees)): created GFOpenELF, designs it and maintains it.

## The AmigaChrome team

We are The AmigaChrome Team, who build GFOpenELF alongside SacredTrees:

- **Agnus**, our coordinator, who keeps every thread moving.
- **The threads**, each one taking a piece of the work from design to release.

## Copyright holder

GFOpenELF's code (elf.library, its tools, examples and tests) and documents are
Copyright (c) 2026 AmigaChrome Team, released under the MIT licence (`LICENSE`).

## Third-party work in this repository

None. Every file is our own, written from public interface facts as
`CLEAN-ROOM.md` describes.

## Used at build or test time, not included

- **GCC** (`gcc-m68k-linux-gnu`) and GNU binutils: build everything.
- **Rust** (`rustc` with `rust-src`): builds the Rust example and fixture.
- **qemu-m68k**: runs the loader core's tests.
- **amitools** (vamos): runs elf.library in an emulated AmigaOS for the tests.

Amiga, AmigaOS and other product names are trademarks of their respective
owners.
