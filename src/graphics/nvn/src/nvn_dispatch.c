/* Native NVN symbol boundary: never substitute an untyped success stub. */
#include "nvn/nvn_dispatch.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define CAPACITY 1024
#define NAME_SIZE 160
typedef struct { char name[NAME_SIZE]; PFNNVNGENERICFUNCPTRPROC fn; } Entry;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static Entry entries[CAPACITY];
static size_t entry_count;
static uint64_t lookups, missing;
static nvn_missing_symbol_fn missing_callback;
static void *missing_user;
static _Thread_local char last_missing[NAME_SIZE];

static int valid_name(const char *name) {
    return name && !strncmp(name, "nvn", 3) && name[3] &&
           strnlen(name, NAME_SIZE) < NAME_SIZE;
}
static PFNNVNGENERICFUNCPTRPROC device_get_proc(const NVNdevice *device, const char *name) {
    (void)device;
    return nvnBootstrapLoader(name);
}
static void report_missing(const char *name) {
    nvn_missing_symbol_fn callback;
    void *user;
    pthread_mutex_lock(&lock);
    ++missing;
    callback = missing_callback;
    user = missing_user;
    pthread_mutex_unlock(&lock);
    snprintf(last_missing, sizeof(last_missing), "%s", name ? name : "<null>");
    /* Callbacks may resolve symbols, so never invoke them under the lock. */
    if (callback) callback(last_missing, user);
    else fprintf(stderr, "[nvn-dispatch] unsupported symbol: %s\n", last_missing);
    errno = ENOSYS;
}
PFNNVNGENERICFUNCPTRPROC nvnBootstrapLoader(const char *name) {
    PFNNVNGENERICFUNCPTRPROC result = NULL;
    pthread_mutex_lock(&lock);
    ++lookups;
    if (valid_name(name)) {
        for (size_t i = 0; i < entry_count; ++i)
            if (!strcmp(name, entries[i].name)) { result = entries[i].fn; break; }
        if (!result && !strcmp(name, "nvnDeviceGetProcAddress"))
            result = (PFNNVNGENERICFUNCPTRPROC)device_get_proc;
        else if (!result && !strcmp(name, "nvnBootstrapLoader"))
            result = (PFNNVNGENERICFUNCPTRPROC)nvnBootstrapLoader;
    }
    pthread_mutex_unlock(&lock);
    if (!result) report_missing(name);
    return result;
}
int nvn_dispatch_register(const nvn_dispatch_entry *batch, size_t count) {
    if ((!batch && count) || count > CAPACITY) return EINVAL;
    pthread_mutex_lock(&lock);
    if (count > CAPACITY - entry_count) { pthread_mutex_unlock(&lock); return ENOSPC; }
    /* Validate all entries before publishing any. Pointers stay stable for the
     * process lifetime; the backend owns the declared function ABI. */
    for (size_t i = 0; i < count; ++i) {
        const char *name = batch[i].name;
        if (!valid_name(name) || !batch[i].function ||
            !strcmp(name, "nvnDeviceGetProcAddress") || !strcmp(name, "nvnBootstrapLoader")) {
            pthread_mutex_unlock(&lock); return EINVAL;
        }
        for (size_t j = 0; j < entry_count; ++j)
            if (!strcmp(name, entries[j].name)) { pthread_mutex_unlock(&lock); return EEXIST; }
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(name, batch[j].name)) { pthread_mutex_unlock(&lock); return EEXIST; }
    }
    for (size_t i = 0; i < count; ++i) {
        strcpy(entries[entry_count].name, batch[i].name);
        entries[entry_count++].fn = batch[i].function;
    }
    pthread_mutex_unlock(&lock);
    return 0;
}
void nvn_dispatch_set_missing_callback(nvn_missing_symbol_fn callback, void *user) {
    pthread_mutex_lock(&lock);
    missing_callback = callback; missing_user = user;
    pthread_mutex_unlock(&lock);
}
void nvn_dispatch_get_stats(nvn_dispatch_stats *stats) {
    if (!stats) return;
    pthread_mutex_lock(&lock);
    stats->registered = entry_count; stats->lookups = lookups; stats->missing = missing;
    pthread_mutex_unlock(&lock);
}
const char *nvn_dispatch_last_missing(void) { return last_missing; }
void nvn_dispatch_init(void) {
    /* Idempotent: do not discard a live backend's registered function pointers. */
}
