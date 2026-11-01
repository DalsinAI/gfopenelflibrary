# What "clean-room" means in this repository

Many files here say they are clean-room. This page says exactly what that
covers, so the claim matches how the work is done.

## The rule

Everything in this repository is the Team's own code, MIT, Copyright AmiCradle
Team, unless a file or `CONTRIBUTORS.md` says otherwise.

**Never copied.** No code, bytes, tables, strings, comments or documentation
text from:

- Kickstart, AmigaOS 3.x or 4.x, or any other Hyperion, Commodore or Cloanto
  release;
- the NDK 3.2 or the OS 4.1 SDK, beyond the interface facts below;
- AROS, MorphOS or any other implementation of these APIs.

Copying includes translating code into another language and paraphrasing it
line by line.

**What we work from.**

- Interface facts: names of libraries, functions, structures, fields, tags
  and constants; their values, offsets, sizes and register use. These come
  from the NDK 3.2 headers, FD and SFD files and autodocs, written into our own
  headers in our own words.
- Behaviour: the AmigaDOS manuals, the RKMs and autodocs, and what OS 3.2.3
  is seen to do in a lab (black-box tests, side by side with ours).
- Our own design.

**No disassembly.** We do not disassemble Kickstart, OS 3.2.3 or OS 4.1 code to
write ours. Fingerprints that recognise a known routine (a few bytes and a
hash) are allowed.

## AROS: consulted, not copied

This is not a strict clean room in the legal sense, where the people who write
the code never see another implementation. Dale's decision of 12 October 2026
is "clean-room everything, but learn from AROS", so:

- People may read AROS (AROS Public License 1.1) for design, edge-case
  behaviour and how it solves the hard parts, and may run AROS as a third
  opinion in the side-by-side tests.
- Nothing is copied, translated or paraphrased from it, as above.
- A file whose design follows AROS says so in its header, and is listed below.
  That keeps provenance clear if anyone asks.

So the accurate description is: **an independent implementation, written from
public interfaces and observed behaviour, with AROS consulted for design.**

## Files whose design follows AROS

| File | What follows AROS | What is ours |
|---|---|---|
| `libraries/opengadtools/frames/openframes.c`, `openframes.h` | The decorator model: a table of drawing methods, and its method names (`WDM_DRAW_WINBORDER`, `WDM_DRAW_SYSIMAGE`, `WDM_DRAW_BORDERPROPBACK`, `SDM_DRAW_SCREENBAR` and the rest) | All the code, the theme rules and the drawing |

Add a row when a new file follows an AROS design.

## If code ever comes from elsewhere

Code taken from another project keeps its own licence and notice in the file,
and is listed in `CONTRIBUTORS.md`. It is never relabelled MIT.
