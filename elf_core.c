/* OpenELF's loader core (elf_core.h says what it does).
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * Every field is read byte by byte, big-endian, so the core runs the same
 * on the 68k and anywhere else. It keeps little in memory: the section
 * headers, one symbol table and its strings while an object loads, and
 * relocations a few dozen at a time. */
#include "elf_core.h"

/* ---- ELF and m68k psABI numbers ---------------------------------------- */

#define ET_REL 1
#define ET_EXEC 2
#define ET_DYN 3
#define EM_68K 4

#define SHT_SYMTAB 2
#define SHT_RELA 4
#define SHT_NOBITS 8
#define SHT_REL 9
#define SHT_INIT_ARRAY 14
#define SHT_FINI_ARRAY 15
#define SHT_PREINIT_ARRAY 16
#define SHF_ALLOC 2
#define SHF_EXECINSTR 4
#define SHN_UNDEF 0
#define SHN_LORESERVE 0xff00
#define SHN_ABS 0xfff1
#define SHN_COMMON 0xfff2
#define SHN_XINDEX 0xffff
#define STB_LOCAL 0
#define STB_WEAK 2
#define STT_TLS 6

#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_TLS 7
#define PF_X 1

#define DT_NULL 0
#define DT_NEEDED 1
#define DT_PLTRELSZ 2
#define DT_HASH 4
#define DT_STRTAB 5
#define DT_STRSZ 10
#define DT_SYMTAB 6
#define DT_RELA 7
#define DT_RELASZ 8
#define DT_RELAENT 9
#define DT_INIT 12
#define DT_FINI 13
#define DT_PLTREL 20
#define DT_JMPREL 23
#define DT_INIT_ARRAY 25
#define DT_FINI_ARRAY 26
#define DT_INIT_ARRAYSZ 27
#define DT_FINI_ARRAYSZ 28
#define DT_PREINIT_ARRAY 32
#define DT_PREINIT_ARRAYSZ 33
#define DT_GNU_HASH 0x6ffffef5UL

#define R_68K_NONE 0
#define R_68K_32 1
#define R_68K_16 2
#define R_68K_8 3
#define R_68K_PC32 4
#define R_68K_PC16 5
#define R_68K_PC8 6
#define R_68K_GOT32 7       /* 7..12: the GOT forms, from -fpic objects */
#define R_68K_GOT8O 12
#define R_68K_PLT32 13
#define R_68K_PLT16 14
#define R_68K_PLT8 15
#define R_68K_PLT32O 16     /* 16..18 */
#define R_68K_COPY 19
#define R_68K_GLOB_DAT 20
#define R_68K_JMP_SLOT 21
#define R_68K_RELATIVE 22
#define R_68K_TLS_FIRST 25  /* 25..42: thread-local storage */
#define R_68K_TLS_LAST 42

#define EHDR_SIZE 52
#define SHDR_SIZE 40
#define PHDR_SIZE 32
#define SYM_SIZE 16
#define RELA_SIZE 12
#define REL_SIZE 8
#define RELOC_CHUNK 32      /* relocations read at a time */
#define MAX_ALIGN 4096      /* the most a section may ask */
#define MAX_CTOR 8          /* constructor and destructor arrays a program may have */

/* ---- Small helpers ------------------------------------------------------ */

static oe_u32 g16(const oe_u8 *p) { return ((oe_u32)p[0] << 8) | p[1]; }
static oe_u32 g32(const oe_u8 *p)
{ return ((oe_u32)p[0] << 24) | ((oe_u32)p[1] << 16) | ((oe_u32)p[2] << 8) | p[3]; }
static void p16(oe_u8 *p, oe_u32 v) { p[0] = (oe_u8)(v >> 8); p[1] = (oe_u8)v; }
static void p32(oe_u8 *p, oe_u32 v)
{ p[0] = (oe_u8)(v >> 24); p[1] = (oe_u8)(v >> 16); p[2] = (oe_u8)(v >> 8); p[3] = (oe_u8)v; }

#define ADDR(ptr) ((oe_u32)(unsigned long)(ptr))
#define PTR(addr) ((oe_u8 *)(unsigned long)(addr))
#define M32(v) ((v) & 0xffffffffUL)

