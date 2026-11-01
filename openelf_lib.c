/* GFOpenELF: elf.library, 68k ELF programs and shared objects for AmigaOS
 * 3.2.3 and GFOpenBench.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 *
 * The loading is done by elf_core.c (no Amiga headers, tested under
 * qemu-m68k); this file is the library around it: the RomTag, the calls
 * (openelf_stubs.S takes each from its registers to a C call), memory from
 * AllocMem, the file through DOS, shared objects kept by name, and the
 * LoadSeg hook.
 *
 * The hook (ElfHook): dos.library's LoadSeg and NewLoadSeg patched so the
 * Amiga's own hunk format is tried first, as before; only when that says
 * "not an object file" does elf.library look for \x7fELF, and when that
 * is not there either the original error goes back unchanged, so whatever
 * the caller tries next (datatypes, a script) still runs.
 *
 * Multicore (amigachrome-guest's libraries/OPEN_LIBRARIES.md, section 5):
 * SysBase and DOSBase are set once at init. The shared-object list and the
 * hook are under one semaphore, held for a whole load (a load opens what it
 * needs, and semaphores nest in one task), so loads take turns: the core
 * itself is reentrant, and the list is what they share. */
#include "openelf_amiga.h"
#include "elf_core.h"
#include "openelf_public.h"

#define LIB_VERSION 1
#define LIB_REVISION 0
#define SEG_MAX (32UL * 1024)   /* ET_REL: sections per segment, for fragmented memory */
#define MAX_DEPS 16
/* DT_NEEDED opens each object inside the load of the one that needs it,
 * on the caller's stack (dlopen_locked, oe_load and load_once: about 1.2 KB
 * a level, more with DOS's Open and Read under it), so a chain of needs
 * stops at MAX_DEPTH levels, or sooner when the stack has less than
 * NEEDED_STACK bytes left for the next one: a LoadSeg from a 4 KB Shell
 * stack has room for few */
#define MAX_DEPTH 16
#define NEEDED_STACK 2048UL

struct dl {
    struct dl *next;
    ULONG refs;
    ULONG loading;              /* listed while it loads, so a DT_NEEDED cycle is seen */
    struct oe_image img;
    struct dl *deps[MAX_DEPS];
    ULONG ndeps;
    ULONG size;                 /* this allocation's */
    char name[4];               /* as opened, NUL-terminated, longer as needed */
};

struct ElfBase {
    struct Library lib;
    UWORD pad;
    BPTR seglist;
    struct SignalSemaphore sem;
    struct dl *dls;             /* shared objects, newest first */
    APTR old_loadseg, old_newloadseg;
    LONG hooked;
    /* the last failure, any task's: for ElfLastError, a diagnostic only */
    LONG last_error;
    ULONG last_detail;
    char last_name[OE_NAME_MAX];
};

/* Set once by lib_init, read-only after. */
APTR SysBase;
APTR DOSBase;
struct ElfBase *ElfBase;

const char lib_name[] = "elf.library";             /* the RomTag's too (openelf_stubs.S) */
const char lib_id[] = "elf.library 1.0 (22.10.2026) GFOpenELF, AmigaChrome Team\r\n";
static const char lib_ver[] __attribute__((used)) =
    "\0$VER: elf.library 1.0 (22.10.2026) GFOpenELF, AmigaChrome Team";

/* ---- Small helpers ------------------------------------------------------ */

static ULONG slen(const char *s) { ULONG n = 0; while (s[n]) n++; return n; }
static int seq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void scopy(char *d, const char *s, ULONG max)
{ ULONG i = 0; while (s[i] && i + 1 < max) { d[i] = s[i]; i++; } d[i] = 0; }

/* ---- What the core asks of the system ------------------------------------ */

struct fenv {
    struct oe_env env;
    BPTR fh;
    ULONG pos;
    struct dl *owner;           /* the shared object being loaded, for its DT_NEEDED; 0 for a program */
    ULONG depth;                /* 0 for what the caller opened, 1 for what that needs, ... */
    int err;                    /* a nested DLOpen's error */
};

static void *e_alloc(struct oe_env *env, oe_u32 size, enum oe_mem kind)
{
    (void)env; (void)kind;
    return AllocMem(size, MEMF_ANY | MEMF_CLEAR);
}

static void e_free(struct oe_env *env, void *mem, oe_u32 size, enum oe_mem kind)
{
    (void)env; (void)kind;
    FreeMem(mem, size);
}

