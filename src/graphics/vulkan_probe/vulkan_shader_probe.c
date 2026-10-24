/* SPDX-License-Identifier: MIT
 * Standalone Vulkan shader-acceptance probe.
 *
 * It answers four separate questions and reports them separately, each with the
 * driver's own VkResult:
 *
 *   tier 1  device facts    - instance / physical device / feature support
 *   tier 2  shader module   - vkCreateShaderModule on the real game modules
 *   tier 3  pipeline        - vkCreate{Compute,Graphics}Pipeline on a real
 *                             VkPipeline with a descriptor set layout that is
 *                             derived from the game's reflection data and is
 *                             never empty
 *   tier 4  draw            - a real dispatch / draw that the GPU consumes
 *
 * The probe loads the Vulkan loader at run time with dlopen() and resolves the
 * entry points through vkGetInstanceProcAddr / vkGetDeviceProcAddr.  It does
 * not link against libvulkan, so the binary starts on a host without a Vulkan
 * loader and reports "no Vulkan in this build" instead of failing to link or
 * pretending that a device exists.
 *
 * The probe owns its instance and device; it never touches the runtime
 * backend (src/graphics/nvn_device/nvn_backend_vulkan.c) and never needs the
 * game to be running.
 */
#define _POSIX_C_SOURCE 200809L

#include <dlfcn.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <vulkan_core.h>

#ifndef FFRS_VK_SOURCE_SHA256
#define FFRS_VK_SOURCE_SHA256 "unfrozen"
#endif
#ifndef FFRS_VK_GIT_HEAD
#define FFRS_VK_GIT_HEAD "unknown"
#endif

#define FFRS_SCHEMA "ffrs.vulkan.shader.v1"

enum {
    MAX_MODULES = 32,
    MAX_BINDS = 64,
    MAX_PIPELINES = 16,
    MAX_STAGES = 512,
    MAX_EXTENSIONS = 256,
    IMAGE_WIDTH = 8,
    IMAGE_HEIGHT = 8,
    UNIFORM_BYTES = 256,
    MAX_DRAW_VERTICES = 3
};

static const uint64_t FENCE_TIMEOUT_NS = UINT64_C(10000000000);
static const char *const LOADER_CANDIDATES[] = {
    "libvulkan.so.1", "libvulkan.so", "libvulkan.1.dylib", "libvulkan.dylib", "libMoltenVK.dylib"
};
enum { LOADER_CANDIDATE_COUNT = (int)(sizeof(LOADER_CANDIDATES) / sizeof(LOADER_CANDIDATES[0])) };

/* ------------------------------------------------------------------ */
/* Vulkan entry points, resolved at run time.                          */
/* ------------------------------------------------------------------ */

/* Resolved with vkGetInstanceProcAddr(NULL, ...): global commands only. */
#define FFRS_VK_GLOBAL_FUNCTIONS(X)                                                     \
    X(vkCreateInstance, PFN_vkCreateInstance)                                           \
    X(vkEnumerateInstanceVersion, PFN_vkEnumerateInstanceVersion)

/* Resolved with vkGetInstanceProcAddr(instance, ...). */
#define FFRS_VK_INSTANCE_FUNCTIONS(X)                                                   \
    X(vkCreateDevice, PFN_vkCreateDevice)                                               \
    X(vkEnumeratePhysicalDevices, PFN_vkEnumeratePhysicalDevices)                       \
    X(vkGetPhysicalDeviceProperties, PFN_vkGetPhysicalDeviceProperties)                 \
    X(vkGetPhysicalDeviceFeatures, PFN_vkGetPhysicalDeviceFeatures)                     \
    X(vkGetPhysicalDeviceFeatures2, PFN_vkGetPhysicalDeviceFeatures2)                   \
    X(vkGetPhysicalDeviceQueueFamilyProperties, PFN_vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkGetPhysicalDeviceMemoryProperties, PFN_vkGetPhysicalDeviceMemoryProperties)     \
    X(vkEnumerateDeviceExtensionProperties, PFN_vkEnumerateDeviceExtensionProperties)   \
    X(vkGetDeviceProcAddr, PFN_vkGetDeviceProcAddr)                                     \
    X(vkDestroyInstance, PFN_vkDestroyInstance)

#define FFRS_VK_DEVICE_FUNCTIONS(X)                                                    \
    X(vkDestroyDevice, PFN_vkDestroyDevice)                                             \
    X(vkGetDeviceQueue, PFN_vkGetDeviceQueue)                                           \
    X(vkCreateShaderModule, PFN_vkCreateShaderModule)                                   \
    X(vkDestroyShaderModule, PFN_vkDestroyShaderModule)                                 \
    X(vkCreateDescriptorSetLayout, PFN_vkCreateDescriptorSetLayout)                     \
    X(vkDestroyDescriptorSetLayout, PFN_vkDestroyDescriptorSetLayout)                   \
    X(vkCreatePipelineLayout, PFN_vkCreatePipelineLayout)                               \
    X(vkDestroyPipelineLayout, PFN_vkDestroyPipelineLayout)                             \
    X(vkCreateComputePipelines, PFN_vkCreateComputePipelines)                             \
    X(vkCreateGraphicsPipelines, PFN_vkCreateGraphicsPipelines)                           \
    X(vkDestroyPipeline, PFN_vkDestroyPipeline)                                         \
    X(vkCreateSampler, PFN_vkCreateSampler)                                             \
    X(vkDestroySampler, PFN_vkDestroySampler)                                           \
    X(vkCreateImage, PFN_vkCreateImage)                                                 \
    X(vkDestroyImage, PFN_vkDestroyImage)                                               \
    X(vkCreateImageView, PFN_vkCreateImageView)                                         \
    X(vkDestroyImageView, PFN_vkDestroyImageView)                                       \
    X(vkAllocateMemory, PFN_vkAllocateMemory)                                           \
    X(vkFreeMemory, PFN_vkFreeMemory)                                                   \
    X(vkBindImageMemory, PFN_vkBindImageMemory)                                         \
    X(vkBindBufferMemory, PFN_vkBindBufferMemory)                                       \
    X(vkCreateBuffer, PFN_vkCreateBuffer)                                               \
    X(vkDestroyBuffer, PFN_vkDestroyBuffer)                                             \
    X(vkGetImageMemoryRequirements, PFN_vkGetImageMemoryRequirements)                   \
    X(vkGetBufferMemoryRequirements, PFN_vkGetBufferMemoryRequirements)                 \
    X(vkCreateDescriptorPool, PFN_vkCreateDescriptorPool)                               \
    X(vkDestroyDescriptorPool, PFN_vkDestroyDescriptorPool)                             \
    X(vkAllocateDescriptorSets, PFN_vkAllocateDescriptorSets)                           \
    X(vkUpdateDescriptorSets, PFN_vkUpdateDescriptorSets)                               \
    X(vkCreatePipelineCache, PFN_vkCreatePipelineCache)                                       \
    X(vkDestroyPipelineCache, PFN_vkDestroyPipelineCache)                                     \
    X(vkCreateRenderPass, PFN_vkCreateRenderPass)                                       \
    X(vkDestroyRenderPass, PFN_vkDestroyRenderPass)                                     \
    X(vkCreateFramebuffer, PFN_vkCreateFramebuffer)                                     \
    X(vkDestroyFramebuffer, PFN_vkDestroyFramebuffer)                                   \
    X(vkCreateCommandPool, PFN_vkCreateCommandPool)                                     \
    X(vkDestroyCommandPool, PFN_vkDestroyCommandPool)                                   \
    X(vkAllocateCommandBuffers, PFN_vkAllocateCommandBuffers)                           \
    X(vkBeginCommandBuffer, PFN_vkBeginCommandBuffer)                                   \
    X(vkEndCommandBuffer, PFN_vkEndCommandBuffer)                                       \
    X(vkCmdBeginRenderPass, PFN_vkCmdBeginRenderPass)                                   \
    X(vkCmdEndRenderPass, PFN_vkCmdEndRenderPass)                                       \
    X(vkCmdBindPipeline, PFN_vkCmdBindPipeline)                                         \
    X(vkCmdBindDescriptorSets, PFN_vkCmdBindDescriptorSets)                             \
    X(vkCmdDraw, PFN_vkCmdDraw)                                                         \
    X(vkCmdDispatch, PFN_vkCmdDispatch)                                                 \
    X(vkQueueSubmit, PFN_vkQueueSubmit)                                                 \
    X(vkWaitForFences, PFN_vkWaitForFences)                                             \
    X(vkCreateFence, PFN_vkCreateFence)                                                 \
    X(vkDestroyFence, PFN_vkDestroyFence)                                               \
    X(vkDeviceWaitIdle, PFN_vkDeviceWaitIdle)

#define FFRS_DECLARE(name, type) static type p_##name;
FFRS_VK_GLOBAL_FUNCTIONS(FFRS_DECLARE)
FFRS_VK_INSTANCE_FUNCTIONS(FFRS_DECLARE)
FFRS_VK_DEVICE_FUNCTIONS(FFRS_DECLARE)
#undef FFRS_DECLARE

static PFN_vkGetInstanceProcAddr p_vkGetInstanceProcAddr;
static void *g_loader;

/* ------------------------------------------------------------------ */
/* Plan model: the host side derives these from the game's reflection.  */
/* ------------------------------------------------------------------ */

