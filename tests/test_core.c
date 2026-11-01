/* OpenELF's core under qemu-m68k: loads the fixtures tests/test_openelf.sh
 * builds with GCC and clang, runs what it loaded, and checks every refusal.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 *   test_core DIR      DIR holds the fixtures
 * Every allocation is counted: a load, an unload or a refusal that leaves
 * one behind fails the test. Each fixture is then loaded cut short at many
 * lengths and with bytes changed, which must refuse or load, never crash. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "../elf_core.h"
#include "fixtures/fixture.h"

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

struct file { unsigned char *data; unsigned long size; };

struct tenv {
    struct oe_env env;
    struct file f;
    long live;                  /* allocations not yet freed */
    unsigned long bytes;        /* segment bytes now allocated */
    const char *dir;
    int depth;
};

/* the shared objects loaded for DT_NEEDED, searched by resolve */
#define MAXLIBS 8
static struct oe_image libs[MAXLIBS];
static struct tenv libenv[MAXLIBS];
static int nlibs;

static int read_file(const char *path, struct file *f)
{
    FILE *fp = fopen(path, "rb");
    long n;
    if (!fp) return -1;
    fseek(fp, 0, SEEK_END);
    n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    f->data = malloc(n ? n : 1);
    f->size = fread(f->data, 1, n, fp);
    fclose(fp);
    return 0;
}

static void *t_alloc(struct oe_env *env, oe_u32 size, enum oe_mem kind)
{
    struct tenv *t = (struct tenv *)env;
    void *p;
    if (size > 64UL * 1024 * 1024) return NULL;     /* what a damaged header asks */
    if (kind == OE_MEM_SEGMENT) {
        /* segments run: memory qemu lets us execute, cleared by mmap */
        p = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) return NULL;
    } else {
        if (posix_memalign(&p, 8, size)) return NULL;
        memset(p, 0, size);
    }
    t->live++;
    if (kind == OE_MEM_SEGMENT) t->bytes += size;
    return p;
}

static void t_free(struct oe_env *env, void *mem, oe_u32 size, enum oe_mem kind)
{
    struct tenv *t = (struct tenv *)env;
    t->live--;
    if (kind == OE_MEM_SEGMENT) { t->bytes -= size; munmap(mem, size); }
    else free(mem);
}

static oe_s32 t_read(struct oe_env *env, oe_u32 off, void *buf, oe_u32 len)
{
    struct tenv *t = (struct tenv *)env;
    if (off > t->f.size || len > t->f.size - off) return -1;
    memcpy(buf, t->f.data + off, len);
    return 0;
}

static int host_add(int a, int b) { return a + b; }
static int host_hello(const char *s) { return (int)strlen(s) + 40; }
static int notes;                       /* ctor.c's destructors, in the order they ran */
static void host_note(int v) { notes = notes * 10 + v; }

static oe_u32 t_resolve(struct oe_env *env, const char *name, int *found)
{
    int i;
    (void)env;
    if (!strcmp(name, "host_add")) { *found = 1; return (oe_u32)(unsigned long)host_add; }
    if (!strcmp(name, "host_note")) { *found = 1; return (oe_u32)(unsigned long)host_note; }
    for (i = 0; i < nlibs; i++) {
        oe_u32 a = oe_lookup(&libs[i], name);
        if (a) { *found = 1; return a; }
    }
    return 0;
}

static void env_init(struct tenv *t, const char *dir);

static int t_needed(struct oe_env *env, const char *name)
{
    struct tenv *t = (struct tenv *)env, *n;
    char path[512];
    int e;
    if (nlibs == MAXLIBS || t->depth > 4) return OE_ERR_NEEDED;
    n = &libenv[nlibs];
    env_init(n, t->dir);
    n->depth = t->depth + 1;
    snprintf(path, sizeof path, "%s/%s", t->dir, name);
    if (read_file(path, &n->f)) return OE_ERR_NEEDED;
    e = oe_load(&n->env, &libs[nlibs]);
    free(n->f.data);
    if (e == OE_OK) nlibs++;
    return e;
}