static oe_s32 e_read(struct oe_env *env, oe_u32 off, void *buf, oe_u32 len)
{
    struct fenv *f = (struct fenv *)env;
    if (f->pos != off) {
        if (Seek(f->fh, (LONG)off, OFFSET_BEGINNING) < 0) return -1;
        f->pos = off;
    }
    if (Read(f->fh, buf, len) != (LONG)len) { f->pos = 0xffffffffUL; return -1; }
    f->pos += len;
    return 0;
}

/* Undefined symbols: from the shared objects already open, newest first */
static oe_u32 e_resolve(struct oe_env *env, const char *name, int *found)
{
    struct dl *d;
    (void)env;
    for (d = ElfBase->dls; d; d = d->next) {
        oe_u32 a;
        if (d->loading) continue;
        a = oe_lookup(&d->img, name);
        if (a) { *found = 1; return a; }
    }
    return 0;
}

static struct dl *dlopen_locked(const char *name, ULONG depth, int *err);

/* Bytes left below here on this task's stack; ~0 when the stack pointer is
 * not on the stack Exec has for the task (we can't tell) */
static ULONG stack_left(void)
{
    UBYTE *task = FindTask(0);
    ULONG here = (ULONG)&task;
    ULONG lower = *(ULONG *)(task + TC_SPLOWER), upper = *(ULONG *)(task + TC_SPUPPER);
    if (here <= lower || here > upper) return 0xffffffffUL;
    return here - lower;
}

static int e_needed(struct oe_env *env, const char *name)
{
    struct fenv *f = (struct fenv *)env;
    int err = OE_OK;
    struct dl *d;
    if (f->owner && f->owner->ndeps == MAX_DEPS) return OE_ERR_NEEDED;    /* more than it can keep */
    if (f->depth >= MAX_DEPTH || stack_left() < NEEDED_STACK) return OE_ERR_NEEDED;    /* too deep a chain */
    if (!(d = dlopen_locked(name, f->depth + 1, &err))) return err ? err : OE_ERR_NEEDED;
    if (f->owner) f->owner->deps[f->owner->ndeps++] = d;
    /* a program's needs stay open: its seglist has nowhere to keep them */
    return OE_OK;
}

static void env_init(struct fenv *f, BPTR fh, struct dl *owner)
{
    char *p = (char *)f;
    ULONG i;
    for (i = 0; i < sizeof *f; i++) p[i] = 0;
    f->env.alloc = e_alloc;
    f->env.free = e_free;
    f->env.read = e_read;
    f->env.resolve = e_resolve;
    f->env.needed = e_needed;
    f->env.seg_max = SEG_MAX;
    f->env.program = !owner;    /* a program's constructors run in its start routine */
    f->fh = fh;
    f->owner = owner;
    /* the file's length, for the core's checks: Seek to the end tells it
     * on the way back to the start (0, not known, when a handler can't) */
    if (fh && Seek(fh, 0, OFFSET_END) >= 0) {
        LONG end = Seek(fh, 0, OFFSET_BEGINNING);
        if (end < 0) f->pos = 0xffffffffUL;     /* where the file stands is not known */
        else f->env.file_size = (ULONG)end;
    }
}

static void note_error(int e, const struct oe_image *img)
{
    ElfBase->last_error = e;
    ElfBase->last_detail = img->detail;
    scopy(ElfBase->last_name, img->name, OE_NAME_MAX);
}

static LONG dos_error(int e)
{
    switch (e) {
    case OE_ERR_NOT_ELF: return ERROR_FILE_NOT_OBJECT;
    case OE_ERR_NOMEM: return ERROR_NO_FREE_STORE;
    case OE_ERR_READ: case OE_ERR_FORMAT: return ERROR_BAD_HUNK;
    default: return ERROR_OBJECT_WRONG_TYPE;
    }
}

typedef void (*vfn)(void);

static void run_array(oe_u32 at, oe_u32 n, int backwards)
{
    oe_u32 i;
    for (i = 0; i < n; i++) {
        ULONG f = ((ULONG *)at)[backwards ? n - 1 - i : i];
        if (f && f != 0xffffffffUL) ((vfn)f)();
    }
}

/* ---- Programs --------------------------------------------------------- */

/* Load the open file fh as a seglist: the segments chained as BPTRs, with
 * a jump in front when the entry isn't the first segment's first byte. */
