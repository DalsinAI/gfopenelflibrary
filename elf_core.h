/* GFOpenELF's loader core: 68k ELF files (objects, executables, shared
 * objects) into memory, relocated, as segments a seglist can be made of.
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * Plain C with no Amiga headers, so the same file builds into elf.library
 * and into the tests (tests/test_openelf.sh loads real GCC and LLVM
 * output with it under qemu-m68k and runs the loaded code). Written from
 * the System V ABI (ELF), the m68k psABI's relocation list and our own
 * design (README.md); never from another loader's source.
 *
 * Three kinds of file load:
 *   ET_REL   a relocatable object (ld -r, or one compiler output), as AROS
 *            and MorphOS load programs: its sections packed into segments
 *            of at most env->seg_max bytes each, so it fits fragmented
 *            memory, then relocated
 *   ET_EXEC  an executable linked with its relocations kept (ld -q,
 *            --emit-relocs): one block, moved by the relocations
 *   ET_DYN   a shared object or a position-independent executable (-pie):
 *            one block, its dynamic relocations applied, DT_NEEDED handed
 *            to env->needed, undefined symbols to env->resolve
 *
 * Every segment is one allocation laid out as an AmigaDOS segment:
 *   +0 the allocation's size in bytes, +4 the link to the next (left 0 for
 *   the caller), +8 the contents.
 * The core never chains them: elf.library turns them into BPTRs, the tests
 * into nothing. Memory comes only from env->alloc, cleared. */
#ifndef OPENELF_CORE_H
#define OPENELF_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char oe_u8;
typedef unsigned short oe_u16;
typedef unsigned long oe_u32;   /* 32 bits on the 68k; wider elsewhere, kept to 32 */
typedef long oe_s32;

/* What a load asks of its caller. */
enum oe_mem { OE_MEM_SEGMENT = 0, OE_MEM_TEMP = 1 };

struct oe_env {
    /* size bytes, cleared, at least 4-byte aligned; NULL when there is none */
    void *(*alloc)(struct oe_env *env, oe_u32 size, enum oe_mem kind);
    void (*free)(struct oe_env *env, void *mem, oe_u32 size, enum oe_mem kind);
    /* len bytes at offset of the file into buf: 0, or -1 on any shortfall */
    oe_s32 (*read)(struct oe_env *env, oe_u32 offset, void *buf, oe_u32 len);
    /* a symbol this file uses and does not define: its address, or 0 with
     * *found left 0; NULL resolves nothing */
    oe_u32 (*resolve)(struct oe_env *env, const char *name, int *found);
    /* a DT_NEEDED name, before the relocations: 0 to go on, else an
     * oe_error; NULL accepts every name */
    int (*needed)(struct oe_env *env, const char *name);
    oe_u32 seg_max;             /* ET_REL: most bytes of sections per segment, 0 = one segment */
    /* 1 for a program: when it has constructors or destructors, entry is a
     * start routine the core adds that runs the constructors, calls the
     * program's own entry (D0 and A0 kept) and runs the destructors after */
    int program;
    void *user;
    /* the file's length in bytes, 0 when not known: when known, a section
     * or segment said to hold more of the file than there is is refused
     * (OE_ERR_READ) before memory is allocated for it */
    oe_u32 file_size;
};

enum oe_error {
    OE_OK = 0,
    OE_ERR_NOT_ELF,             /* no \x7fELF */
    OE_ERR_CLASS,               /* not 32-bit big-endian ELF version 1 */
    OE_ERR_MACHINE,             /* not EM_68K: oe_info.machine says which */
    OE_ERR_TYPE,                /* not ET_REL, ET_EXEC or ET_DYN */
    OE_ERR_READ,                /* the file is shorter than its headers say */
    OE_ERR_NOMEM,
    OE_ERR_FORMAT,              /* headers that contradict themselves */
    OE_ERR_RELOC,               /* a relocation type we don't apply: oe_image.detail */
    OE_ERR_RANGE,               /* a 16- or 8-bit relocation that doesn't fit */
    OE_ERR_UNDEF,               /* a symbol nothing defines: oe_image.name */
    OE_ERR_FIXED,               /* ET_EXEC with no relocations kept: relink with -q */
    OE_ERR_INTERP,              /* wants a program interpreter: a Linux or BSD program */
    OE_ERR_TLS,                 /* thread-local storage */
    OE_ERR_NEEDED               /* env->needed refused a DT_NEEDED */
};

/* What the first bytes of a file say, without loading it. */
struct oe_info {
    int is_elf;
    int elf_class, data, osabi;  /* e_ident[4], [5], [7] */
    int type, machine;           /* e_type, e_machine */
    oe_u32 entry;
};

/* buf holds the file's first len bytes (52 are enough): OE_OK when it is a
 * 68k ELF we can load, else the reason, with *info filled as far as known */
int oe_identify(const void *buf, oe_u32 len, struct oe_info *info);

#define OE_NAME_MAX 64

struct oe_image {
    int type;                   /* ET_REL, ET_EXEC or ET_DYN */
    oe_u32 nseg, segcap;
    void **seg;                 /* nseg segments, laid out as above, in an OE_MEM_TEMP array of segcap */
    oe_u32 entry;               /* where a program starts, 0 for none */
    oe_u32 bias;                /* ET_EXEC, ET_DYN: load address minus link address */
    /* ET_DYN: the dynamic symbols, in the loaded image, for oe_lookup */
    oe_u32 dynsym, dynstr, dynstrsz, nsym, hash;
    /* constructors and destructors: DT_INIT/DT_FINI and the arrays */
    oe_u32 init, fini, init_array, init_count, fini_array, fini_count, preinit_array, preinit_count;
    /* why a load failed */
    oe_u32 detail;              /* the relocation type for OE_ERR_RELOC and OE_ERR_RANGE */
    char name[OE_NAME_MAX];     /* the symbol for OE_ERR_UNDEF, the name for OE_ERR_NEEDED */
};

/* Load the file env->read reads. OE_OK fills *img; on any error nothing is
 * left allocated and img->detail or img->name may say more. */
int oe_load(struct oe_env *env, struct oe_image *img);

/* Free what oe_load allocated (the segments and the array). */
void oe_unload(struct oe_env *env, struct oe_image *img);

/* ET_DYN: a defined global or weak symbol's address, else 0 */
oe_u32 oe_lookup(const struct oe_image *img, const char *name);

/* A short English sentence for an oe_error. */
const char *oe_strerror(int err);

/* The 6-byte stub that starts a seglist whose entry is not its first byte:
 * jmp entry.l */
#define OE_STUB_SIZE 8
void oe_write_stub(void *at, oe_u32 entry);

#ifdef __cplusplus
}
#endif
#endif