enum {
    CLASS_UNIFORM_BUFFER = 0,
    CLASS_STORAGE_BUFFER,
    CLASS_COMBINED_IMAGE_SAMPLER,
    CLASS_SAMPLED_IMAGE,
    CLASS_STORAGE_IMAGE,
    CLASS_INPUT_ATTACHMENT,
    CLASS_COUNT
};

static const char *const CLASS_NAMES[CLASS_COUNT] = {
    "uniform_buffer", "storage_buffer", "combined_image_sampler",
    "sampled_image", "storage_image", "input_attachment"
};

/* The class enum is not the VkDescriptorType numbering; map it explicitly. */
static VkDescriptorType descriptor_type_of(int klass) {
    switch (klass) {
    case CLASS_UNIFORM_BUFFER: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case CLASS_STORAGE_BUFFER: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case CLASS_COMBINED_IMAGE_SAMPLER: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    case CLASS_SAMPLED_IMAGE: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case CLASS_STORAGE_IMAGE: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case CLASS_INPUT_ATTACHMENT: return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    default: return VK_DESCRIPTOR_TYPE_MAX_ENUM;
    }
}

typedef struct {
    uint32_t set;
    uint32_t binding;
    int klass;
    char provenance[16]; /* "reflection" or "spirv_only" */
} Bind;

typedef struct {
    char key[40];
    char variant[16];
    int stage; /* VK_SHADER_STAGE_*_BIT */
    char file[192];
    Bind binds[MAX_BINDS];
    size_t bind_count;
    VkShaderModule module;
    VkResult module_result;
    bool module_created;
    bool module_attempted;
    char message[160];
} Module;

typedef struct {
    char id[64];
    int module_index[2]; /* -1 when the pipeline has a single stage */
    Bind binds[MAX_BINDS];
    size_t bind_count;
    bool layout_conflict;
    VkDescriptorSetLayout layout;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
    VkResult layout_result;
    VkResult pipeline_result;
    bool pipeline_created;
    bool layout_attempted;
    bool pipeline_attempted;
    char message[160];
    VkDescriptorSet descriptor_set;
    bool draw_attempted;
    bool draw_consumed;
    VkResult submit_result;
    VkResult wait_result;
    char draw_reason[96];
} Pipeline;

typedef struct { const char *name; int result; } Stage;

typedef struct {
    void *loader;
    const char *loader_name;
    char loader_error[128];
    uint32_t instance_api_requested;
    uint32_t instance_api_supported;
    uint32_t instance_api_used;
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    uint32_t device_index;
    uint32_t queue_family;
    VkQueueFlags queue_flags;
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceMemoryProperties memory_properties;
    VkPhysicalDeviceFeatures features;
    VkPhysicalDeviceVulkan11Features features_11;
    VkPhysicalDeviceFeatures enabled_features;
    VkPhysicalDeviceVulkan11Features enabled_features_11;
    char extension_names[MAX_EXTENSIONS][128];
    uint32_t extension_count;
    bool fragment_shader_interlock_available;
    bool fragment_shader_interlock_enabled;
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;
    VkFence fence;
    VkRenderPass render_pass;
    VkPipelineCache pipeline_cache;
    VkFramebuffer framebuffer;
    VkImage color_image;
    VkDeviceMemory color_memory;
    VkImageView color_view;
    Module modules[MAX_MODULES];
    size_t module_count;
    Pipeline pipelines[MAX_PIPELINES];
    size_t pipeline_count;
    Stage stages[MAX_STAGES];
    size_t stage_count;
    const char *failure_stage;
    const char *failure;
    int exit_code;
    bool device_lost;
    uint32_t destroyed_objects;
} Probe;

static void watchdog(int signal_number) {
    (void)signal_number;
    static const char message[] =
        "{\"schema\":\"ffrs.vulkan.shader.v1\",\"status\":\"INCOMPLETE\",\"exit_code\":124,"
        "\"failure\":\"PROCESS_WATCHDOG_TIMEOUT\","
        "\"tiers\":{\"1_device_facts\":\"NOT_RUN\",\"2_shader_module\":\"NOT_RUN\","
        "\"3_pipeline\":\"NOT_RUN\",\"4_draw\":\"NOT_RUN\"}}\n";
    (void)write(STDOUT_FILENO, message, sizeof(message) - 1);
    _exit(124);
}

static bool fail(Probe *p, const char *stage, const char *reason, int exit_code) {
    if (!p->failure) {
        p->failure_stage = stage;
        p->failure = reason;
        p->exit_code = exit_code;
    }
    return false;
}

static void trace(const char *what) {
    fprintf(stderr, "[probe] %s\n", what);
}

static void stage(Probe *p, const char *name, VkResult value) {
    if (p->stage_count == MAX_STAGES) return;
    p->stages[p->stage_count].name = name;
    p->stages[p->stage_count].result = (int)value;
    ++p->stage_count;
}

static const char *result_name(int value) {
    switch (value) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_INCOMPLETE: return "VK_INCOMPLETE";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
    case VK_ERROR_INVALID_SHADER_NV: return "VK_ERROR_INVALID_SHADER_NV";
    default: return "VK_RESULT_OTHER";
    }
}

static const char *version_string(uint32_t version) {
    static char buffer[4][32];
    static size_t slot;
    char *out = buffer[slot];
    slot = (slot + 1) % 4;
    snprintf(out, 32, "%u.%u.%u", VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version),
             VK_VERSION_PATCH(version));
    return out;
}

static const char *device_type_name(VkPhysicalDeviceType type) {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "INTEGRATED_GPU";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "DISCRETE_GPU";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "VIRTUAL_GPU";
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return "CPU";
    default: return "OTHER";
    }
}

/* ------------------------------------------------------------------ */
/* Manifest parsing.                                                   */
/* ------------------------------------------------------------------ */

static Module *find_module(Probe *p, const char *key, const char *variant) {
    for (size_t i = 0; i < p->module_count; ++i) {
        if (!strcmp(p->modules[i].key, key) && !strcmp(p->modules[i].variant, variant))
            return &p->modules[i];
    }
    return NULL;
}

static int stage_from_name(const char *name) {
    if (!strcmp(name, "vert")) return VK_SHADER_STAGE_VERTEX_BIT;
    if (!strcmp(name, "frag")) return VK_SHADER_STAGE_FRAGMENT_BIT;
    if (!strcmp(name, "comp")) return VK_SHADER_STAGE_COMPUTE_BIT;
    return 0;
}

static int class_from_name(const char *name) {
    for (int i = 0; i < CLASS_COUNT; ++i)
        if (!strcmp(CLASS_NAMES[i], name)) return i;
    return -1;
}

static bool parse_manifest(Probe *p, const char *path) {
    FILE *file = fopen(path, "r");
    if (!file) {
        p->failure = "MANIFEST_NOT_FOUND";
        p->failure_stage = "parse_manifest";
        p->exit_code = 2;
        return false;
    }
    char line[512];
    while (fgets(line, (int)sizeof(line), file)) {
        char tag[32] = {0};
        if (sscanf(line, "%31s", tag) != 1 || tag[0] == '#') continue;
        if (!strcmp(tag, "module")) {
            char key[40], variant[16], stage_name[16], file_name[192];
            if (sscanf(line, "%*s %39s %15s %15s %191s", key, variant, stage_name, file_name) != 4) {
                fclose(file);
                return fail(p, "parse_manifest", "MALFORMED_MODULE_LINE", 2);
            }
            int stage = stage_from_name(stage_name);
            if (!stage || p->module_count == MAX_MODULES) {
                fclose(file);
                return fail(p, "parse_manifest", "UNKNOWN_STAGE_OR_MODULE_OVERFLOW", 2);
            }
            Module *m = &p->modules[p->module_count++];
            memset(m, 0, sizeof(*m));
            snprintf(m->key, sizeof(m->key), "%s", key);
            snprintf(m->variant, sizeof(m->variant), "%s", variant);
            snprintf(m->file, sizeof(m->file), "%s", file_name);
            m->stage = stage;
        } else if (!strcmp(tag, "binding")) {
            char key[40], variant[16], class_name[32], provenance[32];
            unsigned set, binding;
            if (sscanf(line, "%*s %39s %15s %u %u %31s %31s", key, variant, &set, &binding,
                       class_name, provenance) != 6) {
                fclose(file);
                return fail(p, "parse_manifest", "MALFORMED_BINDING_LINE", 2);
            }
            Module *m = find_module(p, key, variant);
            int klass = class_from_name(class_name);
            if (!m || klass < 0 || m->bind_count == MAX_BINDS) {
                fclose(file);
                return fail(p, "parse_manifest", "UNKNOWN_BINDING_TARGET", 2);
            }
            Bind *bind = &m->binds[m->bind_count++];
            bind->set = set;
            bind->binding = binding;
            bind->klass = klass;
            snprintf(bind->provenance, sizeof(bind->provenance), "%s", provenance);
        } else if (!strcmp(tag, "pipeline")) {
            char id[64], key1[40], var1[16], key2[40], var2[16];
            if (sscanf(line, "%*s %63s %39s %15s %39s %15s", id, key1, var1, key2, var2) != 5) {
                fclose(file);
                return fail(p, "parse_manifest", "MALFORMED_PIPELINE_LINE", 2);
            }
            if (p->pipeline_count == MAX_PIPELINES) {
                fclose(file);
                return fail(p, "parse_manifest", "PIPELINE_OVERFLOW", 2);
            }
            Pipeline *pipe = &p->pipelines[p->pipeline_count++];
            memset(pipe, 0, sizeof(*pipe));
            snprintf(pipe->id, sizeof(pipe->id), "%s", id);
            pipe->module_index[0] = -1;
            pipe->module_index[1] = -1;
            Module *first = find_module(p, key1, var1);
            if (!first) {
                fclose(file);
                return fail(p, "parse_manifest", "PIPELINE_REFERENCES_UNKNOWN_MODULE", 2);
            }
            pipe->module_index[0] = (int)(first - p->modules);
            if (strcmp(key2, "-")) {
                Module *second = find_module(p, key2, var2);
                if (!second) {
                    fclose(file);
                    return fail(p, "parse_manifest", "PIPELINE_REFERENCES_UNKNOWN_MODULE", 2);
                }
                pipe->module_index[1] = (int)(second - p->modules);
            }
        } else {
            fclose(file);
            return fail(p, "parse_manifest", "UNKNOWN_MANIFEST_TAG", 2);
        }
    }
    fclose(file);
    if (!p->module_count) return fail(p, "parse_manifest", "MANIFEST_HAS_NO_MODULES", 2);
    return true;
}