static BPTR loadseg_fh(BPTR fh, int *errp)
{
    struct fenv f;
    struct oe_image img;
    BPTR first = 0;
    int e;
    ULONG i;
    ObtainSemaphore(&ElfBase->sem);
    env_init(&f, fh, 0);
    e = oe_load(&f.env, &img);
    if (e == OE_OK && !img.entry) { oe_unload(&f.env, &img); e = OE_ERR_TYPE; }    /* a shared object with no entry */
    if (e == OE_OK) {
        UBYTE *stub = 0;
        if (img.entry != (ULONG)img.seg[0] + 8) {
            stub = AllocMem(8 + OE_STUB_SIZE, MEMF_ANY | MEMF_CLEAR);
            if (!stub) { oe_unload(&f.env, &img); e = OE_ERR_NOMEM; }
            else {
                *(ULONG *)stub = 8 + OE_STUB_SIZE;
                oe_write_stub(stub + 8, img.entry);
            }
        }
        if (e == OE_OK) {
            for (i = img.nseg; i-- > 0; ) {
                UBYTE *s = img.seg[i];
                *(BPTR *)(s + 4) = first;
                first = MKBADDR(s + 4);
            }
            if (stub) {
                *(BPTR *)(stub + 4) = first;
                first = MKBADDR(stub + 4);
            }
            /* the seglist owns the segments now; only the array goes */
            FreeMem(img.seg, img.segcap * sizeof(void *));
            CacheClearU();
        }
    }
    if (e != OE_OK) note_error(e, &img);
    ReleaseSemaphore(&ElfBase->sem);
    *errp = e;
    return first;
}

static BPTR loadseg_name(CONST_STRPTR name, int *errp)
{
    BPTR fh = Open(name, MODE_OLDFILE), seg;
    if (!fh) { *errp = -1; return 0; }
    seg = loadseg_fh(fh, errp);
    Close(fh);
    return seg;
}

ULONG L_ElfLoadSeg(CONST_STRPTR name)
{
    int e;
    BPTR seg = loadseg_name(name, &e);
    if (!seg && e > 0) SetIoErr(dos_error(e));
    return (ULONG)seg;
}

LONG L_ElfIdentify(const void *buf, ULONG len)
{
    struct oe_info info;
    return oe_identify(buf, len, &info);
}

/* ---- Shared objects ------------------------------------------------------ */

static struct dl *dl_find(const char *name)
{
    struct dl *d;
    for (d = ElfBase->dls; d; d = d->next)
        if (seq(d->name, name)) return d;
    return 0;
}

static void dl_unlist(struct dl *d)
{
    struct dl **pp;
    for (pp = &ElfBase->dls; *pp; pp = &(*pp)->next)
        if (*pp == d) { *pp = d->next; break; }
}

static void dl_release(struct dl *d)
{
    ULONG i;
    struct fenv f;
    if (--d->refs) return;
    run_array(d->img.fini_array, d->img.fini_count, 1);
    if (d->img.fini) ((vfn)d->img.fini)();
    dl_unlist(d);
    env_init(&f, 0, 0);
    oe_unload(&f.env, &d->img);
    for (i = 0; i < d->ndeps; i++) dl_release(d->deps[i]);
    FreeMem(d, d->size);
}

/* name as given when it has a path, else LIBS:elf/name, then as given */
static BPTR dl_openfile(const char *name)
{
    char path[160];
    const char *p;
    BPTR fh;
    for (p = name; *p; p++)
        if (*p == ':' || *p == '/') return Open(name, MODE_OLDFILE);
    if (slen(name) < sizeof path - 10) {
        scopy(path, "LIBS:elf/", sizeof path);
        scopy(path + 9, name, sizeof path - 9);
        if ((fh = Open(path, MODE_OLDFILE))) return fh;
    }
    return Open(name, MODE_OLDFILE);
}

static struct dl *dlopen_locked(const char *name, ULONG depth, int *err)
{
    struct dl *d = dl_find(name);
    struct fenv f;
    ULONG size;
    BPTR fh;
    int e;
    if (d && d->loading) { *err = OE_ERR_NEEDED; return 0; }    /* it needs itself, round a cycle */
    if (d) { d->refs++; return d; }
    size = (sizeof *d + slen(name) + 4) & ~3UL;
    if (!(d = AllocMem(size, MEMF_ANY | MEMF_CLEAR))) { *err = OE_ERR_NOMEM; return 0; }
    d->size = size;
    scopy(d->name, name, slen(name) + 1);
    if (!(fh = dl_openfile(name))) { FreeMem(d, size); *err = OE_ERR_NEEDED; return 0; }
    env_init(&f, fh, d);
    f.depth = depth;
    d->refs = 1;
    d->loading = 1;
    d->next = ElfBase->dls;
    ElfBase->dls = d;
    e = oe_load(&f.env, &d->img);
    Close(fh);
    d->loading = 0;
    if (e != OE_OK) {
        ULONG i;
        note_error(e, &d->img);
        dl_unlist(d);
        for (i = 0; i < d->ndeps; i++) dl_release(d->deps[i]);
        FreeMem(d, size);
        *err = e;
        return 0;
    }
    CacheClearU();
    run_array(d->img.preinit_array, d->img.preinit_count, 0);
    if (d->img.init) ((vfn)d->img.init)();
    run_array(d->img.init_array, d->img.init_count, 0);
    return d;
}

