/* The legacy mtl_* ABI over the ported NVN-to-Vulkan engine.
 *
 * The engine (engine/) knows nothing about the NVN API: it takes command memory and a device and
 * issues Vulkan work. This file is the layer that drives it, and it is the port of the D2R
 * project's dm/gfx/vulkan/nvn_vk.c, which is the same job in that project.
 *
 * What was changed against the D2R original, and why:
 *
 *   - the D2R host's own headers are gone. The D2R file leaned on dm/host/dm.h (its log and its
 *     die()), dm/host/front.h (the screen's refresh rate), dm/gfx/nvn/nvn_mtl.h (the resource
 *     counter struct) and dm/gfx/nvn/nvn_pace.h (the present pacer). Here the log and the fatal
 *     path are the engine's own (xbg_logf, xbg_fatalf), the refresh rate is a callback in the
 *     configuration, the resource counter struct is declared in this file's header, and the pacer
 *     is the formula below.
 *   - the configuration and its entry point are named xbg_vk_config and xbg_vk_initialize instead
 *     of dm_vk_config and dm_vk_initialize: the "dm" prefix names the D2R project, not the ABI.
 *   - xbg_record_command() is not here. It belongs to the host contract, so it is in
 *     xbg_host_bridge.c next to the other host symbols.
 *
 * The mtl_* names are kept as they are: they are the ABI this file implements, and the D2R host
 * calls them by those names. The FFRS runtime does not have to use them; it may drive the engine
 * through xbg_submit() and xbg_present_texture() directly.
 */
#include "engine/xbg_game_internal.h"

#include "nvn_vk_adapter.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* The engine's configuration                                            */
/* ------------------------------------------------------------------ */

static int g_ready;
static int g_in_sub;
static int g_npool;
static uint64_t g_present_n;
static xbg_vk_config g_config;

static void vk_log(const char *format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[512];
    vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    xbg_logf("[vk] %s", buffer);
}

/* A failure that must not return into the guest. The engine's fatal handler is the host's: it may
 * terminate the runtime, and it never comes back. */
static void vk_die(const char *format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[512];
    vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    xbg_fatalf("%s", buffer);
}

/* The Vulkan replays every draw; there is no freeze-elision or extrapolation switch to latch. */
uint64_t mtl_host_latch(void) { return 0; }
void mtl_host_apply(uint64_t value) { (void)value; }

int xbg_vk_initialize(const xbg_vk_config *config, char *error, size_t bytes) {
    if (!config || config->size != sizeof(*config) || !config->game.present) {
        if (error && bytes) snprintf(error, bytes, "Invalid Vulkan config or present callback");
        return -1;
    }
    if (g_ready) {
        if (error && bytes) snprintf(error, bytes, "Vulkan adapter already initialized");
        return -1;
    }
    int r = xbg_game_initialize(&config->game, error, bytes);
    if (r) return r;
    r = xbg_game_set_sampler_mirror_clamp_to_edge(config->sampler_mirror_clamp_to_edge);
    if (r) {
        xbg_game_shutdown();
        return r;
    }
    g_config = *config;
    g_ready = 1;
    return 0;
}

void mtl_init(void) {
    if (!g_ready) vk_die("the Vulkan backend is not initialized (the surface, the device and the shader directory come first)");
}

/* ------------------------------------------------------------------ */
/* The guest's memory pools                                             */
/* ------------------------------------------------------------------ */

static pthread_mutex_t g_pool_mu = PTHREAD_MUTEX_INITIALIZER;
typedef struct { uint64_t lo, size; } pool_range;
static pool_range g_pools[4096];

void mtl_pool_add(const void *mem, uint64_t size) {
    mtl_init();
    if (!mem || !size || size > UINTPTR_MAX - (uintptr_t)mem) vk_die("invalid GPU pool");
    pthread_mutex_lock(&g_pool_mu);
    if (g_npool == 4096) { pthread_mutex_unlock(&g_pool_mu); vk_die("too many GPU pools"); }
    g_pools[g_npool++] = (pool_range){(uintptr_t)mem, size};
    pthread_mutex_unlock(&g_pool_mu);
    xbg_legacy_pool_add((void *)mem, size);
}

void mtl_pool_del(const void *mem) {
    xbg_legacy_pool_del((void *)mem);
    pthread_mutex_lock(&g_pool_mu);
    for (int i = 0; i < g_npool; i++)
        if (g_pools[i].lo == (uintptr_t)mem) { g_pools[i] = g_pools[--g_npool]; break; }
    pthread_mutex_unlock(&g_pool_mu);
}

/* The host memory behind a range of the guest's buffer address: the pool it belongs to and the
 * offset in it. Zero when the address belongs to no pool. */
