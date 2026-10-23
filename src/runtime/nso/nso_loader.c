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
#define MOD0_MAGIC 0x30444F4Du

#define R_AARCH64_ABS64      0x101u
#define R_AARCH64_ABS32      0x102u
#define R_AARCH64_GLOB_DAT   0x401u
#define R_AARCH64_JUMP_SLOT  0x402u
#define R_AARCH64_RELATIVE   0x403u

/* Standard ELF Dynamic Tags */
#define DT_NULL         0
#define DT_PLTRELSZ     2
#define DT_STRTAB       5
#define DT_SYMTAB       6
#define DT_RELA         7
#define DT_RELASZ       8
#define DT_RELAENT      9
#define DT_STRSZ        10
#define DT_SYMENT       11
#define DT_INIT         12
#define DT_FINI         13
#define DT_JMPREL       23
#define DT_INIT_ARRAY   25
#define DT_FINI_ARRAY   26
#define DT_INIT_ARRAYSZ 27
#define DT_FINI_ARRAYSZ 28
#define DT_RELRSZ       35
#define DT_RELR         36
#define DT_RELRENT      37

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

    uint64_t rela_offset;
    uint64_t rela_size;
    uint64_t jmprel_offset;
    uint64_t jmprel_size;
    uint64_t relr_offset;
    uint64_t relr_size;

    uint64_t init_offset;
    uint64_t init_array_offset;
    uint64_t init_array_size;

    uint64_t reloc_count;   /* count of RELA entries */
    uint64_t relr_count;    /* count of RELR targets resolved */

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

static void set_err(char *errbuf, size_t errlen, const char *msg) {
    if (errbuf && errlen) {
        snprintf(errbuf, errlen, "%s", msg);
    }
}

/* ------------------------------------------------------------------ */
/* default import trap                                                 */
/* ------------------------------------------------------------------ */

static uint64_t nso_trap(uint32_t idx, const char *name,
                         uint64_t a0, uint64_t a1, uint64_t a2,
                         uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)idx; (void)name;
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    return 0;
}

/* ------------------------------------------------------------------ */
/* loader implementation                                               */
/* ------------------------------------------------------------------ */