ULONG L_DLOpen(CONST_STRPTR name, ULONG flags)
{
    int e = OE_OK;
    struct dl *d;
    (void)flags;                /* RTLD_NOW is all there is: every symbol is bound at load */
    if (!name) return 0;
    ObtainSemaphore(&ElfBase->sem);
    d = dlopen_locked(name, 0, &e);
    ReleaseSemaphore(&ElfBase->sem);
    if (!d) SetIoErr(e == OE_ERR_NEEDED ? ERROR_OBJECT_NOT_FOUND : dos_error(e));
    return (ULONG)d;
}

/* A handle's symbol: the object itself, then what it needs, as dlsym
 * looks (no cycles: dlopen_locked refuses them) */
static ULONG dl_sym(struct dl *d, CONST_STRPTR name, int depth)
{
    ULONG a = oe_lookup(&d->img, name), i;
    for (i = 0; !a && depth < 8 && i < d->ndeps; i++) a = dl_sym(d->deps[i], name, depth + 1);
    return a;
}

ULONG L_DLSym(APTR handle, CONST_STRPTR name)
{
    ULONG a = 0;
    ObtainSemaphore(&ElfBase->sem);
    if (handle) a = dl_sym((struct dl *)handle, name, 0);
    else {
        int found = 0;
        a = e_resolve(0, name, &found);
    }
    ReleaseSemaphore(&ElfBase->sem);
    return a;
}

LONG L_DLClose(APTR handle)
{
    struct dl *d;
    ObtainSemaphore(&ElfBase->sem);
    for (d = ElfBase->dls; d && d != handle; d = d->next) ;
    if (d) dl_release(d);
    ReleaseSemaphore(&ElfBase->sem);
    return d ? 0 : -1;
}

/* ---- The LoadSeg hook ------------------------------------------------- */

ULONG elf_call_d1(APTR fn, ULONG d1, APTR a6);              /* openelf_stubs.S */
ULONG elf_call_d1d2(APTR fn, ULONG d1, ULONG d2, APTR a6);
extern char elf_loadseg_patch[], elf_newloadseg_patch[];

static int elf_worth_trying(LONG err)
{
    return err == ERROR_FILE_NOT_OBJECT || err == ERROR_BAD_HUNK || err == ERROR_OBJECT_WRONG_TYPE;
}

/* dos.library's LoadSeg, hunks first */
ULONG elf_loadseg_c(CONST_STRPTR name, APTR dos)
{
    ULONG seg = elf_call_d1(ElfBase->old_loadseg, (ULONG)name, dos);
    LONG err;
    int e;
    if (seg || !name) return seg;
    err = IoErr();
    if (!elf_worth_trying(err)) return 0;
    seg = (ULONG)loadseg_name(name, &e);
    if (!seg) SetIoErr(e == OE_ERR_NOT_ELF || e < 0 ? err : dos_error(e));
    return seg;
}

/* NewLoadSeg(file, tags): the tags are for hunk files, so an ELF file
 * loads as LoadSeg would */
ULONG elf_newloadseg_c(CONST_STRPTR name, ULONG tags, APTR dos)
{
    ULONG seg = elf_call_d1d2(ElfBase->old_newloadseg, (ULONG)name, tags, dos);
    LONG err;
    int e;
    if (seg || !name) return seg;
    err = IoErr();
    if (!elf_worth_trying(err)) return 0;
    seg = (ULONG)loadseg_name(name, &e);
    if (!seg) SetIoErr(e == OE_ERR_NOT_ELF || e < 0 ? err : dos_error(e));
    return seg;
}

/* on: 1 installs the hook, 0 takes it out (only when nobody patched over
 * it since). The state before goes back; -1 when 0 could not unhook. */