/* ------------------------------------------------------------------ */
/* Loader and instance setup.                                          */
/* ------------------------------------------------------------------ */

static bool load_loader(Probe *p) {
    for (int i = 0; i < LOADER_CANDIDATE_COUNT; ++i) {
        void *handle = dlopen(LOADER_CANDIDATES[i], RTLD_NOW | RTLD_LOCAL);
        if (handle) {
            p->loader = handle;
            g_loader = handle;
            p->loader_name = LOADER_CANDIDATES[i];
            return true;
        }
    }
    const char *error = dlerror();
    snprintf(p->loader_error, sizeof(p->loader_error), "%s",
             error ? error : "no Vulkan loader library could be opened");
    p->failure = "NO_VULKAN_IN_THIS_BUILD";
    p->failure_stage = "load_loader";
    p->exit_code = 3;
    return false;
}

static bool resolve_global_functions(Probe *p) {
    p_vkGetInstanceProcAddr =
        (PFN_vkGetInstanceProcAddr)dlsym(p->loader, "vkGetInstanceProcAddr");
    if (!p_vkGetInstanceProcAddr) {
        p->failure = "LOADER_MISSING_GET_INSTANCE_PROC_ADDR";
        p->failure_stage = "load_loader";
        p->exit_code = 3;
        return false;
    }
    size_t missing = 0;
#define FFRS_LOAD_GLOBAL(name, type)                                                    \
    p_##name = (type)p_vkGetInstanceProcAddr(NULL, #name);                              \
    if (!p_##name) ++missing;
    FFRS_VK_GLOBAL_FUNCTIONS(FFRS_LOAD_GLOBAL)
#undef FFRS_LOAD_GLOBAL
    if (missing) {
        p->failure = "LOADER_MISSING_GLOBAL_ENTRY_POINTS";
        p->failure_stage = "load_loader";
        p->exit_code = 3;
        return false;
    }
    return true;
}

/* Instance-scope commands are only resolvable once the instance exists. */
static bool resolve_instance_functions(Probe *p) {
    size_t missing = 0;
#define FFRS_LOAD_INSTANCE(name, type)                                                  \
    p_##name = (type)p_vkGetInstanceProcAddr(p->instance, #name);                        \
    if (!p_##name) ++missing;
    FFRS_VK_INSTANCE_FUNCTIONS(FFRS_LOAD_INSTANCE)
#undef FFRS_LOAD_INSTANCE
    if (missing) {
        p->failure = "LOADER_MISSING_INSTANCE_ENTRY_POINTS";
        p->failure_stage = "resolve_instance_functions";
        p->exit_code = 3;
        return false;
    }
    return true;
}

static bool create_instance(Probe *p) {
    uint32_t loader_version = VK_API_VERSION_1_0;
    if (p_vkEnumerateInstanceVersion) p_vkEnumerateInstanceVersion(&loader_version);
    p->instance_api_supported = loader_version;
    p->instance_api_requested = VK_API_VERSION_1_1;
    p->instance_api_used = loader_version < VK_API_VERSION_1_1 ? VK_API_VERSION_1_0
                                                                : VK_API_VERSION_1_1;
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "ffrs-vulkan-shader-probe",
        .applicationVersion = 1,
        .pEngineName = "shader-probe-only",
        .engineVersion = 1,
        .apiVersion = p->instance_api_used};
    VkInstanceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app};
    VkResult result = p_vkCreateInstance(&info, NULL, &p->instance);
    stage(p, "create_instance", result);
    if (result != VK_SUCCESS) {
        if (result == VK_ERROR_INCOMPATIBLE_DRIVER || result == VK_ERROR_INITIALIZATION_FAILED) {
            p->failure = "NO_VULKAN_IN_THIS_BUILD";
            p->exit_code = 3;
        } else {
            p->failure = "INSTANCE_CREATION_FAILED";
            p->exit_code = 2;
        }
        p->failure_stage = "create_instance";
        return false;
    }
    return true;
}

static bool select_device(Probe *p) {
    uint32_t count = 0;
    VkResult result = p_vkEnumeratePhysicalDevices(p->instance, &count, NULL);
    stage(p, "enumerate_physical_devices", result);
    if (result != VK_SUCCESS || !count || count > 64) {
        p->failure = "NO_PHYSICAL_DEVICE";
        p->failure_stage = "select_device";
        p->exit_code = 5;
        return false;
    }
    VkPhysicalDevice devices[64];
    result = p_vkEnumeratePhysicalDevices(p->instance, &count, devices);
    stage(p, "enumerate_physical_devices_list", result);
    if (result != VK_SUCCESS) {
        p->failure = "NO_PHYSICAL_DEVICE";
        p->failure_stage = "select_device";
        p->exit_code = 5;
        return false;
    }
    uint32_t best_score = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t family_count = 0;
        p_vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &family_count, NULL);
        if (!family_count || family_count > 256) continue;
        VkQueueFamilyProperties families[256];
        p_vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &family_count, families);
        for (uint32_t j = 0; j < family_count; ++j) {
            if (!families[j].queueCount) continue;
            VkQueueFlags flags = families[j].queueFlags;
            bool graphics = (flags & VK_QUEUE_GRAPHICS_BIT) != 0;
            bool compute = (flags & VK_QUEUE_COMPUTE_BIT) != 0;
            uint32_t score = (graphics && compute) ? 3 : (graphics ? 2 : (compute ? 1 : 0));
            if (score > best_score) {
                best_score = score;
                p->physical = devices[i];
                p->device_index = i;
                p->queue_family = j;
                p->queue_flags = flags;
            }
        }
    }
    if (!p->physical || best_score < 1) {
        p->failure = "NO_PHYSICAL_DEVICE";
        p->failure_stage = "select_device";
        p->exit_code = 5;
        return false;
    }
    p_vkGetPhysicalDeviceProperties(p->physical, &p->properties);
    p_vkGetPhysicalDeviceMemoryProperties(p->physical, &p->memory_properties);
    p->features_11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    if (p->properties.apiVersion >= VK_API_VERSION_1_1) {
        VkPhysicalDeviceFeatures2 features2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &p->features_11};
        p_vkGetPhysicalDeviceFeatures2(p->physical, &features2);
    }
    p_vkGetPhysicalDeviceFeatures(p->physical, &p->features);
    return true;
}

static void collect_extensions(Probe *p) {
    uint32_t count = 0;
    if (p_vkEnumerateDeviceExtensionProperties(p->physical, NULL, &count, NULL) != VK_SUCCESS)
        return;
    if (count > MAX_EXTENSIONS) count = MAX_EXTENSIONS;
    if (!count) return;
    VkExtensionProperties properties[MAX_EXTENSIONS];
    if (p_vkEnumerateDeviceExtensionProperties(p->physical, NULL, &count, properties) != VK_SUCCESS)
        return;
    p->extension_count = count;
    for (uint32_t i = 0; i < count; ++i)
        snprintf(p->extension_names[i], sizeof(p->extension_names[i]), "%s", properties[i].extensionName);
    for (uint32_t i = 0; i < count; ++i)
        if (!strcmp(properties[i].extensionName, "VK_EXT_fragment_shader_interlock"))
            p->fragment_shader_interlock_available = true;
}