static void env_init(struct tenv *t, const char *dir)
{
    memset(t, 0, sizeof *t);
    t->env.alloc = t_alloc;
    t->env.free = t_free;
    t->env.read = t_read;
    t->env.resolve = t_resolve;
    t->env.needed = t_needed;
    t->dir = dir;
}

static void unload_libs(void)
{
    while (nlibs) {
        nlibs--;
        oe_unload(&libenv[nlibs].env, &libs[nlibs]);
        CHECK(libenv[nlibs].live == 0, "library %d leaves %ld allocations", nlibs, libenv[nlibs].live);
    }
}

/* Load DIR/name as a program and run its entry: it must return FX_ALL.
 * 1 when it was there to run. */
static int run(const char *dir, const char *name, oe_u32 seg_max, int want_type, int min_segs)
{
    struct tenv t;
    struct oe_image img;
    char path[512];
    int e;
    env_init(&t, dir);
    t.env.seg_max = seg_max;
    t.env.program = 1;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (read_file(path, &t.f)) { printf("skipped %s: not built\n", name); return 0; }
    e = oe_load(&t.env, &img);
    CHECK(e == OE_OK, "%s: %s (detail %lu, name %s)", name, oe_strerror(e), (unsigned long)img.detail, img.name);
    if (e == OE_OK) {
        struct fx_api api = { host_hello, 1 };
        int (*entry)(const struct fx_api *) = (int (*)(const struct fx_api *))(unsigned long)img.entry;
        int got;
        CHECK(img.type == want_type, "%s: type %d, want %d", name, img.type, want_type);
        CHECK((int)img.nseg >= min_segs, "%s: %lu segments, want at least %d", name, (unsigned long)img.nseg, min_segs);
        CHECK(img.entry != 0, "%s: no entry", name);
        {
            oe_u32 i;
            for (i = 0; i < img.nseg; i++)
                __builtin___clear_cache((char *)img.seg[i], (char *)img.seg[i] + *(oe_u32 *)img.seg[i]);
        }
        got = img.entry ? entry(&api) : 0;
        CHECK(got == FX_ALL, "%s: checks %#x, want %#x", name, got, FX_ALL);
        printf("%-22s seg_max %-5lu %2lu segment(s), %6lu bytes, checks %#x\n", name, (unsigned long)seg_max,
               (unsigned long)img.nseg, t.bytes, got);
        oe_unload(&t.env, &img);
    }
    unload_libs();
    CHECK(t.live == 0, "%s: %ld allocations left", name, t.live);
    free(t.f.data);
    return 1;
}

/* A program with constructors (ctor.c): it checks their order and its
 * aligned data itself; its destructors must have run, the other way */
static void run_ctor(const char *dir, const char *name, oe_u32 seg_max, int want_type, int imports)
{
    notes = 0;
    if (run(dir, name, seg_max, want_type, 1) && imports)
        CHECK(notes == 31, "%s: destructors ran as %d, want 31", name, notes);
}

/* Load DIR/name: it must be refused with want */
static void refuse(const char *dir, const char *name, int want)
{
    struct tenv t;
    struct oe_image img;
    char path[512];
    int e;
    env_init(&t, dir);
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (read_file(path, &t.f)) { printf("skipped %s: not built\n", name); return; }
    e = oe_load(&t.env, &img);
    CHECK(e == want, "%s: %s, want %s", name, oe_strerror(e), oe_strerror(want));
    printf("%-22s refused: %s\n", name, oe_strerror(e));
    if (e == OE_OK) oe_unload(&t.env, &img);
    unload_libs();
    CHECK(t.live == 0, "%s: %ld allocations left", name, t.live);
    free(t.f.data);
}