int mtl_addr(uint64_t addr, uint64_t len, void **buf, uint64_t *off) {
    int ok = 0;
    *buf = NULL;
    *off = 0;
    pthread_mutex_lock(&g_pool_mu);
    for (int i = 0; i < g_npool; i++) {
        pool_range p = g_pools[i];
        if (addr >= p.lo && addr - p.lo <= p.size && len <= p.size - (addr - p.lo)) {
            *buf = (void *)p.lo;
            *off = addr - p.lo;
            ok = 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_pool_mu);
    return ok;
}

/* ------------------------------------------------------------------ */
/* One submission                                                       */
/* ------------------------------------------------------------------ */

static uint64_t *g_handles;
static size_t g_nhandles, g_cap;

typedef struct direct_record { struct direct_record *next; size_t size; uint8_t data[]; } direct_record;
static direct_record *g_direct;

static void append_handle(uint64_t h) {
    if (!g_in_sub) vk_die("an NVN command outside a submit");
    if (g_nhandles == g_cap) {
        g_cap = g_cap ? g_cap * 2 : 64;
        g_handles = realloc(g_handles, g_cap * sizeof(*g_handles));
        if (!g_handles) vk_die("out of memory for the submit's handles");
    }
    g_handles[g_nhandles++] = h;
}

/* A command the engine is to run at this point of the submission: a record of its own, in a
 * segment of its own, registered with the engine as command memory. */
static void direct(uint32_t op, const void *payload, size_t bytes) {
    size_t total = (8 + bytes + 7) & ~(size_t)7;
    direct_record *r = calloc(1, sizeof(*r) + total + 8);
    if (!r) vk_die("out of memory for a direct record");
    r->size = total + 8;
    r->next = g_direct;
    g_direct = r;
    op_hdr h = {op, (uint32_t)total}, end = {OP_END, 8};
    memcpy(r->data, &h, 8);
    memcpy(r->data + 8, payload, bytes);
    memcpy(r->data + total, &end, 8);
    xbg_legacy_pool_add(r->data, r->size);
    append_handle((uintptr_t)r->data);
}

void mtl_sub_begin(void) {
    mtl_init();
    if (g_in_sub) vk_die("an NVN submit re-entered");
    g_in_sub = 1;
    g_nhandles = 0;
}

void mtl_exec(const uint8_t *p) { append_handle((uintptr_t)p); }
void mtl_exec_rest(const uint64_t *h, int n) { (void)h; (void)n; }
void mtl_sub_index(const uint64_t *h, int n) { (void)h; (void)n; }

void mtl_sub_copy(uint64_t src, uint64_t dst, uint64_t n) {
    uint64_t p[] = {src, dst, n};
    direct(OP_COPY_B2B, p, sizeof(p));
}

/* Fill a buffer range with a 32-bit value, in the order of the submission (OP_CLEAR_BUF). */
void mtl_sub_fill(uint64_t dst, uint64_t n, uint32_t value) {
    p_clear_buf p = {dst, n, value, 0};
    direct(OP_CLEAR_BUF, &p, sizeof(p));
}

/* The deferred events are published before the guest reads them (the engine's gfx-defer-events). */
void mtl_event_read(const volatile uint32_t *val) { xbg_game_event_read((const void *)val); }

void mtl_sub_event(volatile uint32_t *val, uint32_t mode, uint32_t v) {
    struct { uint64_t a; uint32_t m, v; } p = {(uintptr_t)val, mode, v};
    direct(OP_SIGNAL_EVENT, &p, sizeof(p));
}

void mtl_sub_counter(uint64_t addr, uint32_t type) {
    uint64_t p[] = {type, addr};
    direct(OP_REPORT_COUNTER, p, sizeof(p));
}

uint64_t mtl_sub_commit(void) {
    if (!g_in_sub) vk_die("an NVN commit without a begin");
    xbg_submit((int)g_nhandles, g_handles);
    uint64_t f = xbg_legacy_last_fence();
    while (g_direct) {
        direct_record *r = g_direct;
        g_direct = r->next;
        xbg_legacy_pool_del(r->data);
        free(r);
    }
    g_in_sub = 0;
    g_nhandles = 0;
    return f;
}

uint64_t mtl_last_fence(void) { return xbg_legacy_last_fence(); }
int mtl_wait(uint64_t fence, uint64_t timeout) { return xbg_legacy_wait(fence, timeout); }
void mtl_finish(void) { xbg_legacy_finish(); }
void mtl_drain(void) { xbg_legacy_finish(); }

/* ------------------------------------------------------------------ */
/* The objects the guest registered                                     */
/* ------------------------------------------------------------------ */

void mtl_reg_tex(int id, const tex_t *t, const view_t *v) { xbg_legacy_register_texture(id, t, v); }
void mtl_reg_samp(int id, const samp_par *s) { xbg_legacy_register_sampler(id, s); }
void mtl_prog_release(prog_t *p) { xbg_legacy_prog_release(p); }
void mtl_tex_release(tex_t *t) { xbg_legacy_tex_release(t); }
void mtl_xv_window_tex(uint64_t serial) { (void)serial; }
void mtl_read_texels(const tex_t *t, unsigned level, unsigned layer, const NVNcopyRegion *r,
                     uint8_t *dst, size_t row, size_t img) {
    xbg_legacy_read_texels(t, level, layer, r, dst, row, img);
}

/* ------------------------------------------------------------------ */
/* The present                                                          */
/* ------------------------------------------------------------------ */

/* Vulkan has no presentedTime callback, so the CPU time when the present was queued stands in for
 * it; in steady state the difference of two neighbours is the frame interval. */
static double g_present_at[1024];

uint64_t mtl_present(const tex_t *t, double period, const int crop[4]) {
    win_t window = {0};
    window.interval = period > 0 ? 1 : 0;
    if (crop) memcpy(window.crop, crop, sizeof(window.crop));
    xbg_present_texture(&window, t);
    uint64_t n = ++g_present_n;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    g_present_at[n % 1024] = (double)ts.tv_sec + ts.tv_nsec * 1e-9;
    return n;
}

void mtl_present_wait(uint64_t ticket) { if (ticket > g_present_n) vk_die("an unknown present ticket"); }
void mtl_present_flush(void) { mtl_drain(); }
double mtl_presented_time(uint64_t n) {
    return n && n <= g_present_n && g_present_n - n < 1024 ? g_present_at[n % 1024] : 0;
}
uint64_t mtl_present_count(void) { return g_present_n; }
int mtl_has_window(void) { return g_config.has_window ? g_config.has_window(g_config.game.userdata) : 0; }

/* The refresh rate of the screen the window is on. The host's callback answers it when it has
 * one; the configured value is the fallback. */
double mtl_display_refresh(void) {
    double hz = 0;
    if (g_config.display_refresh) hz = g_config.display_refresh(g_config.game.userdata);
    return hz > 0 ? hz : (double)g_config.display_hz;
}

int mtl_display_hz(void) { return (int)(mtl_display_refresh() + 0.5); }

/* The present frame rate: the smallest tier among the integer fractions of the refresh rate that
 * is not below the target (60, or 30 in the power-saving mode the game asks for). */
static double pace_hz(double screen_hz, double floor_hz) {
    if (screen_hz <= 0 || floor_hz <= 0) return floor_hz;
    for (int k = 1; k < 64; k++) {
        double hz = screen_hz / k;
        if (hz < floor_hz - 1e-9) return screen_hz / (k - 1 ? k - 1 : 1);
    }
    return screen_hz;
}

double mtl_pace_hz(void) { return pace_hz(mtl_display_refresh(), mtl_fps_target()); }

__attribute__((weak)) int dm_fps_setting(void) { return 60; }
int mtl_fps_target(void) { return dm_fps_setting() == 30 ? 30 : 60; }
int mtl_link_active(void) { return 0; }
int mtl_link_wait_tick(int vsyncs, uint64_t timeout_ns) { (void)vsyncs; (void)timeout_ns; return 0; }
int mtl_warm_wait(int timeout) { (void)timeout; return 0; }

/* ------------------------------------------------------------------ */
/* The counters                                                         */
/* ------------------------------------------------------------------ */

static _Atomic uint64_t g_cur_frame;

void mtl_set_frame(uint64_t f) {
    atomic_store(&g_cur_frame, f);
    xbg_game_set_frame(f);
}

void mtl_ub_present(double iv) { (void)iv; }

double mtl_gpu_ms(uint64_t f) { return xbg_game_gpu_ms(f); }
int mtl_gpu_span(uint64_t f, uint64_t *t0, uint64_t *t1) { (void)f; *t0 = *t1 = 0; return 0; }
void mtl_stats(uint64_t *passes, uint64_t *draws) {
    *passes = 0;
    xbg_game_stats s;
    xbg_game_get_stats(&s);
    *draws = s.draws;
}
void mtl_args_stats(uint64_t *enc, uint64_t *reuse) { *enc = *reuse = 0; }
void mtl_use_stats(uint64_t *call, uint64_t *skip) { *call = *skip = 0; }
uint64_t mtl_ds_omit(void) { return 0; }
void mtl_bw(uint64_t *load, uint64_t *store, uint64_t *full) { *load = *store = *full = 0; }
uint64_t mtl_udc_saved_bytes(void) { return 0; }
uint64_t mtl_drawable_ns_take(void) { return 0; }
void mtl_pkg_stats(uint64_t *uploads, uint64_t *reuploads) {
    xbg_frame_counters c;
    xbg_game_frame_counters(&c);
    *uploads = c.uploads;
    *reuploads = c.reuploads;
}
uint64_t mtl_pkg_miss(void) { return 0; }
uint64_t mtl_upload_ns_take(void) {
    static _Atomic uint64_t taken;
    xbg_frame_counters c;
    xbg_game_frame_counters(&c);
    return c.upload_ns - atomic_exchange(&taken, c.upload_ns);
}
uint64_t mtl_upload_gpu_ns_take(void) { return 0; }
uint64_t mtl_upload_bytes(void) {
    xbg_frame_counters c;
    xbg_game_frame_counters(&c);
    return c.upload_bytes;
}
void mtl_create_stats(uint64_t *lib, uint64_t *pso) {
    xbg_frame_counters c;
    xbg_game_frame_counters(&c);
    *lib = c.shader_loads;
    *pso = c.pipelines;
}
void mtl_create_ns(uint64_t *lib_ns, uint64_t *pso_ns) {
    xbg_frame_counters c;
    xbg_game_frame_counters(&c);
    *lib_ns = c.shader_load_ns;
    *pso_ns = c.pipeline_ns;
}
void mtl_alloc_stats(uint64_t *waits, uint64_t *grown, int *n) { *waits = *grown = 0; *n = 0; }
void mtl_mem_stat(unsigned long long frame, unsigned long long rss_mb, int heap_ok,
                  unsigned long long heap_mb, unsigned long long heap_free_mb,
                  unsigned long long heap_maxblk_kb) {
    (void)frame; (void)rss_mb; (void)heap_ok; (void)heap_mb; (void)heap_free_mb; (void)heap_maxblk_kb;
}
void mtl_res_snapshot(xbg_vk_res_count *out) {
    if (!out) return;
    memset(out, 0, sizeof *out);
    out->pools = (uint32_t)g_npool;
}
void mtl_res_counts(uint32_t tick, const char *why) {
    xbg_game_stats s;
    xbg_game_get_stats(&s);
    vk_log("resource sample %u (%s): memory pools %d, draws %llu, dispatches %llu", tick, why ? why : "",
           g_npool, (unsigned long long)s.draws, (unsigned long long)s.dispatches);
}
void mtl_ui_ghost(int on, float px, float py, const float grid[4], float cx, float cy) {
    (void)on; (void)px; (void)py; (void)grid; (void)cx; (void)cy;
}
void mtl_ui_ghost_rect(float r[4]) { (void)r; }
int dm_nvn_ruler_truth(void) { return 0; }
void dm_gfx_shot_at(long k, const char *path) { (void)k; (void)path; }

/* ------------------------------------------------------------------ */
/* The entry points the reference's header declares and this port does */
/* not have a use for                                                   */
/* ------------------------------------------------------------------ */

/* The capacity of the texture descriptor pool. The engine reads the capacity out of the pool
 * record the stream binds (OP_SET_TEX_POOL), so there is nothing to set here. */
void mtl_set_tex_pool_n(int n) { (void)n; }

/* A pause in the present thread: a unit-test hook of the reference. */
void mtl_present_test_stall(uint64_t ns) { (void)ns; }

/* The draw and violation counters of the reference's unit test. */
void xv_test_counts(int *dc, int *viol) { if (dc) *dc = 0; if (viol) *viol = 0; }

/* The texture is created: the engine finds the texture through the descriptor pool the stream
 * binds, so there is no separate registration to do here. */
void mtl_tex_init(const tex_t *t) { (void)t; }

/* The guest wrote the texture from the CPU: the engine's copy of it is no longer the truth. */
void mtl_tex_cpu_written(const tex_t *t) { xbg_texture_invalidate(t); }

/* Whether the host texture of that serial is a render target or a shader-written one, so that it
 * is never uploaded from the guest's memory again. The engine keeps that fact per image; the
 * answer here is the conservative one. */
int mtl_tex_gpu_owned(uint64_t serial) { (void)serial; return 0; }

/* The packaged texture edges that were skipped: the engine uploads whole textures and has no
 * edge bookkeeping. */
uint64_t mtl_pkg_edge_skips(void) { return 0; }

/* The per-stage shader key of the reference is the sha256 of the control section and the code
 * area; the engine's key is the digest of the control section alone, so the reference's key
 * cannot be produced here. The answer is "no such stage". */
int prog_key(const prog_t *p, unsigned stage, uint8_t key[8]) {
    (void)p;
    (void)stage;
    if (key) memset(key, 0, 8);
    return 0;
}