static bool create_device(Probe *p) {
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = p->queue_family, .queueCount = 1, .pQueuePriorities = &priority};
    /* Only features the device reports as supported are requested. */
    VkPhysicalDeviceFeatures *f = &p->enabled_features;
    memset(f, 0, sizeof(*f));
    if (p->features.shaderStorageImageWriteWithoutFormat)
        f->shaderStorageImageWriteWithoutFormat = VK_TRUE;
    if (p->features.shaderStorageImageReadWithoutFormat)
        f->shaderStorageImageReadWithoutFormat = VK_TRUE;
    if (p->features.shaderStorageImageExtendedFormats)
        f->shaderStorageImageExtendedFormats = VK_TRUE;
    if (p->features.shaderFloat64) f->shaderFloat64 = VK_TRUE;
    if (p->features.shaderInt64) f->shaderInt64 = VK_TRUE;
    p->enabled_features_11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    if (p->properties.apiVersion >= VK_API_VERSION_1_1 && p->features_11.shaderDrawParameters) {
        p->enabled_features_11.shaderDrawParameters = VK_TRUE;
        p->enabled_features_11.pNext = NULL;
    }
    const char *extensions[1];
    uint32_t extension_count = 0;
    if (p->fragment_shader_interlock_available) {
        extensions[extension_count++] = "VK_EXT_fragment_shader_interlock";
        p->fragment_shader_interlock_enabled = true;
    }
    /* FFRS_PROBE_DEVICE narrows the device creation request so a driver that
     * rejects one part of it can be isolated from the rest of the probe.
     *   minimal : no optional features, no 1.1 feature structure
     *   no11    : the core features but no VkPhysicalDeviceVulkan11Features
     *   full    : everything the device reports as supported (default)
     */
    const char *mode = getenv("FFRS_PROBE_DEVICE");
    if (!mode) mode = "full";
    bool minimal = !strcmp(mode, "minimal");
    bool no11 = !strcmp(mode, "no11");
    const void *next = (!minimal && !no11 && p->properties.apiVersion >= VK_API_VERSION_1_1)
                            ? (const void *)&p->enabled_features_11 : NULL;
    const VkPhysicalDeviceFeatures *features = minimal ? NULL : (const VkPhysicalDeviceFeatures *)f;
    if (minimal) extension_count = 0;
    VkDeviceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = next,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue,
        .enabledExtensionCount = extension_count, .ppEnabledExtensionNames = extensions,
        .pEnabledFeatures = features};
    fprintf(stderr, "[probe] device='%s' api=%u.%u.%u driver=%u vendor=%u devid=%u "
            "family=%u flags=0x%x ext_count=%u fsil=%d\n",
            p->properties.deviceName, VK_VERSION_MAJOR(p->properties.apiVersion),
            VK_VERSION_MINOR(p->properties.apiVersion), VK_VERSION_PATCH(p->properties.apiVersion),
            p->properties.driverVersion, p->properties.vendorID, p->properties.deviceID,
            p->queue_family, (unsigned)p->queue_flags, extension_count,
            (int)p->fragment_shader_interlock_available);
    fprintf(stderr, "[probe] request write=%d read=%d ext=%d f64=%d i64=%d drawparams=%d\n",
            (int)f->shaderStorageImageWriteWithoutFormat,
            (int)f->shaderStorageImageReadWithoutFormat,
            (int)f->shaderStorageImageExtendedFormats, (int)f->shaderFloat64,
            (int)f->shaderInt64, (int)p->enabled_features_11.shaderDrawParameters);
    fprintf(stderr, "[probe] mode=%s next=%s features=%s\n", mode, next ? "yes" : "no",
            features ? "yes" : "no");
    trace("vkCreateDevice");
    VkResult result = p_vkCreateDevice(p->physical, &info, NULL, &p->device);
    stage(p, "create_device", result);
    trace("vkCreateDevice returned");
    if (result != VK_SUCCESS) {
        p->failure = (result == VK_ERROR_FEATURE_NOT_PRESENT) ? "DEVICE_FEATURE_NOT_PRESENT"
                                                              : "DEVICE_CREATION_FAILED";
        p->failure_stage = "create_device";
        p->exit_code = 5;
        return false;
    }
    size_t missing = 0;
#define FFRS_LOAD_DEVICE(name, type)                                                    \
    p_##name = (type)p_vkGetDeviceProcAddr(p->device, #name);                          \
    if (!p_##name) ++missing;
    FFRS_VK_DEVICE_FUNCTIONS(FFRS_LOAD_DEVICE)
#undef FFRS_LOAD_DEVICE
    trace("device entry points resolved");
    if (missing) {
        p->failure = "DEVICE_ENTRY_POINTS_MISSING";
        p->failure_stage = "create_device";
        p->exit_code = 5;
        return false;
    }
    p_vkGetDeviceQueue(p->device, p->queue_family, 0, &p->queue);
    trace("device queue");
    VkCommandPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = p->queue_family};
    result = p_vkCreateCommandPool(p->device, &pool, NULL, &p->command_pool);
    stage(p, "create_command_pool", result);
    if (result != VK_SUCCESS) return fail(p, "create_command_pool", "COMMAND_POOL_FAILED", 5);
    VkCommandBufferAllocateInfo commands = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = p->command_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1};
    result = p_vkAllocateCommandBuffers(p->device, &commands, &p->command_buffer);
    stage(p, "allocate_command_buffer", result);
    if (result != VK_SUCCESS) return fail(p, "allocate_command_buffer", "COMMAND_BUFFER_FAILED", 5);
    trace("command buffer");
    VkFenceCreateInfo fence = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    result = p_vkCreateFence(p->device, &fence, NULL, &p->fence);
    stage(p, "create_fence", result);
    if (result != VK_SUCCESS) return fail(p, "create_fence", "FENCE_FAILED", 5);
    return true;
}

/* ------------------------------------------------------------------ */
/* Tier 2: shader modules.                                             */
/* ------------------------------------------------------------------ */

static bool read_file(const char *path, unsigned char **data, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    if (fseek(file, 0, SEEK_END)) { fclose(file); return false; }
    long length = ftell(file);
    if (length <= 0 || length > (long)(16 * 1024 * 1024)) { fclose(file); return false; }
    rewind(file);
    unsigned char *buffer = malloc((size_t)length);
    if (!buffer) { fclose(file); return false; }
    size_t read = fread(buffer, 1, (size_t)length, file);
    fclose(file);
    if (read != (size_t)length) { free(buffer); return false; }
    *data = buffer;
    *size = read;
    return true;
}

static bool create_shader_modules(Probe *p, const char *directory) {
    char path[512];
    for (size_t i = 0; i < p->module_count; ++i) {
        Module *m = &p->modules[i];
        snprintf(path, sizeof(path), "%s/%s", directory, m->file);
        unsigned char *code = NULL;
        size_t size = 0;
        if (!read_file(path, &code, &size)) {
            m->module_result = VK_ERROR_INITIALIZATION_FAILED;
            m->module_created = false;
            snprintf(m->message, sizeof(m->message),
                     "the module file %s could not be read", m->file);
            continue;
        }
        VkShaderModuleCreateInfo info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = size, .pCode = (const uint32_t *)code};
        m->module_result = p_vkCreateShaderModule(p->device, &info, NULL, &m->module);
        m->module_created = m->module_result == VK_SUCCESS;
        m->module_attempted = true;
        snprintf(m->message, sizeof(m->message),
                 "vkCreateShaderModule returned %s (%d)", result_name((int)m->module_result),
                 (int)m->module_result);
        free(code);
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Tier 3: pipelines with a reflection-derived descriptor set layout.  */
/* ------------------------------------------------------------------ */

static bool merge_binds(Pipeline *pipe, const Module *a, const Module *b) {
    pipe->bind_count = 0;
    pipe->layout_conflict = false;
    const Module *modules[2] = {a, b};
    for (int m = 0; m < 2; ++m) {
        if (!modules[m]) continue;
        for (size_t i = 0; i < modules[m]->bind_count; ++i) {
            const Bind *in = &modules[m]->binds[i];
            bool found = false;
            for (size_t j = 0; j < pipe->bind_count; ++j) {
                Bind *existing = &pipe->binds[j];
                if (existing->set == in->set && existing->binding == in->binding) {
                    found = true;
                    if (existing->klass != in->klass) pipe->layout_conflict = true;
                    break;
                }
            }
            if (!found) {
                if (pipe->bind_count == MAX_BINDS) return false;
                pipe->binds[pipe->bind_count++] = *in;
            }
        }
    }
    return true;
}

static bool create_render_pass(Probe *p) {
    VkAttachmentDescription attachment = {.format = VK_FORMAT_R8G8B8A8_UNORM,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference reference = {.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1, .pColorAttachments = &reference};
    VkRenderPassCreateInfo info = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &attachment, .subpassCount = 1, .pSubpasses = &subpass};
    VkPipelineCacheCreateInfo cache_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    trace("create_pipeline_cache");
    VkResult result = p_vkCreatePipelineCache(p->device, &cache_info, NULL, &p->pipeline_cache);
    stage(p, "create_pipeline_cache", result);
    if (result != VK_SUCCESS) return fail(p, "create_pipeline_cache", "PIPELINE_CACHE_FAILED", 5);
    result = p_vkCreateRenderPass(p->device, &info, NULL, &p->render_pass);
    stage(p, "create_render_pass", result);
    if (result != VK_SUCCESS) return fail(p, "create_render_pass", "RENDER_PASS_FAILED", 5);
    return true;
}

static bool create_framebuffer(Probe *p) {
    VkResult result;
    VkImageCreateInfo image = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {IMAGE_WIDTH, IMAGE_HEIGHT, 1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    result = p_vkCreateImage(p->device, &image, NULL, &p->color_image);
    stage(p, "create_color_image", result);
    if (result != VK_SUCCESS) return fail(p, "create_color_image", "IMAGE_FAILED", 5);
    VkMemoryRequirements requirements;
    p_vkGetImageMemoryRequirements(p->device, p->color_image, &requirements);
    uint32_t memory_type = 0;
    bool have_type = false;
    for (uint32_t i = 0; i < p->memory_properties.memoryTypeCount; ++i) {
        if (!(requirements.memoryTypeBits & (1u << i))) continue;
        if (!have_type || (p->memory_properties.memoryTypes[i].propertyFlags &
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memory_type = i;
            have_type = true;
        }
        if (p->memory_properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
            break;
    }
    if (!have_type) return fail(p, "allocate_color_memory", "NO_MEMORY_TYPE", 5);
    VkMemoryAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = memory_type};
    result = p_vkAllocateMemory(p->device, &allocate, NULL, &p->color_memory);
    stage(p, "allocate_color_memory", result);
    if (result != VK_SUCCESS) return fail(p, "allocate_color_memory", "MEMORY_FAILED", 5);
    result = p_vkBindImageMemory(p->device, p->color_image, p->color_memory, 0);
    stage(p, "bind_color_memory", result);
    if (result != VK_SUCCESS) return fail(p, "bind_color_memory", "MEMORY_FAILED", 5);
    VkImageViewCreateInfo view = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = p->color_image, .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0,
            .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1}};
    result = p_vkCreateImageView(p->device, &view, NULL, &p->color_view);
    stage(p, "create_color_view", result);
    if (result != VK_SUCCESS) return fail(p, "create_color_view", "IMAGE_VIEW_FAILED", 5);
    trace("create_framebuffer");
    VkFramebufferCreateInfo framebuffer = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = p->render_pass, .attachmentCount = 1, .pAttachments = &p->color_view,
        .width = IMAGE_WIDTH, .height = IMAGE_HEIGHT, .layers = 1};
    result = p_vkCreateFramebuffer(p->device, &framebuffer, NULL, &p->framebuffer);
    stage(p, "create_framebuffer", result);
    if (result != VK_SUCCESS) return fail(p, "create_framebuffer", "FRAMEBUFFER_FAILED", 5);
    return true;
}