LONG L_ElfHook(LONG on)
{
    LONG was;
    ObtainSemaphore(&ElfBase->sem);
    was = ElfBase->hooked;
    if (on && !was) {
        Forbid();
        ElfBase->old_loadseg = SetFunction(DOSBase, LVO_LoadSeg, elf_loadseg_patch);
        ElfBase->old_newloadseg = SetFunction(DOSBase, LVO_NewLoadSeg, elf_newloadseg_patch);
        CacheClearU();
        Permit();
        ElfBase->hooked = 1;
        ElfBase->lib.lib_OpenCnt++;     /* hooked, the library stays */
    } else if (!on && was) {
        APTR a, b;
        Forbid();
        a = SetFunction(DOSBase, LVO_LoadSeg, ElfBase->old_loadseg);
        b = SetFunction(DOSBase, LVO_NewLoadSeg, ElfBase->old_newloadseg);
        if (a != (APTR)elf_loadseg_patch || b != (APTR)elf_newloadseg_patch) {
            (void)SetFunction(DOSBase, LVO_LoadSeg, a); /* someone patched over us: leave it */
            (void)SetFunction(DOSBase, LVO_NewLoadSeg, b);
            was = -1;
        } else {
            ElfBase->hooked = 0;
            ElfBase->lib.lib_OpenCnt--;
        }
        CacheClearU();
        Permit();
    }
    ReleaseSemaphore(&ElfBase->sem);
    return was;
}

LONG L_ElfLastError(STRPTR name, ULONG namelen)
{
    if (name && namelen) scopy(name, ElfBase->last_name, namelen);
    return ElfBase->last_error;
}

ULONG L_ElfErrorString(LONG err)
{
    return (ULONG)oe_strerror(err);
}

/* ---- Library housekeeping (called from openelf_stubs.S) ---------------- */

ULONG lib_init_c(struct ElfBase *base, BPTR seglist, APTR sys)
{
    SysBase = sys;
    ElfBase = base;
    base->seglist = seglist;
    base->lib.lib_Node.ln_Type = NT_LIBRARY;
    base->lib.lib_Node.ln_Name = (char *)lib_name;
    base->lib.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    base->lib.lib_Version = LIB_VERSION;
    base->lib.lib_Revision = LIB_REVISION;
    base->lib.lib_IdString = (APTR)lib_id;
    InitSemaphore(&base->sem);
    if (!(DOSBase = OpenLibrary("dos.library", 36))) {
        FreeMem((UBYTE *)base - base->lib.lib_NegSize, base->lib.lib_NegSize + base->lib.lib_PosSize);
        return 0;
    }
    return (ULONG)base;
}

ULONG lib_open_c(struct ElfBase *base)
{
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    return (ULONG)base;
}

ULONG lib_expunge_c(struct ElfBase *base)
{
    BPTR seglist;
    if (base->lib.lib_OpenCnt || base->hooked || base->dls) {
        base->lib.lib_Flags |= LIBF_DELEXP;
        return 0;
    }
    seglist = base->seglist;
    Remove(&base->lib.lib_Node);
    CloseLibrary(DOSBase);
    FreeMem((UBYTE *)base - base->lib.lib_NegSize, base->lib.lib_NegSize + base->lib.lib_PosSize);
    return (ULONG)seglist;
}

ULONG lib_close_c(struct ElfBase *base)
{
    if (base->lib.lib_OpenCnt) base->lib.lib_OpenCnt--;
    if (base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP))
        return lib_expunge_c(base);
    return 0;
}


/* ---- The jump table and the RomTag's init table ------------------------ */

extern char stub_init[], stub_open[], stub_close[], stub_expunge[], stub_null[];
extern char stub_ElfLoadSeg[], stub_ElfIdentify[], stub_DLOpen[], stub_DLSym[], stub_DLClose[];
extern char stub_ElfHook[], stub_ElfLastError[], stub_ElfErrorString[];

static const APTR lib_vectors[] = {
    stub_open, stub_close, stub_expunge, stub_null,
    stub_ElfLoadSeg,            /* -30 */
    stub_ElfIdentify,           /* -36 */
    stub_DLOpen,                /* -42 */
    stub_DLSym,                 /* -48 */
    stub_DLClose,               /* -54 */
    stub_ElfHook,               /* -60 */
    stub_ElfLastError,          /* -66 */
    stub_ElfErrorString,        /* -72 */
    (APTR)-1
};

const ULONG lib_inittable[4] = {
    sizeof(struct ElfBase), (ULONG)lib_vectors, 0, (ULONG)stub_init
};

/* The end of the library, for the RomTag's rt_EndSkip */
const char elf_endskip[] __attribute__((section(".rodata.zzend"))) = "";