nso_module *nso_load_file(const char *path, nso_import_fn import_fn, char *errbuf, size_t errlen) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        set_err(errbuf, errlen, "cannot open file");
        return NULL;
    }
    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        set_err(errbuf, errlen, "cannot fstat file");
        return NULL;
    }
    size_t fsize = (size_t)st.st_size;
    const uint8_t *file = (const uint8_t *)mmap(NULL, fsize, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (file == MAP_FAILED) {
        set_err(errbuf, errlen, "mmap of file failed");
        return NULL;
    }

    if (fsize < sizeof(nso_header_t)) {
        munmap((void *)file, fsize);
        set_err(errbuf, errlen, "file smaller than NSO header");
        return NULL;
    }

    nso_header_t h;
    memcpy(&h, file, sizeof(h));
    if (h.magic != NSO_MAGIC) {
        munmap((void *)file, fsize);
        set_err(errbuf, errlen, "not an NSO0 file");
        return NULL;
    }

    nso_module *m = (nso_module *)calloc(1, sizeof(*m));
    if (!m) {
        munmap((void *)file, fsize);
        set_err(errbuf, errlen, "out of memory for module struct");
        return NULL;
    }
    m->path = strdup(path);
    m->import_fn = import_fn ? import_fn : nso_trap;
    memcpy(m->build_id, h.build_id, 16);

    /* --- locate MOD0 and .dynamic section --- */
    uint32_t text_file_off = h.seg[0].file_off;
    if (text_file_off + 8 > fsize) {
        munmap((void *)file, fsize); free((void *)m->path); free(m);
        set_err(errbuf, errlen, "text section out of bounds");
        return NULL;
    }
    uint32_t mod0_off = rd32(file + text_file_off + 4);
    if (text_file_off + mod0_off + 16 > fsize) {
        munmap((void *)file, fsize); free((void *)m->path); free(m);
        set_err(errbuf, errlen, "MOD0 header out of file bounds");
        return NULL;
    }
    const uint8_t *mod0 = file + text_file_off + mod0_off;
    if (rd32(mod0) != MOD0_MAGIC) {
        munmap((void *)file, fsize); free((void *)m->path); free(m);
        set_err(errbuf, errlen, "MOD0 magic mismatch");
        return NULL;
    }
    int32_t dyn_off = (int32_t)rd32(mod0 + 4);
    uint64_t dyn_mem_addr = (uint64_t)mod0_off + (uint64_t)dyn_off;
    uint64_t dyn_file_off = text_file_off + dyn_mem_addr;
    if (dyn_file_off + 16 > fsize) {
        munmap((void *)file, fsize); free((void *)m->path); free(m);
        set_err(errbuf, errlen, ".dynamic out of file bounds");
        return NULL;
    }

    /* Parse .dynamic entries */
    uint64_t symtab_mem = 0, strtab_mem = 0, strtab_sz = 0;
    for (uint64_t cur = dyn_file_off; cur + 16 <= fsize; cur += 16) {
        int64_t tag = (int64_t)rd64(file + cur);
        uint64_t val = rd64(file + cur + 8);
        if (tag == DT_NULL) break;
        switch (tag) {
            case DT_RELA:         m->rela_offset = val; break;
            case DT_RELASZ:       m->rela_size = val; break;
            case DT_JMPREL:       m->jmprel_offset = val; break;
            case DT_PLTRELSZ:     m->jmprel_size = val; break;
            case DT_RELR:         m->relr_offset = val; break;
            case DT_RELRSZ:       m->relr_size = val; break;
            case DT_SYMTAB:       symtab_mem = val; break;
            case DT_STRTAB:       strtab_mem = val; break;
            case DT_STRSZ:        strtab_sz = val; break;
            case DT_INIT:         m->init_offset = val; break;
            case DT_INIT_ARRAY:   m->init_array_offset = val; break;
            case DT_INIT_ARRAYSZ: m->init_array_size = val; break;
            default: break;
        }
    }

    if (!strtab_mem || !strtab_sz || !symtab_mem) {
        munmap((void *)file, fsize); free((void *)m->path); free(m);
        set_err(errbuf, errlen, "dynamic symbol/string table missing in .dynamic");
        return NULL;
    }

    /* Convert dynstr & dynsym memory offsets to file offsets */
    /* .rodata dst_off is segment 1 */
    uint64_t ro_dst = h.seg[1].dst_off;
    uint64_t ro_file = h.seg[1].file_off;
    uint64_t strtab_file = ro_file + (strtab_mem - ro_dst);
    uint64_t symtab_file = ro_file + (symtab_mem - ro_dst);

    char *dynstr_buf = (char *)malloc(strtab_sz + 1);
    memcpy(dynstr_buf, file + strtab_file, strtab_sz);
    dynstr_buf[strtab_sz] = 0;
    m->dynstr = dynstr_buf;
    m->dynstr_size = (uint32_t)strtab_sz;

    /* Count dynsym entries up to dynstr or strtab */
    size_t sym_bytes = (strtab_mem > symtab_mem) ? (strtab_mem - symtab_mem) : 0;
    uint32_t nsyms = (uint32_t)(sym_bytes / 24);
    if (nsyms == 0) nsyms = 1000; /* fallback estimate */
    m->sym_count = nsyms;
    m->sym_name  = (uint32_t *)malloc(nsyms * sizeof(uint32_t));
    m->sym_shndx = (uint16_t *)malloc(nsyms * sizeof(uint16_t));
    m->sym_value = (uint64_t *)malloc(nsyms * sizeof(uint64_t));
    m->imports   = (uint64_t *)malloc(nsyms * sizeof(uint64_t));
    m->import_names = (char **)malloc(nsyms * sizeof(char *));

    for (uint32_t i = 0; i < nsyms; i++) {
        uint64_t eoff = symtab_file + (uint64_t)i * 24;
        if (eoff + 24 > fsize) { m->sym_count = i; break; }
        m->sym_name[i]  = rd32(file + eoff);
        m->sym_shndx[i] = rd16(file + eoff + 6);
        m->sym_value[i] = rd64(file + eoff + 8);

        if (m->sym_shndx[i] == 0 && m->sym_name[i] < m->dynstr_size) {
            const char *name = m->dynstr + m->sym_name[i];
            m->import_names[i] = strdup(name);
            void *host = dlsym(RTLD_DEFAULT, name);
            m->imports[i] = host ? (uint64_t)(uintptr_t)host : (uint64_t)(uintptr_t)&nso_trap;
            m->import_count++;
        } else {
            m->import_names[i] = NULL;
            m->imports[i] = 0;
        }
    }

    /* --- map the module image contiguously --- */
    uint64_t data_end = (uint64_t)h.seg[2].dst_off + h.seg[2].size;
    uint64_t image_size = data_end + h.seg[2].aux;
    image_size = page_up(image_size);
    uint8_t *image = (uint8_t *)mmap(NULL, (size_t)image_size, PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (image == MAP_FAILED) {
        munmap((void *)file, fsize); free((void *)m->path); free(m);
        set_err(errbuf, errlen, "mmap of module image failed");
        return NULL;
    }
    m->image = image;
    m->image_size = (size_t)image_size;
    uint64_t base = (uint64_t)(uintptr_t)image;

    for (int i = 0; i < 3; i++) {
        if (h.seg[i].file_off + h.seg[i].size > fsize) {
            munmap(image, (size_t)image_size);
            munmap((void *)file, fsize); free((void *)m->path); free(m);
            set_err(errbuf, errlen, "segment out of file range");
            return NULL;
        }
        memcpy(image + h.seg[i].dst_off, file + h.seg[i].file_off, h.seg[i].size);
    }

    /* --- STEP 1: apply compact relative relocations (DT_RELR) --- */
    if (m->relr_offset && m->relr_size) {
        const uint64_t *relr = (const uint64_t *)(image + m->relr_offset);
        size_t count = m->relr_size / 8;
        uint64_t where = 0;
        for (size_t i = 0; i < count; i++) {
            uint64_t entry = relr[i];
            if ((entry & 1) == 0) {
                where = entry;
                *(uint64_t *)(image + where) += base;
                m->relr_count++;
                where += 8;
            } else {
                for (int b = 1; b < 64; b++) {
                    if ((entry >> b) & 1) {
                        *(uint64_t *)(image + where + (b - 1) * 8) += base;
                        m->relr_count++;
                    }
                }
                where += (64 - 1) * 8;
            }
        }
    }

    /* --- STEP 2: apply RELA relocations (DT_RELA and DT_JMPREL) --- */
    for (int pass = 0; pass < 2; pass++) {
        uint64_t toff = (pass == 0) ? m->rela_offset : m->jmprel_offset;
        uint64_t tsz  = (pass == 0) ? m->rela_size : m->jmprel_size;
        if (!toff || !tsz) continue;

        size_t nentries = tsz / 24;
        for (size_t k = 0; k < nentries; k++) {
            const uint8_t *e = image + toff + k * 24;
            uint64_t r_offset = rd64(e);
            uint64_t r_info   = rd64(e + 8);
            int64_t  r_addend = (int64_t)rd64(e + 16);
            uint32_t type = (uint32_t)(r_info & 0xffffffffu);
            uint32_t sym  = (uint32_t)(r_info >> 32);

            if (r_offset + 8 > image_size) continue;

            uint64_t value = 0;
            if (type == R_AARCH64_RELATIVE) {
                value = base + r_offset + (uint64_t)r_addend;
            } else if (sym < m->sym_count && m->sym_shndx[sym] == 0) {
                /* import binding */
                uint64_t fn = m->imports[sym];
                if (type == R_AARCH64_ABS32) {
                    *(uint32_t *)(image + r_offset) = (uint32_t)fn;
                } else {
                    *(uint64_t *)(image + r_offset) = fn;
                }
                m->reloc_count++;
                continue;
            } else if (sym < m->sym_count) {
                /* defined symbol */
                uint64_t addr = base + m->sym_value[sym];
                value = (type == R_AARCH64_ABS32) ? (uint32_t)addr : (addr + (uint64_t)r_addend);
            } else {
                continue;
            }

            if (type == R_AARCH64_ABS32) {
                *(uint32_t *)(image + r_offset) = (uint32_t)value;
            } else {
                *(uint64_t *)(image + r_offset) = value;
            }
            m->reloc_count++;
        }
    }

    /* Flip segment page protections */
    uint64_t tx_lo = h.seg[0].dst_off, tx_hi = tx_lo + h.seg[0].size;
    uint64_t ro_lo = h.seg[1].dst_off, ro_hi = ro_lo + h.seg[1].size;
    uint64_t da_lo = h.seg[2].dst_off, da_hi = data_end + h.seg[2].aux;

    if (ro_hi > ro_lo)
        mprotect(image + page_down(ro_lo), (size_t)(page_up(ro_hi) - page_down(ro_lo)), PROT_READ);
    if (da_hi > da_lo)
        mprotect(image + page_down(da_lo), (size_t)(page_up(da_hi) - page_down(da_lo)), PROT_READ | PROT_WRITE);
    if (tx_hi)
        mprotect(image, (size_t)page_up(tx_hi), PROT_READ | PROT_EXEC);

    munmap((void *)file, fsize);
    return m;
}

void nso_free(nso_module *m) {
    if (!m) return;
    if (m->image) munmap(m->image, m->image_size);
    if (m->import_names) {
        for (uint32_t i = 0; i < m->sym_count; i++) free(m->import_names[i]);
        free(m->import_names);
    }
    free(m->imports);
    free(m->sym_name); free(m->sym_shndx); free(m->sym_value);
    free((void *)m->dynstr);
    free((void *)m->path);
    free(m);
}

uint64_t nso_base(const nso_module *m) { return (uint64_t)(uintptr_t)m->image; }
uint64_t nso_addr(const nso_module *m, uint64_t module_offset) { return (uint64_t)(uintptr_t)(m->image + module_offset); }
uint32_t nso_import_count(const nso_module *m) { return m->import_count; }
uint64_t nso_reloc_count(const nso_module *m) { return m->reloc_count; }
uint64_t nso_relr_count(const nso_module *m) { return m->relr_count; }

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

void nso_run_init(nso_module *m) {
    if (!m || !m->image) return;
    if (m->init_offset) {
        printf("[init] calling DT_INIT at module offset 0x%" PRIx64 "\n", m->init_offset);
        fflush(stdout);
        typedef void (*init_fn_t)(void);
        init_fn_t fn = (init_fn_t)(m->image + m->init_offset);
        fn();
        printf("[init] DT_INIT done\n");
        fflush(stdout);
    }
    if (m->init_array_offset && m->init_array_size) {
        typedef void (*init_fn_t)(void);
        init_fn_t *arr = (init_fn_t *)(m->image + m->init_array_offset);
        size_t count = m->init_array_size / sizeof(init_fn_t);
        printf("[init] calling DT_INIT_ARRAY (%zu initializers at offset 0x%" PRIx64 ")\n",
               count, m->init_array_offset);
        fflush(stdout);
        for (size_t i = 0; i < count; i++) {
            if (arr[i]) {
                uint64_t fn_off = (uint64_t)((const uint8_t *)arr[i] - m->image);
                printf("[init] [%zu/%zu] init fn at 0x%" PRIx64 "\n", i, count, fn_off);
                fflush(stdout);
                arr[i]();
            }
        }
        printf("[init] DT_INIT_ARRAY done\n");
        fflush(stdout);
    }
}

/* ------------------------------------------------------------------ */
/* fault trap handler                                                  */
/* ------------------------------------------------------------------ */

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

static int g_svc_verbose;
static void svc_handler(int sig, siginfo_t *si, void *uc) {
    (void)sig; (void)si; (void)uc;
    if (g_svc_verbose) fprintf(stderr, "[svc] intercepted\n");
}

void nso_enable_svc_trap(int verbose) {
    g_svc_verbose = verbose;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = svc_handler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGILL, &sa, NULL);
}