static bool create_pipelines(Probe *p) {
    for (size_t i = 0; i < p->pipeline_count; ++i) {
        Pipeline *pipe = &p->pipelines[i];
        Module *a = &p->modules[pipe->module_index[0]];
        Module *b = pipe->module_index[1] >= 0 ? &p->modules[pipe->module_index[1]] : NULL;
        if (!a->module_created || (b && !b->module_created)) {
            pipe->pipeline_result = VK_ERROR_INITIALIZATION_FAILED;
            snprintf(pipe->draw_reason, sizeof(pipe->draw_reason),
                     "shader module was not created for this pipeline");
            continue;
        }
        if (!merge_binds(pipe, a, b)) {
            pipe->pipeline_result = VK_ERROR_INITIALIZATION_FAILED;
            snprintf(pipe->draw_reason, sizeof(pipe->draw_reason), "descriptor binding overflow");
            continue;
        }
        if (pipe->layout_conflict) {
            pipe->pipeline_result = VK_ERROR_INITIALIZATION_FAILED;
            snprintf(pipe->draw_reason, sizeof(pipe->draw_reason),
                     "descriptor class conflict between pipeline stages");
            continue;
        }
        if (!pipe->bind_count) {
            pipe->pipeline_result = VK_ERROR_INITIALIZATION_FAILED;
            snprintf(pipe->draw_reason, sizeof(pipe->draw_reason),
                     "empty descriptor set layout is never used to claim this tier");
            continue;
        }
        VkShaderStageFlags layout_stages = (VkShaderStageFlags)a->stage;
        if (b) layout_stages |= (VkShaderStageFlags)b->stage;
        VkDescriptorSetLayoutBinding entries[MAX_BINDS];
        /* pImmutableSamplers must be NULL; an uninitialised pointer here makes
         * the driver dereference garbage. */
        memset(entries, 0, sizeof(entries));
        for (size_t j = 0; j < pipe->bind_count; ++j) {
            entries[j].binding = pipe->binds[j].binding;
            entries[j].descriptorType = descriptor_type_of(pipe->binds[j].klass);
            entries[j].descriptorCount = 1;
            entries[j].stageFlags = layout_stages;
        }
        VkDescriptorSetLayoutCreateInfo layout_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = (uint32_t)pipe->bind_count, .pBindings = entries};
        pipe->layout_attempted = true;
        fprintf(stderr, "[probe] pipeline %s: descriptor set layout %zu bindings\n",
                pipe->id, pipe->bind_count);
        pipe->layout_result = p_vkCreateDescriptorSetLayout(p->device, &layout_info, NULL,
                                                            &pipe->layout);
        stage(p, "create_descriptor_set_layout", pipe->layout_result);
        if (pipe->layout_result != VK_SUCCESS) {
            pipe->pipeline_result = pipe->layout_result;
            snprintf(pipe->draw_reason, sizeof(pipe->draw_reason), "descriptor set layout rejected");
            continue;
        }
        VkPipelineLayoutCreateInfo pipeline_layout_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1, .pSetLayouts = &pipe->layout};
        VkResult result = p_vkCreatePipelineLayout(p->device, &pipeline_layout_info, NULL,
                                                    &pipe->pipeline_layout);
        stage(p, "create_pipeline_layout", result);
        if (result != VK_SUCCESS) {
            pipe->pipeline_result = result;
            snprintf(pipe->draw_reason, sizeof(pipe->draw_reason), "pipeline layout rejected");
            continue;
        }
        bool compute = (a->stage == VK_SHADER_STAGE_COMPUTE_BIT) &&
                       (!b || b->stage == VK_SHADER_STAGE_COMPUTE_BIT);
        if (!compute && !b) {
            pipe->pipeline_result = VK_ERROR_INITIALIZATION_FAILED;
            snprintf(pipe->draw_reason, sizeof(pipe->draw_reason),
                     "graphics pipeline needs both a vertex and a fragment stage");
            continue;
        }
        if (compute) {
            fprintf(stderr, "[probe] pipeline %s: compute pipeline\n", pipe->id);
            VkComputePipelineCreateInfo info = {
                .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = a->module, .pName = "main"},
                .layout = pipe->pipeline_layout,
                .basePipelineHandle = VK_NULL_HANDLE, .basePipelineIndex = -1};
            pipe->pipeline_result = p_vkCreateComputePipelines(p->device, p->pipeline_cache, 1,
                                                               &info, NULL, &pipe->pipeline);
            snprintf(pipe->message, sizeof(pipe->message),
                     "vkCreateComputePipelines returned %s (%d)",
                     result_name((int)pipe->pipeline_result), (int)pipe->pipeline_result);
        } else {
            fprintf(stderr, "[probe] pipeline %s: graphics pipeline render_pass=%p\n", pipe->id,
                    (const void *)p->render_pass);
            VkPipelineVertexInputStateCreateInfo vertex_input = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            VkPipelineInputAssemblyStateCreateInfo assembly = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, .primitiveRestartEnable = VK_FALSE};
            VkViewport viewport = {0.0f, 0.0f, (float)IMAGE_WIDTH, (float)IMAGE_HEIGHT, 0.0f, 1.0f};
            VkRect2D scissor = {{0, 0}, {IMAGE_WIDTH, IMAGE_HEIGHT}};
            VkPipelineViewportStateCreateInfo viewport_state = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                .viewportCount = 1, .pViewports = &viewport,
                .scissorCount = 1, .pScissors = &scissor};
            VkPipelineRasterizationStateCreateInfo rasterization = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
                .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f};
            VkPipelineMultisampleStateCreateInfo multisample = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
            VkPipelineColorBlendAttachmentState blend_attachment = {.blendEnable = VK_FALSE,
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
            VkPipelineColorBlendStateCreateInfo blend = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                .attachmentCount = 1, .pAttachments = &blend_attachment};
            /* Every field of the stage structure has to be written.  The
             * driver dereferences stage.pNext and stage.pSpecializationInfo
             * without checking them, so a field that keeps the stack's
             * leftovers takes the process down with a SIGSEGV; P-MM-27
             * measured that on RG477M and the fix is the memset below. */
            VkPipelineShaderStageCreateInfo stages[2];
            memset(stages, 0, sizeof(stages));
            uint32_t stage_count = 0;
            stages[stage_count].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[stage_count].stage = VK_SHADER_STAGE_VERTEX_BIT;
            stages[stage_count].module = a->stage == VK_SHADER_STAGE_VERTEX_BIT ? a->module : b->module;
            stages[stage_count].pName = "main";
            ++stage_count;
            stages[stage_count].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[stage_count].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[stage_count].module = a->stage == VK_SHADER_STAGE_FRAGMENT_BIT ? a->module : b->module;
            stages[stage_count].pName = "main";
            ++stage_count;
            VkGraphicsPipelineCreateInfo info = {
                .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                .stageCount = stage_count, .pStages = stages,
                .pVertexInputState = &vertex_input, .pInputAssemblyState = &assembly,
                .pViewportState = &viewport_state, .pRasterizationState = &rasterization,
                .pMultisampleState = &multisample, .pColorBlendState = &blend,
                .layout = pipe->pipeline_layout, .renderPass = p->render_pass, .subpass = 0,
                .basePipelineHandle = VK_NULL_HANDLE, .basePipelineIndex = -1};
            pipe->pipeline_result = p_vkCreateGraphicsPipelines(p->device, p->pipeline_cache, 1,
                                                                &info, NULL, &pipe->pipeline);
            snprintf(pipe->message, sizeof(pipe->message),
                     "vkCreateGraphicsPipelines returned %s (%d)",
                     result_name((int)pipe->pipeline_result), (int)pipe->pipeline_result);
        }
        stage(p, "create_pipeline", pipe->pipeline_result);
        pipe->pipeline_attempted = true;
        pipe->pipeline_created = pipe->pipeline_result == VK_SUCCESS;
        if (!pipe->pipeline_created)
            snprintf(pipe->draw_reason, sizeof(pipe->draw_reason), "pipeline creation rejected");
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Tier 4: a draw that the GPU actually consumes.                      */
/* ------------------------------------------------------------------ */

typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkImage image;
    VkDeviceMemory image_memory;
    VkImageView view;
    VkSampler sampler;
} Resource;

static bool pick_memory_type(Probe *p, uint32_t bits, uint32_t *out) {
    bool have = false;
    for (uint32_t i = 0; i < p->memory_properties.memoryTypeCount; ++i) {
        if (!(bits & (1u << i))) continue;
        if (!have) { *out = i; have = true; }
        if (p->memory_properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
            *out = i;
            return true;
        }
    }
    return have;
}

static bool create_buffer_resource(Probe *p, Resource *r) {
    VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = UNIFORM_BYTES,
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VkResult result = p_vkCreateBuffer(p->device, &info, NULL, &r->buffer);
    if (result != VK_SUCCESS) return false;
    VkMemoryRequirements requirements;
    p_vkGetBufferMemoryRequirements(p->device, r->buffer, &requirements);
    uint32_t memory_type = 0;
    if (!pick_memory_type(p, requirements.memoryTypeBits, &memory_type)) return false;
    VkMemoryAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = memory_type};
    result = p_vkAllocateMemory(p->device, &allocate, NULL, &r->memory);
    if (result != VK_SUCCESS) return false;
    result = p_vkBindBufferMemory(p->device, r->buffer, r->memory, 0);
    return result == VK_SUCCESS;
}

static bool create_image_resource(Probe *p, Resource *r, VkFormat format, VkImageUsageFlags usage) {
    VkImageCreateInfo info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = format,
        .extent = {IMAGE_WIDTH, IMAGE_HEIGHT, 1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    VkResult result = p_vkCreateImage(p->device, &info, NULL, &r->image);
    if (result != VK_SUCCESS) return false;
    VkMemoryRequirements requirements;
    p_vkGetImageMemoryRequirements(p->device, r->image, &requirements);
    uint32_t memory_type = 0;
    if (!pick_memory_type(p, requirements.memoryTypeBits, &memory_type)) return false;
    VkMemoryAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = memory_type};
    result = p_vkAllocateMemory(p->device, &allocate, NULL, &r->image_memory);
    if (result != VK_SUCCESS) return false;
    result = p_vkBindImageMemory(p->device, r->image, r->image_memory, 0);
    if (result != VK_SUCCESS) return false;
    VkImageViewCreateInfo view = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = r->image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = format,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0,
            .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1}};
    result = p_vkCreateImageView(p->device, &view, NULL, &r->view);
    if (result != VK_SUCCESS) return false;
    if (usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
        VkSamplerCreateInfo sampler = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .maxLod = 1.0f};
        result = p_vkCreateSampler(p->device, &sampler, NULL, &r->sampler);
        if (result != VK_SUCCESS) return false;
    }
    return true;
}

static void destroy_resource(Probe *p, Resource *r) {
    if (p->device) {
        if (r->sampler) { p_vkDestroySampler(p->device, r->sampler, NULL); ++p->destroyed_objects; }
        if (r->view) { p_vkDestroyImageView(p->device, r->view, NULL); ++p->destroyed_objects; }
        if (r->image) { p_vkDestroyImage(p->device, r->image, NULL); ++p->destroyed_objects; }
        if (r->image_memory) { p_vkFreeMemory(p->device, r->image_memory, NULL); ++p->destroyed_objects; }
        if (r->buffer) { p_vkDestroyBuffer(p->device, r->buffer, NULL); ++p->destroyed_objects; }
        if (r->memory) { p_vkFreeMemory(p->device, r->memory, NULL); ++p->destroyed_objects; }
    }
    memset(r, 0, sizeof(*r));
}

