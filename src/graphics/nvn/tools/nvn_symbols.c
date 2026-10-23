/*
 * nvn_symbols - enumerate a Nintendo Switch NSO0 module's dynamic symbol table
 * and emit the symbols matching a name prefix (default: "nvn").
 *
 * Purpose: quantify the *real* NVN API surface a module exports, so the
 * NVN->Vulkan compatibility layer can be scoped from evidence rather than from
 * assumption. Pure byte-level observation; never executes target code.
 *
 * Usage:
 *   nvn_symbols <unpacked.nso> [--prefix STR] [--all] [--out FILE]
 *
 * Output (TSV, one row per matching *defined* symbol):
 *   idx  name  value  fileoff  size  bind  type  shndx
 *
 * `value` is the symbol's module-relative virtual address (NSO memory space).
 * `fileoff` is the corresponding offset inside the NSO file, or -1 when the
 * address does not map into a file-backed segment (e.g. .bss).
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define NSO_MAGIC 0x304F534Eu /* 'NSO0' little-endian */

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

static uint8_t *g_file;
static size_t g_size;
static nso_header_t H;

static uint32_t rd32(size_t off) { uint32_t v; memcpy(&v, g_file + off, 4); return v; }
static uint64_t rd64(size_t off) { uint64_t v; memcpy(&v, g_file + off, 8); return v; }

static int seg_of_mem(uint64_t addr) {
    for (int i = 0; i < 3; i++)
        if (addr >= H.seg[i].dst_off && addr < H.seg[i].dst_off + H.seg[i].size) return i;
    return -1;
}

static uint64_t mem_to_file(uint64_t addr) {
    for (int i = 0; i < 3; i++)
        if (addr >= H.seg[i].dst_off && addr < H.seg[i].dst_off + H.seg[i].size)
            return H.seg[i].file_off + (addr - H.seg[i].dst_off);
    return 0;
}

static int printable_ratio(const uint8_t *p, size_t n) {
    size_t ok = 0;
    for (size_t i = 0; i < n; i++) if (p[i] >= 0x20 && p[i] < 0x7f) ok++;
    return n ? (int)(ok * 100 / n) : 0;
}

typedef struct {
    uint64_t dynstr_off, dynstr_size, dynsym_off, dynsym_size;
    uint64_t base;
    int printable;
} tables_t;

static int resolve_tables(tables_t *t) {
    uint64_t dso = H.dynstr_extents & 0xffffffffu, dss = H.dynstr_extents >> 32;
    uint64_t dyo = H.dynsym_extents & 0xffffffffu, dys = H.dynsym_extents >> 32;
    if (!dss || !dys || dys % 24) return 0;
    uint64_t candidates[4] = {
        H.seg[1].dst_off,     /* .rodata start */
        0,                    /* module base */
        H.seg[1].dst_off + 0x98,
        H.seg[1].dst_off + 0x8c,
    };
    for (int i = 0; i < 4; i++) {
        uint64_t ds = candidates[i] + dso, dy = candidates[i] + dyo;
        uint64_t fds = mem_to_file(ds), fdy = mem_to_file(dy);
        if (!fds || !fdy || fds + dss > g_size || fdy + dys > g_size) continue;
        int pr = printable_ratio(g_file + fds, dss < 4096 ? dss : 4096);
        if (pr < 60) continue;
        t->base = candidates[i];
        t->dynstr_off = dso; t->dynstr_size = dss;
        t->dynsym_off = dyo; t->dynsym_size = dys;
        t->printable = pr;
        return 1;
    }
    return 0;
}

/* ELF64 STT_* / STB_* (the NSO dynsym is an ELF64 Sym table) */
static const char *sym_type_name(uint8_t info) {
    switch (info & 0xf) {
    case 0:  return "NOTYPE";
    case 1:  return "OBJECT";
    case 2:  return "FUNC";
    case 3:  return "SECTION";
    case 4:  return "FILE";
    case 10: return "GNU_IFUNC";
    default: return "other";
    }
}

static const char *sym_bind_name(uint8_t info) {
    switch (info >> 4) {
    case 0: return "LOCAL";
    case 1: return "GLOBAL";
    case 2: return "WEAK";
    default: return "other";
    }
}

int main(int argc, char **argv) {
    const char *path = NULL;
    const char *prefix = "nvn";
    const char *out_path = NULL;
    int want_all = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--prefix") && i + 1 < argc) prefix = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out_path = argv[++i];
        else if (!strcmp(argv[i], "--all")) want_all = 1;
        else path = argv[i];
    }

    if (!path) {
        fprintf(stderr, "usage: %s <unpacked.nso> [--prefix STR] [--all] [--out FILE]\n", argv[0]);
        return 2;
    }

    FILE *f = fopen(path, "rb");
    if (!f) { perror("open"); return 1; }
    fseek(f, 0, SEEK_END); g_size = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    g_file = malloc(g_size);
    if (!g_file || fread(g_file, 1, g_size, f) != g_size) { fprintf(stderr, "read failed\n"); return 1; }
    fclose(f);

    memcpy(&H, g_file, sizeof(H));
    if (H.magic != NSO_MAGIC) { fprintf(stderr, "bad magic 0x%08x\n", H.magic); return 1; }

    tables_t T;
    if (!resolve_tables(&T)) { fprintf(stderr, "could not resolve dynstr/dynsym\n"); return 1; }

    uint64_t fds = mem_to_file(T.base + T.dynstr_off);
    uint64_t fdy = mem_to_file(T.base + T.dynsym_off);
    const char *dynstr = (const char *)(g_file + fds);
    uint64_t nent = T.dynsym_size / 24;

    FILE *out = stdout;
    if (out_path) {
        out = fopen(out_path, "w");
        if (!out) { perror("open out"); return 1; }
    }

    fprintf(out, "# nvn_symbols\tfile=%s\tprefix=%s\tall=%d\n", path, prefix, want_all);
    fprintf(out, "idx\tname\tvalue\tfileoff\tsize\tbind\ttype\tshndx\n");

    uint64_t matched = 0, defined_total = 0;
    for (uint64_t i = 1; i < nent; i++) {
        size_t b = fdy + i * 24;
        uint32_t nm = rd32(b);
        uint8_t info = g_file[b + 4];
        uint16_t shndx = (uint16_t)(rd32(b + 6) & 0xffff);
        uint64_t value = rd64(b + 8);
        uint64_t size = rd64(b + 16);
        if (shndx == 0) continue; /* undefined => import */
        defined_total++;
        const char *name = dynstr + nm;
        if (!want_all && strncmp(name, prefix, strlen(prefix)) != 0) continue;
        matched++;

        int64_t fileoff = -1;
        if (seg_of_mem(value) >= 0) fileoff = (int64_t)mem_to_file(value);

        fprintf(out, "%" PRIu64 "\t%s\t0x%" PRIx64 "\t%" PRId64 "\t%" PRIu64 "\t%s\t%s\t%u\n",
                i, name, value, fileoff, size,
                sym_bind_name(info), sym_type_name(info), (unsigned)shndx);
    }

    fprintf(stderr, "== %s ==\n", path);
    fprintf(stderr, "  dynsym entries         : %" PRIu64 "\n", nent);
    fprintf(stderr, "  defined symbols (export): %" PRIu64 "\n", defined_total);
    fprintf(stderr, "  matching prefix '%s' : %" PRIu64 "\n", prefix, matched);

    if (out != stdout) fclose(out);
    free(g_file);
    return 0;
}
