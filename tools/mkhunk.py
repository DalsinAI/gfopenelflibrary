#!/usr/bin/env python3
# mkhunk.py IN.elf OUT: a 68k ELF executable linked at 0 with its
# relocations kept (ld -q -N -Ttext=0) as an AmigaDOS load file of one code
# hunk: every allocated section in address order (BSS as zeros), and each
# R_68K_32 as a HUNK_RELOC32 entry. PC-relative relocations need nothing.
# Any other kind, or an undefined symbol, stops it. elf.library itself is
# built this way: the hunk loader must be able to load the ELF loader.
# Copyright (c) 2026 AmiCradle Team. MIT.
import struct, sys

def main(src, dst):
    d = open(src, "rb").read()
    if d[:4] != b"\x7fELF" or d[4] != 1 or d[5] != 2 or struct.unpack(">H", d[18:20])[0] != 4:
        sys.exit(f"{src}: not a 32-bit big-endian 68k ELF file")
    shoff, = struct.unpack(">I", d[32:36])
    shentsize, shnum, shstrndx = struct.unpack(">HHH", d[46:52])
    sh = [struct.unpack(">10I", d[shoff + i * shentsize: shoff + i * shentsize + 40]) for i in range(shnum)]
    alloc = [(i, s) for i, s in enumerate(sh) if s[2] & 2 and s[5]]
    if not alloc:
        sys.exit(f"{src}: nothing to load")
    if min(s[3] for _, s in alloc) != 0:
        sys.exit(f"{src}: not linked at 0 (use -Ttext=0)")
    end = max(s[3] + s[5] for _, s in alloc)
    img = bytearray(end + (-end % 4))
    for i, s in alloc:
        if s[1] != 8:           # SHT_NOBITS stays zero
            img[s[3]:s[3] + s[5]] = d[s[4]:s[4] + s[5]]
    relocs = []
    for i, s in enumerate(sh):
        if s[1] not in (4, 9) or not (sh[s[7]][2] & 2):
            continue
        ent = 12 if s[1] == 4 else 8
        symtab = sh[s[6]]
        for k in range(s[5] // ent):
            off, info = struct.unpack(">II", d[s[4] + k * ent: s[4] + k * ent + 8])
            rtype, sym = info & 0xff, info >> 8
            shndx, = struct.unpack(">H", d[symtab[4] + sym * 16 + 14: symtab[4] + sym * 16 + 16])
            if sym and shndx == 0:
                sys.exit(f"{src}: undefined symbol at {off:#x}")
            if rtype == 1:      # R_68K_32
                if shndx != 0xfff1:     # absolute symbols don't move
                    relocs.append(off)
            elif rtype in (0, 4, 5, 6):
                pass
            else:
                sys.exit(f"{src}: relocation type {rtype} at {off:#x} has no hunk form")
    relocs = sorted(set(relocs))
    n = len(img) // 4
    out = struct.pack(">IIIII", 0x3f3, 0, 1, 0, 0) + struct.pack(">I", n)
    out += struct.pack(">II", 0x3e9, n) + bytes(img)
    if relocs:
        out += struct.pack(">III", 0x3ec, len(relocs), 0) + b"".join(struct.pack(">I", r) for r in relocs) + struct.pack(">I", 0)
    out += struct.pack(">I", 0x3f2)
    open(dst, "wb").write(out)
    print(f"{dst}: {len(out)} bytes, one hunk of {len(img)} bytes, {len(relocs)} relocations")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: mkhunk.py IN.elf OUT")
    main(sys.argv[1], sys.argv[2])