static bool run_draw(Probe *p) {
    for (size_t i = 0; i < p->pipeline_count; ++i) {
        Pipeline *pipe = &p->pipelines[i];
        if (!pipe->pipeline_created) continue;
        Module *a = &p->modules[pipe->module_index[0]];
        Module *b = pipe->module_index[1] >= 0 ? &p->modules[pipe->module_index[1]] : NULL;
        bool compute = (a->stage == VK_SHADER_STAGE_COMPUTE_BIT) &&
                       (!b || b->stage == VK_SHADER_STAGE_COMPUTE_BIT);
        if (!compute && (!p->render_pass || !p->framebuffer)) {
            snprintf(pipe->draw_reason, sizeof(pipe->draw_reason),
                     "graphics draw needs a render pass and framebuffer");
            continue;
        }
        Resource resources[MAX_BINDS];
        memset(resources, 0, sizeof(resources));
        uint32_t counts[CLASS_COUNT];
        memset(counts, 0, sizeof(counts));
        bool ok = true;
        for (size_t j = 0; j < pipe->bind_count && ok; ++j) {
            switch (pipe->binds[j].klass) {
            case CLASS_UNIFORM_BUFFER:
            case CLASS_STORAGE_BUFFER:
                ok = create_buffer_resource(p, &resources[j]);
                break;
            case CLASS_COMBINED_IMAGE_SAMPLER:
                ok = create_image_resource(p, &resources[j], VK_FORMAT_R8G8B8A8_UNORM,
                                           VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
                break;
            case CLASS_SAMPLED_IMAGE:
                ok = create_image_resource(p, &resources[j], VK_FORMAT_R8G8B8A8_UNORM,
                                           VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
                break;
            case CLASS_STORAGE_IMAGE:
                ok = create_image_resource(p, &resources[j], VK_FORMAT_R32_UINT,
                                           VK_IMAGE_USAGE_STORAGE_BIT);
                break;
            case CLASS_INPUT_ATTACHMENT:
                ok = false;
                break;
            default:
                ok = false;
                break;
            }
            if (ok) ++counts[pipe->binds[j].klass];
        }
        if (!ok) {
            for (size_t j = 0; j < pipe->bind_count; ++j) destroy_resource(p, &resources[j]);
            snprintf(pipe->draw_reason, sizeof(pipe->draw_reason),
                     "descriptor resources could not be created");
            continue;
        }
        VkDescriptorPoolSize sizes[CLASS_COUNT];
        uint32_t size_count = 0;
        for (int k = 0; k < CLASS_COUNT; ++k) {
            if (!counts[k]) continue;
            sizes[size_count].type = descriptor_type_of(k);
            sizes[size_count].descriptorCount = counts[k];
            ++size_count;
        }
        VkDescriptorPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 1, .poolSizeCount = size_count, .pPoolSizes = sizes};
        VkDescriptorPool pool = VK_NULL_HANDLE;
        VkResult result = p_vkCreateDescriptorPool(p->device, &pool_info, NULL, &pool);
        if (result != VK_SUCCESS) {
            for (size_t j = 0; j < pipe->bind_count; ++j) destroy_resource(p, &resources[j]);
            snprintf(pipe->draw_reason, sizeof(pipe->draw_reason), "descriptor pool rejected");
            continue;
        }
        VkDescriptorSetAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = pool, .descriptorSetCount = 1, .pSetLayouts = &pipe->layout};
        result = p_vkAllocateDescriptorSets(p->device, &allocate, &pipe->descriptor_set);
        if (result != VK_SUCCESS) {
            p_vkDestroyDescriptorPool(p->device, pool, NULL);
            for (size_t j = 0; j < pipe->bind_count; ++j) destroy_resource(p, &resources[j]);
            snprintf(pipe->draw_reason, sizeof(pipe->draw_reason), "descriptor set allocation rejected");
            continue;
        }
        VkWriteDescriptorSet writes[MAX_BINDS];
        VkDescriptorBufferInfo buffer_infos[MAX_BINDS];
        VkDescriptorImageInfo image_infos[MAX_BINDS];
        memset(writes, 0, sizeof(writes));
        memset(buffer_infos, 0, sizeof(buffer_infos));
        memset(image_infos, 0, sizeof(image_infos));
        for (size_t j = 0; j < pipe->bind_count; ++j) {
            writes[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[j].dstSet = pipe->descriptor_set;
            writes[j].dstBinding = pipe->binds[j].binding;
            writes[j].dstArrayElement = 0;
            writes[j].descriptorCount = 1;
            writes[j].descriptorType = descriptor_type_of(pipe->binds[j].klass);
            if (pipe->binds[j].klass == CLASS_UNIFORM_BUFFER ||
                pipe->binds[j].klass == CLASS_STORAGE_BUFFER) {
                buffer_infos[j].buffer = resources[j].buffer;
                buffer_infos[j].offset = 0;
                buffer_infos[j].range = VK_WHOLE_SIZE;
                writes[j].pBufferInfo = &buffer_infos[j];
            } else {
                image_infos[j].imageView = resources[j].view;
                image_infos[j].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                if (pipe->binds[j].klass == CLASS_COMBINED_IMAGE_SAMPLER)
                    image_infos[j].sampler = resources[j].sampler;
                writes[j].pImageInfo = &image_infos[j];
            }
        }
        p_vkUpdateDescriptorSets(p->device, (uint32_t)pipe->bind_count, writes, 0, NULL);

        VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        result = p_vkBeginCommandBuffer(p->command_buffer, &begin);
        if (result == VK_SUCCESS) {
            if (compute) {
                p_vkCmdBindPipeline(p->command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipe->pipeline);
                p_vkCmdBindDescriptorSets(p->command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                          pipe->pipeline_layout, 0, 1, &pipe->descriptor_set, 0, NULL);
                p_vkCmdDispatch(p->command_buffer, 1, 1, 1);
            } else {
                VkClearValue clear = {.color = {{0.0f, 0.0f, 0.0f, 1.0f}}};
                VkRenderPassBeginInfo pass = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                    .renderPass = p->render_pass, .framebuffer = p->framebuffer,
                    .renderArea = {{0, 0}, {IMAGE_WIDTH, IMAGE_HEIGHT}},
                    .clearValueCount = 1, .pClearValues = &clear};
                p_vkCmdBeginRenderPass(p->command_buffer, &pass, VK_SUBPASS_CONTENTS_INLINE);
                p_vkCmdBindPipeline(p->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe->pipeline);
                p_vkCmdBindDescriptorSets(p->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                          pipe->pipeline_layout, 0, 1, &pipe->descriptor_set, 0, NULL);
                p_vkCmdDraw(p->command_buffer, MAX_DRAW_VERTICES, 1, 0, 0);
                p_vkCmdEndRenderPass(p->command_buffer);
            }
            result = p_vkEndCommandBuffer(p->command_buffer);
        }
        if (result != VK_SUCCESS) {
            p_vkDestroyDescriptorPool(p->device, pool, NULL);
            for (size_t j = 0; j < pipe->bind_count; ++j) destroy_resource(p, &resources[j]);
            snprintf(pipe->draw_reason, sizeof(pipe->draw_reason), "command recording rejected");
            continue;
        }
        pipe->draw_attempted = true;
        VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1, .pCommandBuffers = &p->command_buffer};
        pipe->submit_result = p_vkQueueSubmit(p->queue, 1, &submit, p->fence);
        stage(p, "queue_submit", pipe->submit_result);
        if (pipe->submit_result == VK_SUCCESS) {
            pipe->wait_result = p_vkWaitForFences(p->device, 1, &p->fence, VK_TRUE, FENCE_TIMEOUT_NS);
            stage(p, "wait_for_fence", pipe->wait_result);
            if (pipe->wait_result == VK_SUCCESS) {
                pipe->draw_consumed = true;
            } else if (pipe->wait_result == VK_ERROR_DEVICE_LOST) {
                p->device_lost = true;
                snprintf(pipe->draw_reason, sizeof(pipe->draw_reason), "device lost");
            } else {
                snprintf(pipe->draw_reason, sizeof(pipe->draw_reason), "fence did not signal");
            }
        } else {
            snprintf(pipe->draw_reason, sizeof(pipe->draw_reason), "queue submission rejected");
        }
        p_vkDestroyDescriptorPool(p->device, pool, NULL);
        for (size_t j = 0; j < pipe->bind_count; ++j) destroy_resource(p, &resources[j]);
        if (p->device_lost) return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Reporting.                                                          */
/* ------------------------------------------------------------------ */

static void json_string(const char *value) {
    if (!value) { fputs("null", stdout); return; }
    putchar('"');
    for (const unsigned char *s = (const unsigned char *)value; *s; ++s) {
        if (*s == '"' || *s == '\\') { putchar('\\'); putchar(*s); }
        else if (*s < 32 || *s >= 127) printf("\\u%04x", *s);
        else putchar(*s);
    }
    putchar('"');
}

static const char *stage_status(const Probe *p, size_t index, bool *any_failed) {
    (void)p; (void)index; (void)any_failed;
    return NULL;
}

static void print_module(const Module *m) {
    printf("{\"key\":"); json_string(m->key);
    fputs(",\"variant\":", stdout); json_string(m->variant);
    printf(",\"stage\":\"%s\",\"file\":", m->stage == VK_SHADER_STAGE_VERTEX_BIT ? "vert"
          : m->stage == VK_SHADER_STAGE_FRAGMENT_BIT ? "frag" : "comp");
    json_string(m->file);
    printf(",\"created\":%s,\"attempted\":%s,", m->module_created ? "true" : "false",
           m->module_attempted ? "true" : "false");
    if (m->module_attempted) {
        printf("\"vk_result\":%d,\"vk_result_name\":", (int)m->module_result);
        json_string(result_name((int)m->module_result));
    } else {
        fputs("\"vk_result\":null,\"vk_result_name\":\"NOT_ATTEMPTED\"", stdout);
    }
    fputs(",\"message\":", stdout);
    json_string(m->message[0] ? m->message : NULL);
    putchar('}');
}

static void print_pipeline(const Pipeline *pipe) {
    printf("{\"id\":"); json_string(pipe->id);
    fputs(",\"created\":", stdout);
    fputs(pipe->pipeline_created ? "true" : "false", stdout);
    if (pipe->pipeline_attempted) {
        printf(",\"vk_result\":%d,\"vk_result_name\":", (int)pipe->pipeline_result);
        json_string(result_name((int)pipe->pipeline_result));
    } else {
        fputs(",\"vk_result\":null,\"vk_result_name\":\"NOT_ATTEMPTED\"", stdout);
    }
    fputs(",\"message\":", stdout);
    json_string(pipe->message[0] ? pipe->message : NULL);
    if (pipe->layout_attempted) {
        printf(",\"descriptor_set_layout_result\":%d,\"descriptor_set_layout_result_name\":",
               (int)pipe->layout_result);
        json_string(result_name((int)pipe->layout_result));
    } else {
        fputs(",\"descriptor_set_layout_result\":null,"
              "\"descriptor_set_layout_result_name\":\"NOT_ATTEMPTED\"", stdout);
    }
    fputs(",\"descriptor_set_layout\":[", stdout);
    for (size_t j = 0; j < pipe->bind_count; ++j) {
        if (j) putchar(',');
        printf("{\"set\":%u,\"binding\":%u,\"type\":", pipe->binds[j].set, pipe->binds[j].binding);
        json_string(CLASS_NAMES[pipe->binds[j].klass]);
        fputs(",\"provenance\":", stdout);
        json_string(pipe->binds[j].provenance);
        putchar('}');
    }
    fputs("],\"draw\":{\"attempted\":", stdout);
    fputs(pipe->draw_attempted ? "true" : "false", stdout);
    printf(",\"consumed\":%s,", pipe->draw_consumed ? "true" : "false");
    if (pipe->draw_attempted) {
        printf("\"submit_vk_result\":%d,\"submit_vk_result_name\":", (int)pipe->submit_result);
        json_string(result_name((int)pipe->submit_result));
        printf(",\"wait_vk_result\":%d,\"wait_vk_result_name\":", (int)pipe->wait_result);
        json_string(result_name((int)pipe->wait_result));
    } else {
        fputs("\"submit_vk_result\":null,\"submit_vk_result_name\":\"NOT_ATTEMPTED\""
              ",\"wait_vk_result\":null,\"wait_vk_result_name\":\"NOT_ATTEMPTED\"", stdout);
    }
    fputs(",\"reason\":", stdout);
    json_string(pipe->draw_reason[0] ? pipe->draw_reason : NULL);
    fputs("}}", stdout);
}

static void print_report(const Probe *p) {
    size_t modules_created = 0, pipelines_created = 0, draws_consumed = 0, draws_attempted = 0;
    for (size_t i = 0; i < p->module_count; ++i) if (p->modules[i].module_created) ++modules_created;
    for (size_t i = 0; i < p->pipeline_count; ++i) {
        if (p->pipelines[i].pipeline_created) ++pipelines_created;
        if (p->pipelines[i].draw_attempted) ++draws_attempted;
        if (p->pipelines[i].draw_consumed) ++draws_consumed;
    }
    const char *tier1 = p->physical ? "PASS" : "NOT_RUN";
    const char *tier2 = p->device ? (modules_created == p->module_count ? "PASS" : "PARTIAL") : "NOT_RUN";
    const char *tier3 = p->device ? (pipelines_created == p->pipeline_count ? "PASS" : "PARTIAL") : "NOT_RUN";
    const char *tier4 = p->device ? (draws_consumed ? (draws_consumed == pipelines_created ? "PASS" : "PARTIAL")
                                                    : "NOT_RUN") : "NOT_RUN";
    const char *status = "COMPLETE";
    if (p->failure && !strcmp(p->failure, "NO_VULKAN_IN_THIS_BUILD")) status = "NO_VULKAN_IN_THIS_BUILD";
    else if (p->failure) status = "INCOMPLETE";
    printf("{\"schema\":\"%s\",\"status\":\"%s\",\"exit_code\":%d,", FFRS_SCHEMA, status,
           p->exit_code);
    fputs("\"source_sha256\":", stdout); json_string(FFRS_VK_SOURCE_SHA256);
    fputs(",\"git_head_at_build\":", stdout); json_string(FFRS_VK_GIT_HEAD);
    fputs(",\"vulkan\":{\"loader_library\":", stdout);
    json_string(p->loader_name);
    printf(",\"loader_found\":%s,\"loader_error\":", p->loader ? "true" : "false");
    json_string(p->loader_error[0] ? p->loader_error : NULL);
    printf(",\"instance_api_version_requested\":\"%s\",\"instance_api_version_supported\":\"%s\","
           "\"instance_api_version_used\":\"%s\"}",
           version_string(p->instance_api_requested), version_string(p->instance_api_supported),
           version_string(p->instance_api_used));
    fputs(",\"device_facts\":{\"tier\":\"1_device_facts\",\"status\":", stdout);
    json_string(tier1);
    fputs(",\"physical_device\":{\"name\":", stdout);
    json_string(p->properties.deviceName);
    printf(",\"index\":%u,\"type\":", p->device_index);
    json_string(device_type_name(p->properties.deviceType));
    printf(",\"api_version\":\"%s\",\"driver_version\":%u,\"vendor_id\":%u,\"device_id\":%u,"
           "\"queue_family\":%u,\"queue_flags\":%u},",
           version_string(p->properties.apiVersion),
           p->properties.driverVersion, p->properties.vendorID, p->properties.deviceID,
           p->queue_family, (unsigned)p->queue_flags);
    fputs("\"features_supported\":{\"shaderStorageImageWriteWithoutFormat\":", stdout);
    fputs(p->features.shaderStorageImageWriteWithoutFormat ? "true" : "false", stdout);
    fputs(",\"shaderStorageImageReadWithoutFormat\":", stdout);
    fputs(p->features.shaderStorageImageReadWithoutFormat ? "true" : "false", stdout);
    fputs(",\"shaderStorageImageExtendedFormats\":", stdout);
    fputs(p->features.shaderStorageImageExtendedFormats ? "true" : "false", stdout);
    fputs(",\"shaderFloat64\":", stdout);
    fputs(p->features.shaderFloat64 ? "true" : "false", stdout);
    fputs(",\"shaderInt64\":", stdout);
    fputs(p->features.shaderInt64 ? "true" : "false", stdout);
    fputs(",\"drawParameters\":", stdout);
    fputs(p->features_11.shaderDrawParameters ? "true" : "false", stdout);
    fputs("},\"features_enabled_by_probe\":{\"shaderStorageImageWriteWithoutFormat\":", stdout);
    fputs(p->enabled_features.shaderStorageImageWriteWithoutFormat ? "true" : "false", stdout);
    fputs(",\"shaderStorageImageReadWithoutFormat\":", stdout);
    fputs(p->enabled_features.shaderStorageImageReadWithoutFormat ? "true" : "false", stdout);
    fputs(",\"shaderStorageImageExtendedFormats\":", stdout);
    fputs(p->enabled_features.shaderStorageImageExtendedFormats ? "true" : "false", stdout);
    fputs(",\"shaderFloat64\":", stdout);
    fputs(p->enabled_features.shaderFloat64 ? "true" : "false", stdout);
    fputs(",\"shaderInt64\":", stdout);
    fputs(p->enabled_features.shaderInt64 ? "true" : "false", stdout);
    fputs(",\"drawParameters\":", stdout);
    fputs(p->enabled_features_11.shaderDrawParameters ? "true" : "false", stdout);
    printf("},\"fragment_shader_interlock_supported\":%s,\"fragment_shader_interlock_enabled\":%s,",
           p->fragment_shader_interlock_available ? "true" : "false",
           p->fragment_shader_interlock_enabled ? "true" : "false");
    fputs("\"device_extensions_available\":[", stdout);
    for (uint32_t i = 0; i < p->extension_count; ++i) {
        if (i) putchar(',');
        json_string(p->extension_names[i]);
    }
    fputs("]}," , stdout);
    fputs("\"tiers\":{", stdout);
    fputs("\"1_device_facts\":{\"status\":", stdout); json_string(tier1);
    fputs(",\"reason\":", stdout); json_string(p->physical ? NULL : "NO_PHYSICAL_DEVICE");
    fputs("},\"2_shader_module\":{\"status\":", stdout); json_string(tier2);
    printf(",\"created\":%zu,\"total\":%zu,\"modules\":[", modules_created, p->module_count);
    for (size_t i = 0; i < p->module_count; ++i) {
        if (i) putchar(',');
        print_module(&p->modules[i]);
    }
    fputs("]},\"3_pipeline\":{\"status\":", stdout); json_string(tier3);
    printf(",\"created\":%zu,\"total\":%zu,\"pipelines\":[", pipelines_created, p->pipeline_count);
    for (size_t i = 0; i < p->pipeline_count; ++i) {
        if (i) putchar(',');
        print_pipeline(&p->pipelines[i]);
    }
    fputs("]},\"4_draw\":{\"status\":", stdout); json_string(tier4);
    printf(",\"attempted\":%zu,\"consumed\":%zu,\"reason\":", draws_attempted, draws_consumed);
    json_string(draws_consumed ? NULL : "NO_DRAW_WAS_CONSUMED");
    fputs("}},\"failure_stage\":", stdout); json_string(p->failure_stage);
    fputs(",\"failure\":", stdout); json_string(p->failure);
    fputs(",\"stages\":[", stdout);
    size_t errors = 0;
    for (size_t i = 0; i < p->stage_count; ++i) {
        if (i) putchar(',');
        fputs("{\"stage\":", stdout); json_string(p->stages[i].name);
        printf(",\"vk_result\":%d,\"vk_result_name\":", p->stages[i].result);
        json_string(result_name(p->stages[i].result));
        putchar('}');
        if (p->stages[i].result != VK_SUCCESS) ++errors;
    }
    printf("],\"vk_result_errors\":%zu,\"cleanup\":{\"strategy\":\"%s\","
           "\"objects_released\":%u,\"device_and_instance_destroyed\":false}}\n",
           errors,
           "explicit_object_release_then_process_exit",
           p->destroyed_objects);
    (void)stage_status;
}

static void cleanup(Probe *p) {
    if (p->device) {
        /* Nothing may still be in flight when the objects below are released. */
        p_vkDeviceWaitIdle(p->device);
        if (p->fence) { p_vkDestroyFence(p->device, p->fence, NULL); ++p->destroyed_objects; }
        if (p->command_pool) { p_vkDestroyCommandPool(p->device, p->command_pool, NULL); ++p->destroyed_objects; }
        if (p->framebuffer) { p_vkDestroyFramebuffer(p->device, p->framebuffer, NULL); ++p->destroyed_objects; }
        if (p->color_view) { p_vkDestroyImageView(p->device, p->color_view, NULL); ++p->destroyed_objects; }
        if (p->color_image) { p_vkDestroyImage(p->device, p->color_image, NULL); ++p->destroyed_objects; }
        if (p->color_memory) { p_vkFreeMemory(p->device, p->color_memory, NULL); ++p->destroyed_objects; }
        if (p->render_pass) { p_vkDestroyRenderPass(p->device, p->render_pass, NULL); ++p->destroyed_objects; }
        for (size_t i = 0; i < p->module_count; ++i)
            if (p->modules[i].module) {
                p_vkDestroyShaderModule(p->device, p->modules[i].module, NULL);
                ++p->destroyed_objects;
            }
        for (size_t i = 0; i < p->pipeline_count; ++i) {
            if (p->pipelines[i].pipeline) {
                p_vkDestroyPipeline(p->device, p->pipelines[i].pipeline, NULL);
                ++p->destroyed_objects;
            }
            if (p->pipelines[i].pipeline_layout) {
                p_vkDestroyPipelineLayout(p->device, p->pipelines[i].pipeline_layout, NULL);
                ++p->destroyed_objects;
            }
            if (p->pipelines[i].layout) {
                p_vkDestroyDescriptorSetLayout(p->device, p->pipelines[i].layout, NULL);
                ++p->destroyed_objects;
            }
        }
        /* The device and the instance are deliberately not destroyed here.
         * On this Mali driver vkDestroyDevice makes the driver abort in its
         * own event thread ("pthread_mutex_lock called on a destroyed
         * mutex"), which would take the report down with it.  The probe is a
         * short lived process, so the operating system reclaims both. */
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fputs("Usage: vulkan_shader_probe <manifest-file> <module-directory>\n"
              "Reports the device-facts, shader-module, pipeline and draw tiers as JSON.\n",
              stderr);
        return 64;
    }
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = watchdog;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGALRM, &action, NULL)) { perror("sigaction"); return 2; }
    alarm(120);
    Probe probe;
    memset(&probe, 0, sizeof(probe));
    if (!parse_manifest(&probe, argv[1])) {
        print_report(&probe);
        fflush(stdout);
        return probe.exit_code ? probe.exit_code : 2;
    }
    trace("load_loader");
    if (!load_loader(&probe) || !resolve_global_functions(&probe) || !create_instance(&probe) ||
        !resolve_instance_functions(&probe)) {
        print_report(&probe);
        fflush(stdout);
        return probe.exit_code ? probe.exit_code : 2;
    }
    trace("select_device");
    if (!select_device(&probe)) {
        print_report(&probe);
        fflush(stdout);
        return probe.exit_code ? probe.exit_code : 5;
    }
    trace("collect_extensions");
    collect_extensions(&probe);
    trace("create_device");
    if (!create_device(&probe)) {
        cleanup(&probe);
        print_report(&probe);
        fflush(stdout);
        return probe.exit_code ? probe.exit_code : 5;
    }
    trace("create_shader_modules");
    (void)create_shader_modules(&probe, argv[2]);
    trace("create_render_pass");
    (void)create_render_pass(&probe);
    trace("create_pipelines");
    (void)create_pipelines(&probe);
    trace("create_framebuffer");
    (void)create_framebuffer(&probe);
    trace("run_draw");
    (void)run_draw(&probe);
    trace("report");
    print_report(&probe);
    fflush(stdout);
    trace("cleanup");
    cleanup(&probe);
    alarm(0);
    return probe.exit_code;
}
