#define _GNU_SOURCE
#include "nso_loader.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#if !defined(__aarch64__)
#error "nso_loader targets AArch64 hosts (module code executes natively, untranslated)"
#endif

#define NSO_MAGIC 0x304F534Eu
#define R_AARCH64_ABS64      0x101u
#define R_AARCH64_ABS32      0x102u
#define R_AARCH64_GLOB_DAT   0x401u
#define R_AARCH64_JUMP_SLOT  0x402u
#define R_AARCH64_RELATIVE   0x403u

typedef struct {
    uint32_t file_off, dst_off, size, aux;
} nso_seg_t;

typedef struct {
    uint32_t magic, version, reserved, flags;
    nso_seg_t seg[3];
    uint8_t build_id[0x20];
    uint32_t compressed_size[3];
    uint8_t pad[0x24];
    uint64_t dynstr_extents, dynsym_extents;
    uint8_t section_hash[3][0x20];
} nso_header_t;

struct nso_module {
    uint8_t *image;         /* base of the contiguous module mapping */
    size_t image_size;
    char *path;

    const char *dynstr;
    uint32_t dynstr_size;
    uint64_t *sym_value;    /* st_value per dynsym entry */
    uint16_t *sym_shndx;
    uint32_t *sym_name;
    uint32_t sym_count;

    uint64_t reloc_base;    /* file offset of the relocation table */
    uint64_t reloc_count;

    uint64_t *imports;      /* resolved host function per undefined symbol */
    char **import_names;
    uint32_t import_count;

    nso_import_fn import_fn;
    uint8_t build_id[16];
    void *user_dlsym;
    uint64_t trap_calls;
};

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t rd64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }
static uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }

static uint64_t page_size(void) {
    static uint64_t ps;
    if (!ps) ps = (uint64_t)sysconf(_SC_PAGESIZE);
    return ps;
}
static uint64_t page_down(uint64_t v) { return v & ~(page_size() - 1); }
static uint64_t page_up(uint64_t v) { return page_down(v + page_size() - 1); }

static int seg_index_of(const nso_header_t *h, uint64_t addr) {
    for (int i = 0; i < 3; i++)
        if (addr >= h->seg[i].dst_off && addr < h->seg[i].dst_off + h->seg[i].size)
            return i;
    /* .bss follows .rwdata and is part of the module image too */
    uint64_t bss_lo = (uint64_t)h->seg[2].dst_off + h->seg[2].size;
    if (addr >= bss_lo && addr < bss_lo + h->seg[2].aux) return 3;
    return -1;
}

static uint64_t mem_to_file(const nso_header_t *h, uint64_t addr) {
    for (int i = 0; i < 3; i++)
        if (addr >= h->seg[i].dst_off && addr < h->seg[i].dst_off + h->seg[i].size)
            return h->seg[i].file_off + (addr - h->seg[i].dst_off);
    return 0;
}

/* ------------------------------------------------------------------ */
/* default import trap                                                 */
/* ------------------------------------------------------------------ */

static uint64_t nso_trap(uint64_t a0, uint64_t a1, uint64_t a2,
                          uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    return 0;
}

/* ------------------------------------------------------------------ */
/* relocation table discovery                                          */
/* ------------------------------------------------------------------ */

static uint64_t find_reloc_table(const nso_header_t *h, const uint8_t *file,
                                 size_t fsize, size_t *out_count) {
    uint64_t best = 0, best_count = 0;
    for (size_t phase = 0; phase < 3; phase++) {
        for (size_t s = 0x100 + phase * 8; s + 24 * 8 <= fsize; s += 8) {
            uint64_t count = 0;
            for (size_t k = 0; s + k * 24 + 24 <= fsize; k++) {
                uint64_t info = rd64(file + s + k * 24 + 8);
                uint32_t t = (uint32_t)(info & 0xffffffffu);
                if (t != R_AARCH64_ABS64 && t != R_AARCH64_ABS32 &&
                    t != R_AARCH64_GLOB_DAT && t != R_AARCH64_JUMP_SLOT &&
                    t != R_AARCH64_RELATIVE)
                    break;
                if (seg_index_of(h, rd64(file + s + k * 24)) < 0) break;
                count++;
                if (count == 8) break;
            }
            if (count < 8) continue;
            while (s + count * 24 + 24 <= fsize) {
                uint64_t info = rd64(file + s + count * 24 + 8);
                uint32_t t = (uint32_t)(info & 0xffffffffu);
                if (t != R_AARCH64_ABS64 && t != R_AARCH64_ABS32 &&
                    t != R_AARCH64_GLOB_DAT && t != R_AARCH64_JUMP_SLOT &&
                    t != R_AARCH64_RELATIVE)
                    break;
                if (seg_index_of(h, rd64(file + s + count * 24)) < 0) break;
                count++;
            }
            if (count > best_count) { best_count = count; best = s; }
        }
    }
    *out_count = (size_t)best_count;
    return best;
}

