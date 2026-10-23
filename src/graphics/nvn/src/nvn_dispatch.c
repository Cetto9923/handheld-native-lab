/*
 * nvn_dispatch.c - Dynamic symbol resolver and dispatch table for NVN API.
 * Generated from main.bin imported NVN symbol inventory (535 symbols).
 */
#include "nvn/nvn_dispatch.h"
#include <stdio.h>
#include <string.h>

static void nvn_stub_log(const char *name) {
    static int count = 0;
    if (count++ < 50) {
        fprintf(stderr, "[nvn-dispatch] stub call: %s\n", name);
    }
}

static NVNboolean stub_nvnDeviceInitialize(NVNdevice *device, const NVNdeviceBuilder *builder) {
    (void)device; (void)builder;
    nvn_stub_log("nvnDeviceInitialize");
    return 1;
}

static void stub_nvnDeviceFinalize(NVNdevice *device) {
    (void)device;
    nvn_stub_log("nvnDeviceFinalize");
}

static NVNgenericFuncPtrFunc stub_generic(void) {
    return NULL;
}

typedef struct {
    const char *name;
    NVNgenericFuncPtrFunc fn;
} NvnDispatchEntry;

static const NvnDispatchEntry g_dispatch_table[] = {
    {"nvnDeviceInitialize", (NVNgenericFuncPtrFunc)stub_nvnDeviceInitialize},
    {"nvnDeviceFinalize", (NVNgenericFuncPtrFunc)stub_nvnDeviceFinalize},
};

static const size_t g_dispatch_count = sizeof(g_dispatch_table) / sizeof(g_dispatch_table[0]);

NVNgenericFuncPtrFunc nvnBootstrapLoader(const char *name) {
    if (!name) return NULL;
    for (size_t i = 0; i < g_dispatch_count; i++) {
        if (!strcmp(name, g_dispatch_table[i].name)) {
            return g_dispatch_table[i].fn;
        }
    }
    /* Fallback stub */
    return (NVNgenericFuncPtrFunc)stub_generic;
}

void nvn_dispatch_init(void) {
    /* Ready for Vulkan backend registration */
}