static void mclear(void *d, oe_u32 n) { oe_u8 *q = (oe_u8 *)d; while (n--) *q++ = 0; }
static void mcopy(void *d, const void *s, oe_u32 n)
{ oe_u8 *q = (oe_u8 *)d; const oe_u8 *r = (const oe_u8 *)s; while (n--) *q++ = *r++; }
static int streq(const char *a, const char *b)
{ while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void setname(struct oe_image *img, const char *s)
{ oe_u32 i = 0; while (s[i] && i < OE_NAME_MAX - 1) { img->name[i] = s[i]; i++; } img->name[i] = 0; }

void oe_write_stub(void *at, oe_u32 entry)
{
    oe_u8 *p = (oe_u8 *)at;
    p16(p, 0x4ef9);             /* jmp (xxx).l */
    p32(p + 2, entry);
    p16(p + 6, 0x4e71);         /* nop, to a longword */
}

const char *oe_strerror(int err)
{
    switch (err) {
    case OE_OK: return "loaded";
    case OE_ERR_NOT_ELF: return "not an ELF file";
    case OE_ERR_CLASS: return "not a 32-bit big-endian ELF file";
    case OE_ERR_MACHINE: return "an ELF file for another CPU";
    case OE_ERR_TYPE: return "an ELF file that is not an object, program or shared object";
    case OE_ERR_READ: return "the ELF file is cut short";
    case OE_ERR_NOMEM: return "not enough memory";
    case OE_ERR_FORMAT: return "the ELF file's headers are damaged";
    case OE_ERR_RELOC: return "a relocation elf.library does not apply (build without -fpic)";
    case OE_ERR_RANGE: return "a 16- or 8-bit relocation out of range";
    case OE_ERR_UNDEF: return "a symbol nothing defines";
    case OE_ERR_FIXED: return "an executable with no relocations kept (link with -q or -pie)";
    case OE_ERR_INTERP: return "a Linux or BSD program (it wants an interpreter)";
    case OE_ERR_TLS: return "thread-local storage is not supported";
    case OE_ERR_NEEDED: return "a shared object it needs could not be loaded";
    }
    return "unknown error";
}

int oe_identify(const void *buf, oe_u32 len, struct oe_info *info)
{
    const oe_u8 *b = (const oe_u8 *)buf;
    mclear(info, sizeof *info);
    if (len < 16 || b[0] != 0x7f || b[1] != 'E' || b[2] != 'L' || b[3] != 'F')
        return OE_ERR_NOT_ELF;
    info->is_elf = 1;
    info->elf_class = b[4];
    info->data = b[5];
    info->osabi = b[7];
    if (len < 20)
        return OE_ERR_CLASS;
    /* e_type and e_machine sit at the same place in every class, in the
     * file's own byte order: say which CPU even when we can't load it */
    info->type = (int)(b[5] == 1 ? (oe_u32)(b[16] | (b[17] << 8)) : g16(b + 16));
    info->machine = (int)(b[5] == 1 ? (oe_u32)(b[18] | (b[19] << 8)) : g16(b + 18));
    if (info->machine != EM_68K && (b[5] == 1 || b[5] == 2))
        return OE_ERR_MACHINE;
    if (b[4] != 1 || b[5] != 2 || b[6] != 1 || len < EHDR_SIZE)
        return OE_ERR_CLASS;
    info->entry = g32(b + 24);
    if (info->type != ET_REL && info->type != ET_EXEC && info->type != ET_DYN)
        return OE_ERR_TYPE;
    return OE_OK;
}

/* ---- One load's state --------------------------------------------------- */

struct ld {
    struct oe_env *env;
    struct oe_image *img;
    oe_u8 eh[EHDR_SIZE];
    oe_u32 shnum, phnum;
    oe_u8 *sh;                  /* the section headers, shnum * 40 */
    oe_u32 *secaddr;            /* ET_REL: each section's address, 0 when not loaded */
    oe_u8 *sym;                 /* ET_REL, ET_EXEC: the symbol table */
    oe_u32 nsym, symsec;
    char *str;                  /* and its strings */
    oe_u32 strsize;
    oe_u32 common;              /* ET_REL: where the COMMON symbols went */
    oe_u32 blo, bsize;          /* ET_EXEC, ET_DYN: the block's link address and size */
    /* a program's constructors (ctor) and destructors (dtor), in order */
    struct fnarr { oe_u32 at, n, key; } ctor[MAX_CTOR], dtor[MAX_CTOR];
    oe_u32 nctor, ndtor;
};

/* len bytes at link address va lie inside the loaded block */
static int inblock(const struct ld *l, oe_u32 va, oe_u32 len)
{
    return va >= l->blo && len <= l->bsize && va - l->blo <= l->bsize - len;
}

static void *talloc(struct ld *l, oe_u32 n) { return l->env->alloc(l->env, n ? n : 4, OE_MEM_TEMP); }
static void tfree(struct ld *l, void *p, oe_u32 n) { if (p) l->env->free(l->env, p, n ? n : 4, OE_MEM_TEMP); }

static int rd(struct ld *l, oe_u32 off, void *buf, oe_u32 len)
{
    if (len == 0) return OE_OK;
    if (off + len < off) return OE_ERR_FORMAT;
    return l->env->read(l->env, off, buf, len) == 0 ? OE_OK : OE_ERR_READ;
}

static const oe_u8 *shdr(struct ld *l, oe_u32 i) { return l->sh + i * SHDR_SIZE; }
#define SH_NAME(s) g32((s) + 0)
#define SH_TYPE(s) g32((s) + 4)
#define SH_FLAGS(s) g32((s) + 8)
#define SH_ADDR(s) g32((s) + 12)
#define SH_OFFSET(s) g32((s) + 16)
#define SH_SIZE(s) g32((s) + 20)
#define SH_LINK(s) g32((s) + 24)
#define SH_INFO(s) g32((s) + 28)
#define SH_ALIGN(s) g32((s) + 32)
#define SH_ENTSIZE(s) g32((s) + 36)

#define ST_NAME(s) g32((s) + 0)
#define ST_VALUE(s) g32((s) + 4)
#define ST_SIZE(s) g32((s) + 8)
#define ST_BIND(s) ((s)[12] >> 4)
#define ST_TYPE(s) ((s)[12] & 15)
#define ST_SHNDX(s) g16((s) + 14)

/* A new segment of size content bytes aligned to al (a power of two up to
 * MAX_ALIGN): its content's address, or 0. The allocation is only taken to
 * be 4-aligned, so above 4 it grows by al - 4 and the contents start where
 * the alignment falls. */
static oe_u8 *newseg(struct ld *l, oe_u32 size, oe_u32 al)
{
    struct oe_image *img = l->img;
    oe_u8 *m;
    if (al < 4) al = 4;
    if (img->nseg == img->segcap) {
        oe_u32 n = img->segcap ? img->segcap * 2 : 8;
        void **a = (void **)talloc(l, n * sizeof(void *));
        if (!a) return 0;
        if (img->seg) {
            mcopy(a, img->seg, img->nseg * sizeof(void *));
            tfree(l, img->seg, img->segcap * sizeof(void *));
        }
        img->seg = a;
        img->segcap = n;
    }
    if (size > 0x7fff0000UL) return 0;
    size = (size + 8 + (al - 4) + 7) & ~7UL;
    m = (oe_u8 *)l->env->alloc(l->env, size, OE_MEM_SEGMENT);
    if (!m) return 0;
    p32(m, size);
    img->seg[img->nseg++] = m;
    return PTR((ADDR(m) + 8 + al - 1) & ~(al - 1));
}

/* addr is code or data in one of the image's segments */
static int inimage(const struct oe_image *img, oe_u32 addr)
{
    oe_u32 i;
    for (i = 0; i < img->nseg; i++) {
        oe_u32 b = ADDR(img->seg[i]) + 8, n = g32((oe_u8 *)img->seg[i]) - 8;
        if (addr >= b && addr - b < n) return 1;
    }
    return 0;
}

void oe_unload(struct oe_env *env, struct oe_image *img)
{
    oe_u32 i;
    for (i = 0; i < img->nseg; i++)
        env->free(env, img->seg[i], g32((oe_u8 *)img->seg[i]), OE_MEM_SEGMENT);
    if (img->seg) env->free(env, img->seg, img->segcap * sizeof(void *), OE_MEM_TEMP);
    img->seg = 0;
    img->nseg = 0;
    img->segcap = 0;
}

/* The relocation types apply() knows: anything else is refused before its
 * symbol is looked up (a -fpic object's _GLOBAL_OFFSET_TABLE_ is the
 * relocation's fault, not a missing symbol's) */
static int known(struct ld *l, oe_u32 type)
{
    if (type <= R_68K_PC8 || (type >= R_68K_PLT32 && type <= R_68K_PLT8)) return OE_OK;
    l->img->detail = type;
    return type >= R_68K_TLS_FIRST && type <= R_68K_TLS_LAST ? OE_ERR_TLS : OE_ERR_RELOC;
}

/* The bytes a relocation type changes */
static oe_u32 relwidth(oe_u32 rt)
{
    if (rt == R_68K_NONE) return 0;
    if (rt == R_68K_8 || rt == R_68K_PC8 || rt == R_68K_PLT8) return 1;
    if (rt == R_68K_16 || rt == R_68K_PC16 || rt == R_68K_PLT16) return 2;
    return 4;
}

/* Apply one relocation at P: S + A for the absolute kinds, minus P for the
 * PC-relative ones. */
static int apply(struct ld *l, oe_u32 type, oe_u32 P, oe_u32 S, oe_u32 A)
{
    oe_u8 *p = PTR(P);
    oe_u32 v;
    long sv;
    switch (type) {
    case R_68K_NONE:
        return OE_OK;
    case R_68K_32:
        p32(p, M32(S + A));
        return OE_OK;
    case R_68K_PC32: case R_68K_PLT32:
        p32(p, M32(S + A - P));
        return OE_OK;
    case R_68K_16: case R_68K_8:
        v = M32(S + A);
        sv = (long)(v & 0x80000000UL ? -(long)(M32(~v) + 1) : (long)v);
        if (type == R_68K_16 ? (sv < -32768 || sv > 65535) : (sv < -128 || sv > 255))
            break;
        if (type == R_68K_16) p16(p, v); else p[0] = (oe_u8)v;
        return OE_OK;
    case R_68K_PC16: case R_68K_PC8: case R_68K_PLT16: case R_68K_PLT8:
        v = M32(S + A - P);
        sv = (long)(v & 0x80000000UL ? -(long)(M32(~v) + 1) : (long)v);
        if ((type == R_68K_PC16 || type == R_68K_PLT16) ? (sv < -32768 || sv > 32767) : (sv < -128 || sv > 127))
            break;
        if (type == R_68K_PC16 || type == R_68K_PLT16) p16(p, v); else p[0] = (oe_u8)v;
        return OE_OK;
    default:
        l->img->detail = type;
        return type >= R_68K_TLS_FIRST && type <= R_68K_TLS_LAST ? OE_ERR_TLS : OE_ERR_RELOC;
    }
    l->img->detail = type;
    return OE_ERR_RANGE;
}

/* The ELF header and the section headers, which every kind starts with. */
static int read_headers(struct ld *l)
{
    struct oe_info info;
    int e;
    if ((e = rd(l, 0, l->eh, EHDR_SIZE)) != OE_OK)
        return e == OE_ERR_READ ? OE_ERR_NOT_ELF : e;
    if ((e = oe_identify(l->eh, EHDR_SIZE, &info)) != OE_OK)
        return e;
    l->img->type = info.type;
    l->shnum = g16(l->eh + 48);
    l->phnum = g16(l->eh + 44);
    if (g32(l->eh + 32) && g16(l->eh + 46) != SHDR_SIZE)
        return OE_ERR_FORMAT;
    if (l->phnum && g16(l->eh + 42) != PHDR_SIZE)
        return OE_ERR_FORMAT;
    if (g32(l->eh + 32) == 0)
        l->shnum = 0;
    else if (l->shnum == 0) {   /* more than 65279: the count is in section 0 */
        oe_u8 s0[SHDR_SIZE];
        if ((e = rd(l, g32(l->eh + 32), s0, SHDR_SIZE)) != OE_OK) return e;
        l->shnum = SH_SIZE(s0);
    }
    if (l->shnum) {
        if (l->shnum > 0x100000UL) return OE_ERR_FORMAT;
        if (!(l->sh = (oe_u8 *)talloc(l, l->shnum * SHDR_SIZE))) return OE_ERR_NOMEM;
        if ((e = rd(l, g32(l->eh + 32), l->sh, l->shnum * SHDR_SIZE)) != OE_OK) return e;
    }
    return OE_OK;
}

/* The symbol table section i and its strings, into l->sym and l->str. */
static int read_symtab(struct ld *l, oe_u32 i)
{
    const oe_u8 *s = shdr(l, i), *t;
    int e;
    if (SH_LINK(s) >= l->shnum || SH_SIZE(s) % SYM_SIZE) return OE_ERR_FORMAT;
    t = shdr(l, SH_LINK(s));
    l->symsec = i;
    l->nsym = SH_SIZE(s) / SYM_SIZE;
    l->strsize = SH_SIZE(t);
    if (!(l->sym = (oe_u8 *)talloc(l, SH_SIZE(s)))) return OE_ERR_NOMEM;
    if (!(l->str = (char *)talloc(l, l->strsize + 1))) return OE_ERR_NOMEM;
    if ((e = rd(l, SH_OFFSET(s), l->sym, SH_SIZE(s))) != OE_OK) return e;
    if ((e = rd(l, SH_OFFSET(t), l->str, l->strsize)) != OE_OK) return e;
    l->str[l->strsize] = 0;
    return OE_OK;
}

static const char *symname(struct ld *l, const oe_u8 *s)
{
    oe_u32 n = ST_NAME(s);
    return n < l->strsize ? l->str + n : "";
}

/* Ask env->resolve for an undefined symbol. */
static int resolve(struct ld *l, const char *name, int weak, oe_u32 *val)
{
    int found = 0;
    *val = l->env->resolve ? l->env->resolve(l->env, name, &found) : 0;
    if (found) return OE_OK;
    *val = 0;
    if (weak) return OE_OK;
    setname(l->img, name);
    return OE_ERR_UNDEF;
}

/* ---- ET_REL: sections into segments, relocated ------------------------- */

/* Symbol i's address in an object, resolving it once if it is undefined
 * (the answer is written back as an absolute symbol). */
static int rel_symval(struct ld *l, oe_u32 i, oe_u32 *val)
{
    oe_u8 *s;
    oe_u32 shndx;
    int e;
    if (i >= l->nsym) return OE_ERR_FORMAT;
    s = l->sym + i * SYM_SIZE;
    shndx = ST_SHNDX(s);
    if (ST_TYPE(s) == STT_TLS) return OE_ERR_TLS;
    if (shndx == SHN_UNDEF) {
        if (i == 0) { *val = 0; return OE_OK; }
        if ((e = resolve(l, symname(l, s), ST_BIND(s) == STB_WEAK, val)) != OE_OK) return e;
        p32(s + 4, *val);
        p16(s + 14, SHN_ABS);
        return OE_OK;
    }
    if (shndx == SHN_ABS) { *val = ST_VALUE(s); return OE_OK; }
    if (shndx == SHN_COMMON) { *val = l->common + ST_VALUE(s); return OE_OK; }
    if (shndx >= SHN_LORESERVE || shndx >= l->shnum) return OE_ERR_FORMAT;
    if (!l->secaddr[shndx]) {
        /* a section we didn't load: only debug data points there */
        l->img->detail = 0;
        setname(l->img, symname(l, s));
        return OE_ERR_UNDEF;
    }
    *val = M32(l->secaddr[shndx] + ST_VALUE(s));
    return OE_OK;
}

static int rel_relocate(struct ld *l, oe_u32 ri)
{
    const oe_u8 *r = shdr(l, ri);
    oe_u32 type = SH_TYPE(r), ent = type == SHT_RELA ? RELA_SIZE : REL_SIZE;
    oe_u32 target = SH_INFO(r), size, off, n, k;
    oe_u8 buf[RELOC_CHUNK * RELA_SIZE];
    int e;
    if (target >= l->shnum || SH_LINK(r) != l->symsec) return OE_ERR_FORMAT;
    if (!l->secaddr[target]) return OE_OK;          /* debug sections' relocations */
    size = SH_SIZE(shdr(l, target));
    if (SH_SIZE(r) % ent) return OE_ERR_FORMAT;
    for (off = 0; off < SH_SIZE(r); off += n * ent) {
        n = (SH_SIZE(r) - off) / ent;
        if (n > RELOC_CHUNK) n = RELOC_CHUNK;
        if ((e = rd(l, SH_OFFSET(r) + off, buf, n * ent)) != OE_OK) return e;
        for (k = 0; k < n; k++) {
            const oe_u8 *q = buf + k * ent;
            oe_u32 where = g32(q), info = g32(q + 4), S, A, P, rt = info & 0xff;
            if (where > size || size - where < relwidth(rt))
                return OE_ERR_FORMAT;
            P = M32(l->secaddr[target] + where);
            if (rt == R_68K_NONE) continue;
            if ((e = known(l, rt)) != OE_OK) return e;
            if ((e = rel_symval(l, info >> 8, &S)) != OE_OK) return e;
            /* SHT_REL keeps the addend at the place, as wide as the relocation */
            if (ent == RELA_SIZE) A = g32(q + 8);
            else if (relwidth(rt) == 4) A = g32(PTR(P));
            else if (relwidth(rt) == 2) A = M32((g16(PTR(P)) ^ 0x8000UL) - 0x8000UL);
            else A = M32((PTR(P)[0] ^ 0x80UL) - 0x80UL);
            if ((e = apply(l, rt, P, S, A)) != OE_OK) return e;
        }
    }
    return OE_OK;
}

static int load_rel(struct ld *l)
{
    struct oe_image *img = l->img;
    oe_u32 i, symtab = 0, seg_used = 0, seg_size = 0, first = 0, commsize = 0, commalign = 4;
    oe_u32 *off;                /* per index: a section's offset in its segment; a segment's size and alignment */
    int e;
    oe_u8 *base = 0;
    for (i = 1; i < l->shnum; i++)
        if (SH_TYPE(shdr(l, i)) == SHT_SYMTAB) { if (symtab) return OE_ERR_FORMAT; symtab = i; }
    if (!symtab) return OE_ERR_FORMAT;
    if ((e = read_symtab(l, symtab)) != OE_OK) return e;

    /* COMMON symbols: laid out together, after the sections */
    for (i = 1; i < l->nsym; i++) {
        oe_u8 *s = l->sym + i * SYM_SIZE;
        if (ST_SHNDX(s) == SHN_COMMON) {
            oe_u32 a = ST_VALUE(s) ? ST_VALUE(s) : 1;
            if ((a & (a - 1)) || a > MAX_ALIGN) return OE_ERR_FORMAT;
            commsize = (commsize + a - 1) & ~(a - 1);
            if (a > commalign) commalign = a;
            p32(s + 4, commsize);
            commsize += ST_SIZE(s);
        }
    }

    if (!(l->secaddr = (oe_u32 *)talloc(l, (l->shnum + 1) * sizeof(oe_u32)))) return OE_ERR_NOMEM;
    if (!(off = (oe_u32 *)talloc(l, (l->shnum + 1) * 3 * sizeof(oe_u32)))) return OE_ERR_NOMEM;

    /* Pass 1: which segment each section goes in. Section shnum stands for
     * the COMMON block. */
    for (i = 1; i <= l->shnum; i++) {
        oe_u32 sz, al;
        if (i < l->shnum) {
            const oe_u8 *s = shdr(l, i);
            if (!(SH_FLAGS(s) & SHF_ALLOC) || SH_SIZE(s) == 0) continue;
            sz = SH_SIZE(s);
            al = SH_ALIGN(s) ? SH_ALIGN(s) : 1;
        } else {
            if (!commsize) continue;
            sz = commsize;
            al = commalign;
        }
        if ((al & (al - 1)) || al > MAX_ALIGN) { tfree(l, off, (l->shnum + 1) * 3 * sizeof(oe_u32)); return OE_ERR_FORMAT; }
        /* seg_size + al + sz > seg_max, without wrapping at 32 bits */
        if (seg_size && l->env->seg_max
            && (al > l->env->seg_max || sz > l->env->seg_max - al || seg_size > l->env->seg_max - al - sz)) {
            off[3 * seg_used + 1] = seg_size;
            seg_used++;
            seg_size = 0;
        }
        if (al > off[3 * seg_used + 2]) off[3 * seg_used + 2] = al;
        /* the round-up and the add must not wrap past 32 bits either */
        if (seg_size > 0xffffffffUL - (al - 1)) { tfree(l, off, (l->shnum + 1) * 3 * sizeof(oe_u32)); return OE_ERR_FORMAT; }
        seg_size = (seg_size + al - 1) & ~(al - 1);
        if (sz > 0xffffffffUL - seg_size) { tfree(l, off, (l->shnum + 1) * 3 * sizeof(oe_u32)); return OE_ERR_FORMAT; }
        off[3 * i] = seg_size;
        l->secaddr[i] = seg_used + 1;           /* its segment, 1-based, for now */
        seg_size += sz;
    }
    if (seg_size) off[3 * seg_used++ + 1] = seg_size;
    if (!seg_used) { tfree(l, off, (l->shnum + 1) * 3 * sizeof(oe_u32)); return OE_ERR_FORMAT; }

    /* Pass 2: the segments, then each section's address */
    for (i = 0; i < seg_used; i++) {
        /* where each segment's contents start, kept in its size's place */
        oe_u8 *m = newseg(l, off[3 * i + 1], off[3 * i + 2]);
        if (!m) { tfree(l, off, (l->shnum + 1) * 3 * sizeof(oe_u32)); return OE_ERR_NOMEM; }
        off[3 * i + 1] = ADDR(m);
        if (!base) base = m;
    }
    for (i = 1; i <= l->shnum; i++) {
        if (!l->secaddr[i]) continue;
        l->secaddr[i] = off[3 * (l->secaddr[i] - 1) + 1] + off[3 * i];
    }
    l->common = l->secaddr[l->shnum];
    tfree(l, off, (l->shnum + 1) * 3 * sizeof(oe_u32));

    /* Pass 3: contents (NOBITS stay cleared) */
    for (i = 1; i < l->shnum; i++) {
        const oe_u8 *s = shdr(l, i);
        if (!l->secaddr[i] || SH_TYPE(s) == SHT_NOBITS) continue;
        if ((e = rd(l, SH_OFFSET(s), PTR(l->secaddr[i]), SH_SIZE(s))) != OE_OK) return e;
    }

    /* Pass 4: relocations */
    for (i = 1; i < l->shnum; i++) {
        oe_u32 t = SH_TYPE(shdr(l, i));
        if ((t == SHT_RELA || t == SHT_REL) && (e = rel_relocate(l, i)) != OE_OK) return e;
    }

    /* The entry: _start if the object defines it, else its first code */
    for (i = 1; i < l->nsym; i++) {
        oe_u8 *s = l->sym + i * SYM_SIZE;
        if (ST_BIND(s) != STB_LOCAL && ST_SHNDX(s) != SHN_UNDEF && streq(symname(l, s), "_start")) {
            if ((e = rel_symval(l, i, &img->entry)) != OE_OK) return e;
            break;
        }
    }
    for (i = 1; !img->entry && i < l->shnum; i++)
        if (l->secaddr[i] && (SH_FLAGS(shdr(l, i)) & SHF_EXECINSTR)) first = i, img->entry = l->secaddr[first];

    /* Constructor arrays, for a caller that runs them (objects have no
     * DT_INIT_ARRAY; the first of each kind is kept) */
    for (i = 1; i < l->shnum; i++) {
        const oe_u8 *s = shdr(l, i);
        oe_u32 t = SH_TYPE(s);
        if (!l->secaddr[i]) continue;
        if (t == SHT_INIT_ARRAY && !img->init_array) img->init_array = l->secaddr[i], img->init_count = SH_SIZE(s) / 4;
        if (t == SHT_FINI_ARRAY && !img->fini_array) img->fini_array = l->secaddr[i], img->fini_count = SH_SIZE(s) / 4;
        if (t == SHT_PREINIT_ARRAY && !img->preinit_array) img->preinit_array = l->secaddr[i], img->preinit_count = SH_SIZE(s) / 4;
    }
    return OE_OK;
}

/* ---- ET_EXEC and ET_DYN: one block from the program headers ------------- */

static int load_block(struct ld *l, oe_u8 **phbuf, oe_u32 *lo_out)
{
    struct oe_image *img = l->img;
    oe_u32 i, lo = 0xffffffffUL, hi = 0, nload = 0, al = 4, entry = g32(l->eh + 24);
    oe_u8 *ph, *base;
    int e, entry_ok = 0;
    if (!l->phnum || l->phnum > 1024) return OE_ERR_FORMAT;
    if (!(ph = (oe_u8 *)talloc(l, l->phnum * PHDR_SIZE))) return OE_ERR_NOMEM;
    *phbuf = ph;
    if ((e = rd(l, g32(l->eh + 28), ph, l->phnum * PHDR_SIZE)) != OE_OK) return e;
    for (i = 0; i < l->phnum; i++) {
        const oe_u8 *p = ph + i * PHDR_SIZE;
        oe_u32 t = g32(p), va = g32(p + 8), fsz = g32(p + 16), msz = g32(p + 20);
        if (t == PT_INTERP) return OE_ERR_INTERP;
        if (t == PT_TLS) return OE_ERR_TLS;
        if (t != PT_LOAD || msz == 0) continue;
        if (fsz > msz || va + msz < va) return OE_ERR_FORMAT;
        if ((g32(p + 24) & PF_X) && entry >= va && entry - va < fsz) entry_ok = 1;
        if (va < lo) lo = va;
        if (va + msz > hi) hi = va + msz;
        nload++;
    }
    if (!nload) return OE_ERR_FORMAT;
    /* a program starts in code it loaded (a shared object may have no entry) */
    if (!entry_ok && (entry || img->type == ET_EXEC)) return OE_ERR_FORMAT;
    /* the block keeps the alignment its sections ask: the bias is a multiple of it */
    for (i = 1; i < l->shnum; i++) {
        const oe_u8 *s = shdr(l, i);
        oe_u32 a = SH_ALIGN(s);
        if (!(SH_FLAGS(s) & SHF_ALLOC) || a <= al) continue;
        if ((a & (a - 1)) || a > MAX_ALIGN) return OE_ERR_FORMAT;
        al = a;
    }
    lo &= ~(al - 1);
    if (hi - lo > 0x7fffffffUL) return OE_ERR_FORMAT;
    if (!(base = newseg(l, hi - lo, al))) return OE_ERR_NOMEM;
    for (i = 0; i < l->phnum; i++) {
        const oe_u8 *p = ph + i * PHDR_SIZE;
        if (g32(p) != PT_LOAD || g32(p + 20) == 0) continue;
        if ((e = rd(l, g32(p + 4), base + (g32(p + 8) - lo), g32(p + 16))) != OE_OK) return e;
    }
    img->bias = M32(ADDR(base) - lo);
    l->blo = lo;
    l->bsize = hi - lo;
    *lo_out = lo;
    return OE_OK;
}

/* ET_EXEC: the absolute relocations ld -q kept, moved by the bias. The
 * block moved as a whole, so PC-relative ones stand as they are. */
static int load_exec(struct ld *l)
{
    struct oe_image *img = l->img;
    oe_u8 *ph = 0;
    oe_u32 lo, i, nrel = 0, cursym = 0;
    oe_u8 buf[RELOC_CHUNK * RELA_SIZE];
    int e = load_block(l, &ph, &lo);
    if (ph) tfree(l, ph, l->phnum * PHDR_SIZE);
    if (e != OE_OK) return e;
    img->entry = M32(g32(l->eh + 24) + img->bias);
    for (i = 1; i < l->shnum; i++) {
        const oe_u8 *r = shdr(l, i), *t;
        oe_u32 ent, off, n, k, tlo, thi;
        if (SH_TYPE(r) != SHT_RELA && SH_TYPE(r) != SHT_REL) continue;
        ent = SH_TYPE(r) == SHT_RELA ? RELA_SIZE : REL_SIZE;
        if (SH_INFO(r) == 0 || SH_INFO(r) >= l->shnum || SH_SIZE(r) % ent) return OE_ERR_FORMAT;
        t = shdr(l, SH_INFO(r));
        if (!(SH_FLAGS(t) & SHF_ALLOC)) continue;
        tlo = SH_ADDR(t);
        thi = tlo + SH_SIZE(t);
        if (SH_LINK(r) != cursym) {
            if (l->sym) { tfree(l, l->sym, l->nsym * SYM_SIZE); tfree(l, l->str, l->strsize + 1); l->sym = 0; l->str = 0; }
            if (SH_LINK(r) == 0 || SH_LINK(r) >= l->shnum) return OE_ERR_FORMAT;
            if ((e = read_symtab(l, SH_LINK(r))) != OE_OK) return e;
            cursym = SH_LINK(r);
        }
        for (off = 0; off < SH_SIZE(r); off += n * ent) {
            n = (SH_SIZE(r) - off) / ent;
            if (n > RELOC_CHUNK) n = RELOC_CHUNK;
            if ((e = rd(l, SH_OFFSET(r) + off, buf, n * ent)) != OE_OK) return e;
            for (k = 0; k < n; k++) {
                const oe_u8 *q = buf + k * ent, *s;
                oe_u32 where = g32(q), info = g32(q + 4), rt = info & 0xff, si = info >> 8, shndx;
                nrel++;
                if (rt == R_68K_NONE || rt == R_68K_PC32 || rt == R_68K_PC16 || rt == R_68K_PC8
                    || rt == R_68K_PLT32 || rt == R_68K_PLT16 || rt == R_68K_PLT8)
                    continue;
                if (si >= l->nsym) return OE_ERR_FORMAT;
                s = l->sym + si * SYM_SIZE;
                shndx = ST_SHNDX(s);
                /* absolute symbols, and weak ones nobody defined, don't move;
                 * a strong one nobody defined is a hole in the program */
                if (shndx == SHN_UNDEF && si != 0 && ST_BIND(s) != STB_WEAK) {
                    setname(img, symname(l, s));
                    return OE_ERR_UNDEF;
                }
                if (shndx == SHN_ABS || (shndx == SHN_UNDEF && si != 0)) continue;
                if (ST_TYPE(s) == STT_TLS) return OE_ERR_TLS;
                if (where < tlo || where + 4 > thi || !inblock(l, where, 4)) return OE_ERR_FORMAT;
                if (rt == R_68K_32) {
                    oe_u8 *p = PTR(M32(where + img->bias));
                    p32(p, M32(g32(p) + img->bias));
                } else if (rt == R_68K_16 || rt == R_68K_8) {
                    if (img->bias) { img->detail = rt; return OE_ERR_RANGE; }
                } else {
                    img->detail = rt;
                    return rt >= R_68K_TLS_FIRST && rt <= R_68K_TLS_LAST ? OE_ERR_TLS : OE_ERR_RELOC;
                }
            }
        }
    }
    if (!nrel && img->bias) return OE_ERR_FIXED;
    (void)lo;
    return OE_OK;
}

/* ET_DYN: the dynamic section's relocations */
struct dyn {
    oe_u32 rela, relasz, jmprel, pltrelsz, symtab, strtab, strsz, hash, gnuhash;
};

/* How many dynamic symbols there are, from either hash table; 0 when the
 * tables don't fit in the block */
static oe_u32 dyn_nsym(const struct ld *l, const struct dyn *d)
{
    oe_u32 bias = l->img->bias, n = 0;
    if (d->hash) {
        if (!inblock(l, d->hash, 8)) return 0;
        n = g32(PTR(M32(d->hash + bias + 4)));     /* nchain */
        if (!inblock(l, d->hash, 8 + (g32(PTR(M32(d->hash + bias))) + n) * 4)) return 0;
    } else if (d->gnuhash) {
        /* the GNU hash: the highest symbol any chain reaches, plus one */
        const oe_u8 *h = PTR(M32(d->gnuhash + bias)), *buckets;
        oe_u32 nb, symoff, bloom, i, top = 0;
        if (!inblock(l, d->gnuhash, 16)) return 0;
        nb = g32(h); symoff = g32(h + 4); bloom = g32(h + 8);
        if (nb > l->bsize / 4 || bloom > l->bsize / 4 || !inblock(l, d->gnuhash, 16 + (bloom + nb) * 4)) return 0;
        buckets = h + 16 + bloom * 4;
        for (i = 0; i < nb; i++) if (g32(buckets + i * 4) > top) top = g32(buckets + i * 4);
        if (top < symoff) n = symoff;
        else {
            for (;;) {
                oe_u32 ca = d->gnuhash + 16 + (bloom + nb + top - symoff) * 4;
                if (!inblock(l, ca, 4)) return 0;
                if (g32(PTR(M32(ca + bias))) & 1) break;
                top++;
            }
            n = top + 1;
        }
    }
    if (n > l->bsize / SYM_SIZE || !inblock(l, d->symtab, n * SYM_SIZE)) return 0;
    return n;
}

/* A string in the dynamic string table, or "" when it lies outside (the
 * table was checked to end in a NUL, so every string in it ends there) */
static const char *dyn_str(const struct ld *l, const struct dyn *d, oe_u32 off)
{
    if (off >= d->strsz) return "";
    return (const char *)PTR(M32(d->strtab + off + l->img->bias));
}

static int dyn_symval(struct ld *l, const struct dyn *d, oe_u32 si, oe_u32 *val, oe_u32 *size)
{
    struct oe_image *img = l->img;
    const oe_u8 *s;
    if (si == 0) { *val = 0; return OE_OK; }
    if (si >= img->nsym) return OE_ERR_FORMAT;
    s = PTR(M32(d->symtab + img->bias)) + si * SYM_SIZE;
    if (ST_TYPE(s) == STT_TLS) return OE_ERR_TLS;
    if (size) *size = ST_SIZE(s);
    if (ST_SHNDX(s) == SHN_ABS) { *val = ST_VALUE(s); return OE_OK; }
    if (ST_SHNDX(s) != SHN_UNDEF) { *val = M32(ST_VALUE(s) + img->bias); return OE_OK; }
    return resolve(l, dyn_str(l, d, ST_NAME(s)), ST_BIND(s) == STB_WEAK, val);
}

static int dyn_relocs(struct ld *l, const struct dyn *d, oe_u32 at, oe_u32 sz)
{
    struct oe_image *img = l->img;
    const oe_u8 *r = PTR(M32(at + img->bias));
    oe_u32 k;
    int e;
    if (sz % RELA_SIZE || !inblock(l, at, sz)) return OE_ERR_FORMAT;
    for (k = 0; k < sz / RELA_SIZE; k++, r += RELA_SIZE) {
        oe_u32 P = M32(g32(r) + img->bias), info = g32(r + 4), A = g32(r + 8), rt = info & 0xff, S, size = 0;
        if (rt == R_68K_NONE) continue;
        if (!inblock(l, g32(r), 4)) return OE_ERR_FORMAT;
        if (rt == R_68K_RELATIVE) { p32(PTR(P), M32(img->bias + A)); continue; }
        if (rt == R_68K_COPY) {
            /* the symbol is defined here, at P: its contents come from the
             * shared object that also defines it */
            oe_u32 si = info >> 8;
            const oe_u8 *s;
            if (si == 0 || si >= img->nsym) return OE_ERR_FORMAT;
            s = PTR(M32(d->symtab + img->bias)) + si * SYM_SIZE;
            size = ST_SIZE(s);
            if (!inblock(l, g32(r), size)) return OE_ERR_FORMAT;
            if ((e = resolve(l, dyn_str(l, d, ST_NAME(s)), 0, &S)) != OE_OK) return e;
            mcopy(PTR(P), PTR(S), size);
            continue;
        }
        if ((e = dyn_symval(l, d, info >> 8, &S, &size)) != OE_OK) return e;
        if (rt == R_68K_GLOB_DAT || rt == R_68K_JMP_SLOT) { p32(PTR(P), M32(S + A)); continue; }
        if ((e = apply(l, rt, P, S, A)) != OE_OK) return e;
    }
    return OE_OK;
}

static int load_dyn(struct ld *l)
{
    struct oe_image *img = l->img;
    struct dyn d;
    oe_u8 *ph = 0;
    oe_u32 lo, i, dynva = 0, dynsz = 0;
    const oe_u8 *dp;
    int e = load_block(l, &ph, &lo);
    if (e == OE_OK) {
        for (i = 0; i < l->phnum; i++) {
            const oe_u8 *p = ph + i * PHDR_SIZE;
            if (g32(p) == PT_DYNAMIC) dynva = g32(p + 8), dynsz = g32(p + 20);
        }
    }
    if (ph) tfree(l, ph, l->phnum * PHDR_SIZE);
    if (e != OE_OK) return e;
    img->entry = g32(l->eh + 24) ? M32(g32(l->eh + 24) + img->bias) : 0;
    if (!dynva) return OE_OK;   /* a PIE with nothing to relocate */

    if (!inblock(l, dynva, dynsz)) return OE_ERR_FORMAT;
    mclear(&d, sizeof d);
    dp = PTR(M32(dynva + img->bias));
    for (i = 0; i + 8 <= dynsz && g32(dp + i) != DT_NULL; i += 8) {
        oe_u32 tag = g32(dp + i), v = g32(dp + i + 4);
        switch (tag) {
        case DT_RELA: d.rela = v; break;
        case DT_RELASZ: d.relasz = v; break;
        case DT_RELAENT: if (v != RELA_SIZE) return OE_ERR_FORMAT; break;
        case DT_JMPREL: d.jmprel = v; break;
        case DT_PLTRELSZ: d.pltrelsz = v; break;
        case DT_PLTREL: if (v != DT_RELA) return OE_ERR_FORMAT; break;
        case DT_SYMTAB: d.symtab = v; break;
        case DT_STRTAB: d.strtab = v; break;
        case DT_STRSZ: d.strsz = v; break;
        case DT_HASH: d.hash = v; break;
        case DT_GNU_HASH: d.gnuhash = v; break;
        case DT_INIT: img->init = M32(v + img->bias); break;
        case DT_FINI: img->fini = M32(v + img->bias); break;
        case DT_INIT_ARRAY: img->init_array = M32(v + img->bias); break;
        case DT_INIT_ARRAYSZ: img->init_count = v / 4; break;
        case DT_FINI_ARRAY: img->fini_array = M32(v + img->bias); break;
        case DT_FINI_ARRAYSZ: img->fini_count = v / 4; break;
        case DT_PREINIT_ARRAY: img->preinit_array = M32(v + img->bias); break;
        case DT_PREINIT_ARRAYSZ: img->preinit_count = v / 4; break;
        }
    }
    if (d.strtab && (!d.strsz || !inblock(l, d.strtab, d.strsz)
                     || *PTR(M32(d.strtab + d.strsz - 1 + img->bias)) != 0))
        return OE_ERR_FORMAT;
    /* the constructors and destructors lie in the block */
    if (img->init_count > l->bsize / 4 || img->fini_count > l->bsize / 4 || img->preinit_count > l->bsize / 4
        || (img->init && !inblock(l, M32(img->init - img->bias), 2))
        || (img->fini && !inblock(l, M32(img->fini - img->bias), 2))
        || (img->init_count && !inblock(l, M32(img->init_array - img->bias), img->init_count * 4))
        || (img->fini_count && !inblock(l, M32(img->fini_array - img->bias), img->fini_count * 4))
        || (img->preinit_count && !inblock(l, M32(img->preinit_array - img->bias), img->preinit_count * 4)))
        return OE_ERR_FORMAT;
    img->nsym = d.symtab ? dyn_nsym(l, &d) : 0;
    if (d.symtab && !d.strtab) return OE_ERR_FORMAT;
    if (img->nsym) {
        img->dynsym = M32(d.symtab + img->bias);
        img->dynstr = M32(d.strtab + img->bias);
        img->dynstrsz = d.strsz;
        img->hash = d.hash ? M32(d.hash + img->bias) : 0;
    }

    /* what it needs first, so their symbols resolve */
    for (i = 0; i + 8 <= dynsz && g32(dp + i) != DT_NULL; i += 8) {
        if (g32(dp + i) != DT_NEEDED) continue;
        if (!d.strtab) return OE_ERR_FORMAT;
        if (l->env->needed) {
            const char *name = dyn_str(l, &d, g32(dp + i + 4));
            if ((e = l->env->needed(l->env, name)) != OE_OK) { setname(img, name); return OE_ERR_NEEDED; }
        }
    }
    if ((d.relasz || d.pltrelsz) && !d.symtab) return OE_ERR_FORMAT;
    if (d.rela && (e = dyn_relocs(l, &d, d.rela, d.relasz)) != OE_OK) return e;
    if (d.jmprel && (e = dyn_relocs(l, &d, d.jmprel, d.pltrelsz)) != OE_OK) return e;
    return OE_OK;
}

/* ---- A program's start routine ------------------------------------------ */

/* The priority in an array section's name: .init_array.00101 is 101, and
 * a plain .init_array runs after every numbered one */
static oe_u32 secpri(const char *name)
{
    oe_u32 v = 0;
    const char *p;
    if (!name[0]) return 0x10000UL;
    for (p = name + 1; *p && *p != '.'; p++) ;
    if (!*p || !p[1]) return 0x10000UL;
    for (p++; *p; p++) {
        if (*p < '0' || *p > '9' || v > 0xffff) return 0x10000UL;
        v = v * 10 + (oe_u32)(*p - '0');
    }
    return v;
}

/* n function pointers at at, or with n 0 the one function at at, into the
 * constructor or destructor list, kept in key order */
static int addfn(struct ld *l, int dtor, oe_u32 at, oe_u32 n, oe_u32 key)
{
    struct fnarr *a = dtor ? l->dtor : l->ctor;
    oe_u32 *cnt = dtor ? &l->ndtor : &l->nctor, i;
    if (*cnt == MAX_CTOR) return OE_ERR_FORMAT;
    for (i = *cnt; i > 0 && a[i - 1].key > key; i--)
        a[i].at = a[i - 1].at, a[i].n = a[i - 1].n, a[i].key = a[i - 1].key;   /* no memcpy in the library */
    a[i].at = at;
    a[i].n = n;
    a[i].key = key;
    (*cnt)++;
    return OE_OK;
}

/* Constructors come in order preinit, DT_INIT, init; destructors run
 * backwards through fini then DT_FINI. Objects and executables: from their
 * array sections (an object's numbered ones apart, ordered by number). */
static int collect_sections(struct ld *l)
{
    oe_u32 i, sx = g16(l->eh + 50), nsz = 0;
    char *names = 0;
    int e = OE_OK;
    if (sx == SHN_XINDEX && l->shnum) sx = SH_LINK(shdr(l, 0));
    if (sx && sx < l->shnum) {
        nsz = SH_SIZE(shdr(l, sx));
        if (nsz > 0x100000UL) return OE_ERR_FORMAT;
        if (!(names = (char *)talloc(l, nsz + 1))) return OE_ERR_NOMEM;
        e = rd(l, SH_OFFSET(shdr(l, sx)), names, nsz);
    }
    for (i = 1; e == OE_OK && i < l->shnum; i++) {
        const oe_u8 *s = shdr(l, i);
        oe_u32 t = SH_TYPE(s), n = SH_SIZE(s) / 4, at, pri;
        if ((t != SHT_INIT_ARRAY && t != SHT_FINI_ARRAY && t != SHT_PREINIT_ARRAY) || !(SH_FLAGS(s) & SHF_ALLOC) || !n)
            continue;
        if (l->img->type == ET_REL) {
            if (!(at = l->secaddr[i])) continue;
        } else {
            if (!inblock(l, SH_ADDR(s), n * 4)) { e = OE_ERR_FORMAT; break; }
            at = M32(SH_ADDR(s) + l->img->bias);
        }
        pri = names && SH_NAME(s) < nsz ? secpri(names + SH_NAME(s)) : 0x10000UL;
        if (t == SHT_PREINIT_ARRAY) e = addfn(l, 0, at, n, pri);
        else if (t == SHT_INIT_ARRAY) e = addfn(l, 0, at, n, 0x20000UL + pri);
        else e = addfn(l, 1, at, n, 1 + pri);
    }
    if (names) tfree(l, names, nsz + 1);
    return e;
}

/* Shared objects and PIEs: from the dynamic section */
static int collect_dyn(struct ld *l)
{
    struct oe_image *img = l->img;
    int e = OE_OK;
    if (img->preinit_count) e = addfn(l, 0, img->preinit_array, img->preinit_count, 0);
    if (e == OE_OK && img->init) e = addfn(l, 0, img->init, 0, 0x18000UL);
    if (e == OE_OK && img->init_count) e = addfn(l, 0, img->init_array, img->init_count, 0x20000UL);
    if (e == OE_OK && img->fini) e = addfn(l, 1, img->fini, 0, 0);
    if (e == OE_OK && img->fini_count) e = addfn(l, 1, img->fini_array, img->fini_count, 1);
    return e;
}

/* A jsr to each function in the list (backwards for destructors), written
 * at *q when it is not 0; *count counts them. 0 and -1 are skipped. */
static int emit(const struct oe_image *img, const struct fnarr *a, oe_u32 na, int back, oe_u8 **q, oe_u32 *count)
{
    oe_u32 k, i;
    for (k = 0; k < na; k++) {
        const struct fnarr *f = &a[back ? na - 1 - k : k];
        oe_u32 n = f->n ? f->n : 1;
        for (i = 0; i < n; i++) {
            oe_u32 fn = f->n ? g32(PTR(f->at) + 4 * (back ? n - 1 - i : i)) : f->at;
            if (fn == 0 || fn == 0xffffffffUL) continue;
            if (!inimage(img, fn)) return OE_ERR_FORMAT;
            if (*q) { p16(*q, 0x4eb9); p32(*q + 2, fn); *q += 6; }   /* jsr fn.l */
            (*count)++;
        }
    }
    return OE_OK;
}

/* A program with constructors or destructors starts in a routine of its
 * own, in a segment of its own: constructors, the entry with D0 and A0 as
 * they came, destructors, and the entry's D0 back. */
static int start_routine(struct ld *l)
{
    struct oe_image *img = l->img;
    oe_u32 nc = 0, nd = 0;
    oe_u8 *q = 0, *w;
    int e = img->type == ET_DYN ? collect_dyn(l) : collect_sections(l);
    if (e == OE_OK) e = emit(img, l->ctor, l->nctor, 0, &q, &nc);
    if (e == OE_OK) e = emit(img, l->dtor, l->ndtor, 1, &q, &nd);
    if (e != OE_OK || nc + nd == 0) return e;
    if (!(w = q = newseg(l, 20 + 6 * (nc + nd), 4))) return OE_ERR_NOMEM;
    p32(q, 0x48e78080UL);                       /* movem.l d0/a0,-(sp) */
    q += 4;
    emit(img, l->ctor, l->nctor, 0, &q, &nc);
    p32(q, 0x4cdf0101UL);                       /* movem.l (sp)+,d0/a0 */
    p16(q + 4, 0x4eb9);                         /* jsr entry.l */
    p32(q + 6, img->entry);
    p16(q + 10, 0x2f00);                        /* move.l d0,-(sp) */
    q += 12;
    emit(img, l->dtor, l->ndtor, 1, &q, &nd);
    p16(q, 0x201f);                             /* move.l (sp)+,d0 */
    p16(q + 2, 0x4e75);                         /* rts */
    img->entry = ADDR(w);
    return OE_OK;
}

/* ---- The entry points --------------------------------------------------- */

static void cleanup(struct ld *l)
{
    if (l->sym) tfree(l, l->sym, l->nsym * SYM_SIZE);
    if (l->str) tfree(l, l->str, l->strsize + 1);
    if (l->secaddr) tfree(l, l->secaddr, (l->shnum + 1) * sizeof(oe_u32));
    if (l->sh) tfree(l, l->sh, l->shnum * SHDR_SIZE);
    l->sym = 0; l->str = 0; l->secaddr = 0; l->sh = 0;
}

static int load_once(struct oe_env *env, struct oe_image *img)
{
    struct ld l;
    int e;
    mclear(&l, sizeof l);
    mclear(img, sizeof *img);
    l.env = env;
    l.img = img;
    e = read_headers(&l);
    if (e == OE_OK) {
        if (img->type == ET_REL) e = load_rel(&l);
        else if (img->type == ET_EXEC) e = load_exec(&l);
        else e = load_dyn(&l);
    }
    if (e == OE_OK && env->program && img->entry) e = start_routine(&l);
    cleanup(&l);
    if (e != OE_OK) {
        oe_u32 detail = img->detail;
        char name[OE_NAME_MAX];
        mcopy(name, img->name, OE_NAME_MAX);
        oe_unload(env, img);
        mclear(img, sizeof *img);
        img->detail = detail;
        mcopy(img->name, name, OE_NAME_MAX);
    }
    return e;
}

int oe_load(struct oe_env *env, struct oe_image *img)
{
    int e = load_once(env, img);
    /* an object whose short branches can't cross between segments loads
     * again as one segment */
    if (e == OE_ERR_RANGE && env->seg_max) {
        oe_u32 keep = env->seg_max;
        env->seg_max = 0;
        e = load_once(env, img);
        env->seg_max = keep;
    }
    return e;
}

oe_u32 oe_lookup(const struct oe_image *img, const char *name)
{
    oe_u32 i;
    if (!img->dynsym || !img->dynstr) return 0;
    if (img->hash) {
        /* the System V hash */
        const oe_u8 *h = PTR(img->hash);
        oe_u32 nb = g32(h), hv = 0, g;
        const oe_u8 *n = (const oe_u8 *)name;
        if (!nb) return 0;
        while (*n) {
            hv = (hv << 4) + *n++;
            g = hv & 0xf0000000UL;
            if (g) hv ^= g >> 24;
            hv &= ~g;
            hv = M32(hv);
        }
        oe_u32 steps = 0;
        for (i = g32(h + 8 + (hv % nb) * 4); i && i < img->nsym && steps++ < img->nsym; i = g32(h + 8 + nb * 4 + i * 4)) {
            const oe_u8 *s = PTR(img->dynsym) + i * SYM_SIZE;
            if (ST_SHNDX(s) != SHN_UNDEF && ST_BIND(s) != STB_LOCAL && ST_NAME(s) < img->dynstrsz
                && streq((const char *)PTR(img->dynstr + ST_NAME(s)), name))
                return ST_SHNDX(s) == SHN_ABS ? ST_VALUE(s) : M32(ST_VALUE(s) + img->bias);
        }
        return 0;
    }
    for (i = 1; i < img->nsym; i++) {
        const oe_u8 *s = PTR(img->dynsym) + i * SYM_SIZE;
        if (ST_SHNDX(s) != SHN_UNDEF && ST_BIND(s) != STB_LOCAL && ST_NAME(s) < img->dynstrsz
            && streq((const char *)PTR(img->dynstr + ST_NAME(s)), name))
            return ST_SHNDX(s) == SHN_ABS ? ST_VALUE(s) : M32(ST_VALUE(s) + img->bias);
    }
    return 0;
}