/* ------------------------------------------------------------------ */
/* load                                                                */
/* ------------------------------------------------------------------ */

static void set_err(char *buf, size_t len, const char *msg) {
    if (buf && len) snprintf(buf, len, "%s", msg);
}

nso_module *nso_load_file(const char *path, nso_import_fn import_fn,
                          char *errbuf, size_t errlen) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) { set_err(errbuf, errlen, "cannot open NSO"); return NULL; }
    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); set_err(errbuf, errlen, "fstat failed"); return NULL; }
    size_t fsize = (size_t)st.st_size;
    uint8_t *file = mmap(NULL, fsize, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (file == MAP_FAILED) { set_err(errbuf, errlen, "mmap of file failed"); return NULL; }

    if (fsize < sizeof(nso_header_t)) { munmap(file, fsize); set_err(errbuf, errlen, "file too small"); return NULL; }
    nso_header_t h;
    memcpy(&h, file, sizeof(h));
    if (h.magic != NSO_MAGIC) { munmap(file, fsize); set_err(errbuf, errlen, "bad NSO0 magic"); return NULL; }

    nso_module *m = calloc(1, sizeof(*m));
    if (!m) { munmap(file, fsize); set_err(errbuf, errlen, "oom"); return NULL; }
    m->path = strdup(path);
    memcpy(m->build_id, h.build_id, 16);
    m->import_fn = import_fn;

    /* --- dynamic tables (offsets are relative to the .rodata start) --- */
    uint64_t dso = h.dynstr_extents & 0xffffffffu, dss = h.dynstr_extents >> 32;
    uint64_t dyo = h.dynsym_extents & 0xffffffffu, dys = h.dynsym_extents >> 32;
    if (!dss || !dys || dys % 24) {
        munmap(file, fsize); free((void *)m->path); free(m);
        set_err(errbuf, errlen, "dynstr/dynsym extents missing from NSO header");
        return NULL;
    }
    uint64_t base = h.seg[1].dst_off;
    uint64_t fds = mem_to_file(&h, base + dso), fdy = mem_to_file(&h, base + dyo);
    if (!fds || !fdy || fds + dss > fsize || fdy + dys > fsize) {
        munmap(file, fsize); free((void *)m->path); free(m);
        set_err(errbuf, errlen, "dynstr/dynsym out of file range");
        return NULL;
    }
    m->dynstr = (const char *)(file + fds);
    m->dynstr_size = (uint32_t)dss;
    m->sym_count = (uint32_t)(dys / 24);
    m->sym_name = calloc(m->sym_count, sizeof(uint32_t));
    m->sym_shndx = calloc(m->sym_count, sizeof(uint16_t));
    m->sym_value = calloc(m->sym_count, sizeof(uint64_t));
    if (!m->sym_name || !m->sym_shndx || !m->sym_value) {
        munmap(file, fsize); free((void *)m->path); free(m);
        set_err(errbuf, errlen, "oom"); return NULL;
    }
    for (uint32_t i = 0; i < m->sym_count; i++) {
        const uint8_t *e = file + fdy + i * 24;
        m->sym_name[i] = rd32(e);
        m->sym_shndx[i] = rd16(e + 6);
        m->sym_value[i] = rd64(e + 8);
    }

    /* --- import list --- */
    m->imports = calloc(m->sym_count, sizeof(uint64_t));
    m->import_names = calloc(m->sym_count, sizeof(char *));
    if (!m->imports || !m->import_names) {
        munmap(file, fsize); free((void *)m->path); free(m);
        set_err(errbuf, errlen, "oom"); return NULL;
    }
    for (uint32_t i = 1; i < m->sym_count; i++) {
        if (m->sym_shndx[i] != 0) continue;   /* defined in this module */
        const char *name = m->dynstr + m->sym_name[i];
        m->import_names[i] = strdup(name);
        void *host = dlsym(RTLD_DEFAULT, name);
        m->imports[i] = host ? (uint64_t)(uintptr_t)host : (uint64_t)(uintptr_t)&nso_trap;
        m->import_count++;
    }

    /* --- relocation table --- */
    size_t rcount = 0;
    uint64_t rbase = find_reloc_table(&h, file, fsize, &rcount);
    if (!rcount) {
        munmap(file, fsize); free((void *)m->path); free(m);
        set_err(errbuf, errlen, "relocation table not found");
        return NULL;
    }
    m->reloc_base = rbase;
    m->reloc_count = rcount;

    /* --- map the module image contiguously --- */
    uint64_t data_end = (uint64_t)h.seg[2].dst_off + h.seg[2].size;
    uint64_t image_size = data_end + h.seg[2].aux;
    image_size = page_up(image_size);
    uint8_t *image = mmap(NULL, (size_t)image_size, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (image == MAP_FAILED) {
        munmap(file, fsize); free((void *)m->path); free(m);
        set_err(errbuf, errlen, "mmap of module image failed");
        return NULL;
    }
    m->image = image;
    m->image_size = (size_t)image_size;

    for (int i = 0; i < 3; i++) {
        if (h.seg[i].file_off + h.seg[i].size > fsize) {
            set_err(errbuf, errlen, "segment out of file range");
            return NULL;
        }
        memcpy(image + h.seg[i].dst_off, file + h.seg[i].file_off, h.seg[i].size);
    }

    /* --- apply relocations --- */
    for (size_t k = 0; k < rcount; k++) {
        const uint8_t *e = file + rbase + k * 24;
        uint64_t r_offset = rd64(e);
        uint64_t r_info = rd64(e + 8);
        int64_t r_addend = (int64_t)rd64(e + 16);
        uint32_t type = (uint32_t)(r_info & 0xffffffffu);
        uint32_t sym = (uint32_t)(r_info >> 32);
        if (r_offset + 8 > image_size) continue;

        uint64_t value;
        if (type == R_AARCH64_RELATIVE) {
            value = (uint64_t)(uintptr_t)image + r_offset + (uint64_t)r_addend;
        } else if (sym < m->sym_count && m->sym_shndx[sym] == 0) {
            /* import: bind to the resolved host function (or trap) */
            uint64_t fn = m->imports[sym];
            if (type == R_AARCH64_ABS32) {
                *(uint32_t *)(image + r_offset) = (uint32_t)fn;
            } else {
                *(uint64_t *)(image + r_offset) = fn;
            }
            continue;
        } else if (sym < m->sym_count) {
            /* defined symbol: module-relative address */
            uint64_t addr = (uint64_t)(uintptr_t)image + m->sym_value[sym];
            value = (type == R_AARCH64_ABS32) ? (uint32_t)addr
                                               : addr + (uint64_t)r_addend;
        } else {
            continue;
        }
        if (type == R_AARCH64_ABS32)
            *(uint32_t *)(image + r_offset) = (uint32_t)value;
        else
            *(uint64_t *)(image + r_offset) = value;
    }

    /* --- page protection: data first, text last so text wins shared pages --- */
    uint64_t ro_lo = h.seg[1].dst_off, ro_hi = ro_lo + h.seg[1].size;
    uint64_t da_lo = h.seg[2].dst_off, da_hi = data_end + h.seg[2].aux;
    uint64_t tx_hi = h.seg[0].dst_off + h.seg[0].size;
    if (ro_hi > ro_lo)
        mprotect(image + page_down(ro_lo), (size_t)(page_up(ro_hi) - page_down(ro_lo)), PROT_READ);
    if (da_hi > da_lo)
        mprotect(image + page_down(da_lo), (size_t)(page_up(da_hi) - page_down(da_lo)), PROT_READ | PROT_WRITE);
    if (tx_hi)
        mprotect(image, (size_t)page_up(tx_hi), PROT_READ | PROT_EXEC);

    munmap(file, fsize);
    return m;
}

void nso_free(nso_module *m) {
    if (!m) return;
    if (m->image) munmap(m->image, m->image_size);
    for (uint32_t i = 0; i < m->sym_count; i++) free(m->import_names[i]);
    free(m->import_names);
    free(m->imports);
    free(m->sym_name); free(m->sym_shndx); free(m->sym_value);
    free((void *)m->path);
    free(m);
}

uint64_t nso_base(const nso_module *m) { return (uint64_t)(uintptr_t)m->image; }
uint64_t nso_addr(const nso_module *m, uint64_t off) { return (uint64_t)(uintptr_t)(m->image + off); }
uint32_t nso_import_count(const nso_module *m) { return m->import_count; }
uint64_t nso_reloc_count(const nso_module *m) { return m->reloc_count; }

const char *nso_symbol_name(const nso_module *m, uint32_t index) {
    if (index >= m->sym_count) return NULL;
    return m->dynstr + m->sym_name[index];
}

uint64_t nso_symbol_value(const nso_module *m, uint32_t index) {
    if (index >= m->sym_count) return UINT64_MAX;
    if (m->sym_shndx[index] == 0) return UINT64_MAX;
    return m->sym_value[index];
}

uint32_t nso_find_symbol(const nso_module *m, const char *name) {
    if (!name) return 0;
    for (uint32_t i = 1; i < m->sym_count; i++) {
        const char *s = m->dynstr + m->sym_name[i];
        if (!strcmp(s, name)) return i;
    }
    return 0;
}

void nso_build_id(const nso_module *m, char out[33]) {
    /* The NSO header stores the build id little-endian; Nintendo displays it
     * big-endian, which is the form T00 recorded. */
    /* Nintendo displays the build id as four big-endian 32-bit words. */
    for (int i = 0; i < 4; i++)
        snprintf(out + i * 8, 9, "%08x",
                 ((uint32_t)m->build_id[i * 4] << 24) | ((uint32_t)m->build_id[i * 4 + 1] << 16) |
                 ((uint32_t)m->build_id[i * 4 + 2] << 8) | (uint32_t)m->build_id[i * 4 + 3]);
    out[32] = 0;
}

uint64_t nso_call(nso_module *m, uint64_t module_offset,
                  uint64_t a0, uint64_t a1, uint64_t a2,
                  uint64_t a3, uint64_t a4, uint64_t a5) {
    typedef uint64_t (*fn_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
    fn_t fn = (fn_t)(m->image + module_offset);
    return fn(a0, a1, a2, a3, a4, a5);
}

/* ------------------------------------------------------------------ */
/* svc interception                                                    */
/* ------------------------------------------------------------------ */

static int g_svc_verbose;

static void svc_handler(int sig, siginfo_t *si, void *uc) {
    (void)sig; (void)si;
    if (g_svc_verbose) {
        fprintf(stderr, "[svc] intercepted at pc=%p\n", uc);
    }
    /* Return zero for the syscall and continue. */
    if (uc) {
        /* ucontext_t layout differs per platform; handled by the caller-side
         * hook below on Darwin/Linux. Kept minimal here. */
    }
}

/* Report a faulting PC relative to the module image, so crashes inside the
 * game code can be attributed to a module offset instead of a raw address. */
static nso_module *g_fault_module;

static void fault_handler(int sig, siginfo_t *si, void *uc) {
    uint64_t pc = 0, fa = 0;
#if defined(__APPLE__)
    ucontext_t *u = (ucontext_t *)uc;
    pc = (uint64_t)u->uc_mcontext->__ss.__pc;
    fa = si ? (uint64_t)si->si_addr : 0;
#elif defined(__linux__)
    ucontext_t *u = (ucontext_t *)uc;
    pc = (uint64_t)u->uc_mcontext.gregs[REG_PC];
    fa = (uint64_t)u->uc_mcontext.gregs[REG_SIGINFO];
    if (si) fa = (uint64_t)si->si_addr;
#endif
    (void)si;
    fprintf(stderr, "[fault] signal=%d pc=0x%llx fa=0x%llx", sig,
            (unsigned long long)pc, (unsigned long long)fa);
    if (g_fault_module && pc >= nso_base(g_fault_module) &&
        pc < nso_base(g_fault_module) + g_fault_module->image_size)
        fprintf(stderr, " module_offset=0x%llx",
                (unsigned long long)(pc - nso_base(g_fault_module)));
    fprintf(stderr, "\n");
    fflush(stderr);
    _exit(128 + sig);
}

void nso_install_fault_trap(nso_module *m) {
    g_fault_module = m;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = fault_handler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGTRAP, &sa, NULL);
}

void nso_enable_svc_trap(int verbose) {
    g_svc_verbose = verbose;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = svc_handler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGILL, &sa, NULL);
}
