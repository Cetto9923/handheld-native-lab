#include "nvn/nvn_dispatch.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
static unsigned misses;
static void on_missing(const char *name, void *user) {
    assert(user == &misses && name && *name);
    ++misses;
    assert(nvnBootstrapLoader("nvnDeviceGetProcAddress"));
}
static NVNboolean initialize(NVNdevice *device, const NVNdeviceBuilder *builder) {
    return device != NULL && builder != NULL;
}
static void *lookup_thread(void *unused) {
    (void)unused;
    for (unsigned i = 0; i < 1000; ++i)
        assert(nvnBootstrapLoader("nvnDeviceInitialize") == (PFNNVNGENERICFUNCPTRPROC)initialize);
    return NULL;
}
int main(void) {
    nvn_dispatch_init();
    nvn_dispatch_set_missing_callback(on_missing, &misses);
    assert(nvnBootstrapLoader("nvnDeviceInitialize") == NULL);
    assert(errno == ENOSYS && misses == 1);
    assert(!strcmp(nvn_dispatch_last_missing(), "nvnDeviceInitialize"));
    nvn_dispatch_entry batch[] = {
        {"nvnDeviceInitialize", (PFNNVNGENERICFUNCPTRPROC)initialize},
        {"nvnDeviceInitialize", (PFNNVNGENERICFUNCPTRPROC)initialize}
    };
    assert(nvn_dispatch_register(batch, 2) == EEXIST);
    nvn_dispatch_stats stats;
    nvn_dispatch_get_stats(&stats);
    assert(stats.registered == 0);
    assert(nvn_dispatch_register(batch, 1) == 0);
    assert(nvn_dispatch_register(batch, 1) == EEXIST);
    nvn_dispatch_init();
    NVNdevice device = {0};
    NVNdeviceBuilder builder = {0};
    PFNNVNDEVICEINITIALIZEPROC init = (PFNNVNDEVICEINITIALIZEPROC)nvnBootstrapLoader("nvnDeviceInitialize");
    assert(init(&device, &builder) && !init(NULL, &builder));
    PFNNVNDEVICEGETPROCADDRESSPROC get_proc =
        (PFNNVNDEVICEGETPROCADDRESSPROC)nvnBootstrapLoader("nvnDeviceGetProcAddress");
    assert(get_proc(&device, "nvnDeviceInitialize") == (PFNNVNGENERICFUNCPTRPROC)init);
    assert(get_proc(&device, "nvnCommandBufferDrawArrays") == NULL);
    assert(nvnBootstrapLoader(NULL) == NULL);
    assert(nvnBootstrapLoader("otherFunction") == NULL);
    assert(nvn_dispatch_register(NULL, 1) == EINVAL);
    nvn_dispatch_entry reserved = {"nvnBootstrapLoader", (PFNNVNGENERICFUNCPTRPROC)initialize};
    assert(nvn_dispatch_register(&reserved, 1) == EINVAL);
    char name[] = "nvnDeviceInitializeAlternate";
    nvn_dispatch_entry copied = {name, (PFNNVNGENERICFUNCPTRPROC)initialize};
    assert(nvn_dispatch_register(&copied, 1) == 0);
    name[3] = 'X';
    assert(nvnBootstrapLoader("nvnDeviceInitializeAlternate") == (PFNNVNGENERICFUNCPTRPROC)initialize);
    pthread_t threads[4];
    for (unsigned i = 0; i < 4; ++i) assert(!pthread_create(&threads[i], NULL, lookup_thread, NULL));
    for (unsigned i = 0; i < 4; ++i) assert(!pthread_join(threads[i], NULL));
    nvn_dispatch_get_stats(&stats);
    assert(stats.registered == 2 && stats.missing == 4 && stats.lookups >= 4000);
    puts("NVN dispatch ABI, atomic registration, missing APIs and concurrent lookups passed");
    return 0;
}