static unsigned long fx_g32(const unsigned char *p) { return (unsigned long)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static void fx_p32(unsigned char *p, unsigned long v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

/* The header of f's first section of this type, or NULL */
static unsigned char *fx_section(const struct file *f, unsigned long type)
{
    unsigned long shoff = fx_g32(f->data + 32), shnum = f->data[48] << 8 | f->data[49], i;
    for (i = 1; i < shnum && shoff + (i + 1) * 40 <= f->size; i++)
        if (fx_g32(f->data + shoff + i * 40 + 4) == type) return f->data + shoff + i * 40;
    return NULL;
}

/* Table sizes that wrap 32 bits when the core checks them */
static void bad_tables(const char *dir)
{
    struct tenv t;
    struct oe_image img;
    char path[512];
    unsigned char *s;
    int e;
    /* an object's symbol strings, 0xffffffff bytes at offset 0: size + 1 is 0 */
    env_init(&t, dir);
    snprintf(path, sizeof path, "%s/prog-gcc.o", dir);
    if (read_file(path, &t.f) == 0 && (s = fx_section(&t.f, 2)) != NULL) {
        s = t.f.data + fx_g32(t.f.data + 32) + fx_g32(s + 24) * 40;
        fx_p32(s + 16, 0);
        fx_p32(s + 20, 0xffffffffUL);
        e = oe_load(&t.env, &img);
        CHECK(e == OE_ERR_FORMAT, "strings of 0xffffffff bytes: %s", oe_strerror(e));
        printf("%-22s refused: %s\n", "symstr-wrap", oe_strerror(e));
        if (e == OE_OK) oe_unload(&t.env, &img);
        CHECK(t.live == 0, "symstr-wrap: %ld allocations left", t.live);
        free(t.f.data);
    } else printf("skipped symstr-wrap: no prog-gcc.o\n");
    /* a SysV hash whose nbucket + nchain, times 4, wraps to 4 bytes: its
     * symbols can't be looked up through it */
    env_init(&t, dir);
    snprintf(path, sizeof path, "%s/libfix.so", dir);
    if (read_file(path, &t.f) == 0 && (s = fx_section(&t.f, 5)) != NULL) {
        unsigned char *h = t.f.data + fx_g32(s + 16);
        fx_p32(h, 0x40000001UL - fx_g32(h + 4));
        unsigned long nsym = 0;
        e = oe_load(&t.env, &img);
        if (e == OE_OK) {
            nsym = img.nsym;
            CHECK(img.nsym == 0 && img.hash == 0, "hash-wrap: loaded with %lu symbols through the hash",
                  (unsigned long)img.nsym);
            if (img.nsym == 0) CHECK(oe_lookup(&img, "lib_mul") == 0, "hash-wrap: lib_mul found");
            oe_unload(&t.env, &img);
        }
        if (e == OE_OK) printf("%-22s loaded, %lu symbols through the hash\n", "hash-wrap", nsym);
        else printf("%-22s refused: %s\n", "hash-wrap", oe_strerror(e));
        CHECK(t.live == 0, "hash-wrap: %ld allocations left", t.live);
        free(t.f.data);
    } else printf("skipped hash-wrap: no libfix.so\n");
}

/* Cut short and changed bytes: refuse or load, never crash or leak */
static void damage(const char *dir, const char *name)
{
    struct tenv t;
    struct oe_image img;
    char path[512];
    unsigned long full, cut, i, loaded = 0, tries = 0;
    unsigned seed = 12345;
    env_init(&t, dir);
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (read_file(path, &t.f)) return;
    full = t.f.size;
    for (cut = 0; cut < full; cut += 1 + cut / 16) {
        t.f.size = cut;
        if (oe_load(&t.env, &img) == OE_OK) { loaded++; oe_unload(&t.env, &img); }
        unload_libs();
        tries++;
    }
    t.f.size = full;
    for (i = 0; i < 400; i++) {
        unsigned long at;
        unsigned char old;
        seed = seed * 1103515245u + 12345u;
        at = (seed >> 8) % (full < 4096 ? full : 4096);    /* the headers are near the start */
        old = t.f.data[at];
        t.f.data[at] ^= (unsigned char)(1u << ((seed >> 4) & 7));
        if (oe_load(&t.env, &img) == OE_OK) { loaded++; oe_unload(&t.env, &img); }
        unload_libs();
        t.f.data[at] = old;
        tries++;
    }
    CHECK(t.live == 0, "%s damaged: %ld allocations left", name, t.live);
    printf("%-22s damaged %lu ways, %lu still loaded, no leak\n", name, tries, loaded);
    free(t.f.data);
}

static void identify(void)
{
    static const unsigned char hunk[] = { 0, 0, 3, 0xf3, 0, 0, 0, 0 };
    unsigned char ppc[52] = { 0x7f, 'E', 'L', 'F', 1, 2, 1, 0 };
    struct oe_info info;
    CHECK(oe_identify(hunk, sizeof hunk, &info) == OE_ERR_NOT_ELF && !info.is_elf, "a hunk file is not ELF");
    ppc[17] = 2;                /* ET_EXEC */
    ppc[19] = 20;               /* EM_PPC: an OS 4 or MorphOS program */
    CHECK(oe_identify(ppc, sizeof ppc, &info) == OE_ERR_MACHINE && info.machine == 20, "PowerPC is another CPU");
    ppc[19] = 4;                /* EM_68K, but 64-bit */
    ppc[4] = 2;
    CHECK(oe_identify(ppc, sizeof ppc, &info) == OE_ERR_CLASS, "64-bit is refused");
}

int main(int argc, char **argv)
{
    const char *d = argc > 1 ? argv[1] : ".";
    setvbuf(stdout, NULL, _IONBF, 0);
    identify();
    /* objects, AROS and MorphOS style: one segment, then small segments */
    run(d, "prog-gcc.o", 0, 1, 1);
    run(d, "prog-gcc.o", 256, 1, 3);
    run(d, "prog-gcc-sections.o", 512, 1, 3);
    run(d, "prog-gcc-r.o", 0, 1, 1);
    run(d, "prog-gcc-r.o", 1024, 1, 3);
    run(d, "prog-clang.o", 0, 1, 1);
    run(d, "prog-clang.o", 256, 1, 1);        /* its PC16 data loads fail apart, so it loads again as one */
    run(d, "prog-rust.o", 0, 1, 1);           /* Rust, core only: LLVM's backend */
    run(d, "prog-rust.o", 512, 1, 1);
    run(d, "prog-rust-exec-q", 0, 2, 1);
    /* executables with their relocations, and position-independent ones */
    run(d, "prog-exec-q", 0, 2, 1);
    run(d, "prog-clang-exec-q", 0, 2, 1);
    run(d, "prog-pie", 0, 3, 1);
    run(d, "main.so", 0, 3, 1);               /* needs libfix.so */
    /* constructors and destructors in a start routine, data aligned to 32 */
    run_ctor(d, "ctor.o", 0, 1, 1);
    run_ctor(d, "ctor.o", 16, 1, 1);          /* every section a segment of its own */
    run_ctor(d, "ctor-r.o", 0, 1, 1);
    run_ctor(d, "ctor-exec-q", 0, 2, 0);      /* an executable imports nothing */
    run_ctor(d, "ctor-pie", 0, 3, 1);
    /* what it must refuse */
    refuse(d, "prog-exec-fixed", OE_ERR_FIXED);
    refuse(d, "prog-pic.o", OE_ERR_RELOC);
    refuse(d, "tls.o", OE_ERR_TLS);
    refuse(d, "linux-dynamic", OE_ERR_INTERP);
    refuse(d, "host-x86", OE_ERR_MACHINE);
    refuse(d, "prog-undef.o", OE_ERR_UNDEF);
    refuse(d, "prog-exec-undef", OE_ERR_UNDEF);
    refuse(d, "prog-exec-badentry", OE_ERR_FORMAT);
    refuse(d, "needs-missing.so", OE_ERR_NEEDED);
    refuse(d, "not-elf.txt", OE_ERR_NOT_ELF);
    /* damaged files */
    damage(d, "prog-gcc-r.o");
    damage(d, "prog-clang.o");
    damage(d, "prog-rust.o");
    damage(d, "prog-exec-q");
    damage(d, "prog-pie");
    damage(d, "main.so");
    damage(d, "ctor-r.o");
    damage(d, "ctor-exec-q");
    damage(d, "ctor-pie");
    bad_tables(d);
    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("all passed\n");
    return 0;
}
