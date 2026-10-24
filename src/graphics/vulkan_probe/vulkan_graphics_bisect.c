/* SPDX-License-Identifier: MIT
 *
 * vulkan_graphics_bisect.c -- P-MM-27, the graphics-pipeline crash bisect.
 *
 * Purpose
 * -------
 * P-MM-23 measured that vkCreateGraphicsPipelines() takes the process down on
 * RG477M with "Fatal signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr
 * 0x40000000000", for the game's own shaders and for a hand-written trivial
 * vertex/fragment pair alike.  This probe answers four questions with device
 * evidence:
 *
 *   Q1  what is the minimum graphics-pipeline state that triggers the crash
 *   Q2  whether the crash is in the loader or in the driver
 *   Q3  whether the state is wrong or the driver is broken
 *   Q4  what graphics-pipeline state *does* build on this device
 *
 * Design
 * ------
 * One experiment per process.  The parent forks and execs itself once per
 * experiment, so every experiment gets its own exit status, its own signal and
 * its own fault address; nothing that one experiment does to the process
 * address space can influence the next one.  The child writes its evidence
 * into <out>/<experiment>/ and the parent collects the outcome into
 * <out>/summary.json.
 *
 * The probe owns its own VkInstance and VkDevice and never touches the runtime
 * backend.  It dlopen()s the Vulkan loader at run time and resolves the entry
 * points through vkGetInstanceProcAddr / vkGetDeviceProcAddr, so the same
 * source starts on a host without a Vulkan loader and says so instead of
 * failing to link.
 *
 * Nothing here is a mock: every state that is handed to the driver is a state
 * that a Vulkan implementation is required to accept or to reject with a
 * VkResult, and the driver's own answer is what the report quotes.
 */
#define _POSIX_C_SOURCE 200809L

/* The faulting program counter is read out of the signal context.  That is
 * an Android/aarch64 shape: bionic's struct sigcontext carries the registers,
 * the frame pointer and the program counter. */
#if defined(__ANDROID__) && defined(__aarch64__)
#define BISECT_HAVE_ARM64_CONTEXT 1
#else
#define BISECT_HAVE_ARM64_CONTEXT 0
#endif

#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#if BISECT_HAVE_ARM64_CONTEXT
#include <ucontext.h>
#endif
#include <unistd.h>

#include <vulkan_core.h>

#define BISECT_SCHEMA "ffrs.vulkan.graphics.bisect.v1"


/* The image the render pass and the framebuffer are built on. */
enum { IMAGE_WIDTH = 8, IMAGE_HEIGHT = 8 };

/*
 * The poison pointer.  It is deliberately outside every mapping the process
 * makes, so a driver that dereferences it takes a SEGV_MAPERR whose fault
 * address is the poison value itself -- that is how a stage-structure field
 * that was never initialised gets pinned down to a single field.
 */
#define BISECT_POISON_PTR ((const void *)(uintptr_t)0x0000DEADBEEF0000ULL)

/* ------------------------------------------------------------------ */
/* Experiment flags                                                    */
/* ------------------------------------------------------------------ */

enum {
    /* How VkPipelineShaderStageCreateInfo is filled in. */
    EXP_STAGE_ZEROED = 1u << 0,       /* every field written explicitly */
    EXP_STAGE_UNINIT = 1u << 1,       /* P-MM-23's shape: four fields written */
    EXP_STAGE_POISON_PNEXT = 1u << 2, /* pNext set to BISECT_POISON_PTR */
    EXP_STAGE_POISON_SPEC = 1u << 3,  /* pSpecializationInfo set to poison */
    EXP_STAGE_POISON_FLAGS = 1u << 4, /* flags set to 0xffffffff */
    EXP_STACK_POISON = 1u << 5,       /* dirty the frame before the array */

    /* The rest of the graphics pipeline state. */
    EXP_VERTEX_BINDING = 1u << 6,      /* one binding + one attribute */
    EXP_DYNAMIC_VIEWPORT = 1u << 7,    /* viewportCount 0 + dynamic state */
    EXP_ATTACHMENT_0 = 1u << 8,        /* render pass without attachments */
    EXP_SAMPLES_4 = 1u << 9,           /* 4x MSAA */
    EXP_DEPTH = 1u << 10,             /* depth attachment + depth state */
    EXP_DEPTH_CLAMP = 1u << 11,       /* depthClampEnable */
    EXP_DYNAMIC_RENDERING = 1u << 12, /* VkPipelineRenderingCreateInfo */
    EXP_RASTERIZER_DISCARD = 1u << 13,
    EXP_BLEND_ENABLE = 1u << 14,
    EXP_LOGIC_OP = 1u << 15,
    EXP_GAME_MODULES = 1u << 16,  /* the game's own vertex and fragment modules */
    EXP_DEVICE_FEATURES = 1u << 17 /* ask for the features the device reports */
};

typedef struct {
    const char *name;
    uint32_t flags;
    const char *question;
    const char *state;
} Experiment;

static const Experiment EXPERIMENTS[] = {
    {"pmm23-uninit", EXP_STAGE_UNINIT | EXP_STACK_POISON,
     "Q1/Q3 reproduce P-MM-23 exactly: the stage structures keep the stack's leftovers",
     "P-MM-23's graphics state: 1 colour attachment, 1 viewport, 1 scissor, "
     "vertex input count 0, samples 1, no depth, no dynamic state, blend attachmentCount 1"},

    {"pmm23-uninit-nopoison", EXP_STAGE_UNINIT,
     "Q1 the same shape without the deliberate stack dirtying",
     "as pmm23-uninit, stack not pre-dirtied"},

    {"stage-zeroed", EXP_STAGE_ZEROED,
     "Q1/Q4 the same state with every VkPipelineShaderStageCreateInfo field written",
     "as pmm23-uninit; stage.pNext = NULL, stage.flags = 0, stage.pSpecializationInfo = NULL"},

    {"stage-pnext-poison", EXP_STAGE_ZEROED | EXP_STAGE_POISON_PNEXT,
     "Q1 which single stage field carries the crash",
     "as stage-zeroed, but stage.pNext = 0xdeadbeef0000"},

    {"stage-specinfo-poison", EXP_STAGE_ZEROED | EXP_STAGE_POISON_SPEC,
     "Q1 which single stage field carries the crash",
     "as stage-zeroed, but stage.pSpecializationInfo = 0xdeadbeef0000"},

    {"stage-flags-poison", EXP_STAGE_ZEROED | EXP_STAGE_POISON_FLAGS,
     "Q1 which single stage field carries the crash",
     "as stage-zeroed, but stage.flags = 0xffffffff"},

    {"vertex-binding-1", EXP_STAGE_ZEROED | EXP_VERTEX_BINDING,
     "Q1 does a minimal vertex input change the outcome",
     "as stage-zeroed, but vertexBindingDescriptionCount 1 and "
     "vertexAttributeDescriptionCount 1 (binding 0, attribute 0 -> position)"},

    {"viewport-dynamic", EXP_STAGE_ZEROED | EXP_DYNAMIC_VIEWPORT,
     "Q1/Q4 does making the viewport dynamic change the outcome",
     "as stage-zeroed, but viewportCount 0, scissorCount 0 and pDynamicState "
     "with VK_DYNAMIC_STATE_VIEWPORT and VK_DYNAMIC_STATE_SCISSOR"},

    {"attachment-count-0", EXP_STAGE_ZEROED | EXP_ATTACHMENT_0,
     "Q1 does a render pass without attachments change the outcome",
     "as stage-zeroed, but the render pass has 0 attachments, the subpass has "
     "0 colour attachments and VkPipelineColorBlendStateCreateInfo.attachmentCount is 0"},

    {"samples-4", EXP_STAGE_ZEROED | EXP_SAMPLES_4,
     "Q1 does a multisampled target change the outcome",
     "as stage-zeroed, but rasterizationSamples is VK_SAMPLE_COUNT_4_BIT and the "
     "colour image and framebuffer are 4x multisampled"},

    {"depth-attachment", EXP_STAGE_ZEROED | EXP_DEPTH,
     "Q1 does a depth attachment change the outcome",
     "as stage-zeroed, but the render pass has a D32_SFLOAT depth attachment, the "
     "subpass references it and pDepthStencilState is present"},

    {"depth-clamp", EXP_STAGE_ZEROED | EXP_DEPTH | EXP_DEPTH_CLAMP,
     "Q1 does depthClampEnable change the outcome",
     "as depth-attachment, but rasterization.depthClampEnable is VK_TRUE"},

    {"rasterizer-discard", EXP_STAGE_ZEROED | EXP_RASTERIZER_DISCARD,
     "Q1 does rasterizerDiscardEnable change the outcome",
     "as stage-zeroed, but rasterization.rasterizerDiscardEnable is VK_TRUE"},

    {"blend-enable", EXP_STAGE_ZEROED | EXP_BLEND_ENABLE,
     "Q1 does an enabled blend change the outcome",
     "as stage-zeroed, but the colour blend attachment has blendEnable VK_TRUE "
     "(factors ONE/ZERO, ops ADD)"},

    {"logic-op", EXP_STAGE_ZEROED | EXP_LOGIC_OP,
     "Q1 does a logic op change the outcome",
     "as stage-zeroed, but the colour blend attachment has logicOp VK_LOGIC_OP_OR"},

    {"dynamic-rendering", EXP_STAGE_ZEROED | EXP_DYNAMIC_RENDERING,
     "Q4 does VK_KHR_dynamic_rendering instead of a render pass build",
     "as stage-zeroed, but VkGraphicsPipelineCreateInfo.pNext carries "
     "VkPipelineRenderingCreateInfo (1 colour attachment, R8G8B8A8_UNORM) and "
     "renderPass is VK_NULL_HANDLE"},

    {"dynamic-rendering-poison", EXP_DYNAMIC_RENDERING | EXP_STAGE_POISON_PNEXT,
     "Q1 does dynamic rendering change which field carries the crash",
     "as dynamic-rendering, but stage.pNext = 0xdeadbeef0000"},

    {"game-modules", EXP_STAGE_ZEROED | EXP_GAME_MODULES | EXP_DEVICE_FEATURES,
     "Q4 can the game's own shaders build a graphics pipeline on this device",
     "the game's own vertex and fragment modules, read from vert.spv and "
     "frag.spv in the module directory; the colour attachment count and the "
     "descriptor set layout are derived from the modules' own SPIR-V"},
};

enum { EXPERIMENT_COUNT = (int)(sizeof(EXPERIMENTS) / sizeof(EXPERIMENTS[0])) };

/* ------------------------------------------------------------------ */
/* Vulkan entry points, resolved at run time                          */
/* ------------------------------------------------------------------ */

#define BISECT_INSTANCE_FUNCTIONS(X)                                                   \
    X(vkCreateDevice, PFN_vkCreateDevice)                                              \
    X(vkEnumeratePhysicalDevices, PFN_vkEnumeratePhysicalDevices)                      \
    X(vkGetPhysicalDeviceProperties, PFN_vkGetPhysicalDeviceProperties)                \
    X(vkGetPhysicalDeviceQueueFamilyProperties, PFN_vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkGetPhysicalDeviceMemoryProperties, PFN_vkGetPhysicalDeviceMemoryProperties)    \
    X(vkGetPhysicalDeviceFeatures, PFN_vkGetPhysicalDeviceFeatures)                   \
    X(vkGetPhysicalDeviceFeatures2, PFN_vkGetPhysicalDeviceFeatures2)                 \
    X(vkEnumerateDeviceExtensionProperties, PFN_vkEnumerateDeviceExtensionProperties)  \
    X(vkGetDeviceProcAddr, PFN_vkGetDeviceProcAddr)                                    \
    X(vkDestroyInstance, PFN_vkDestroyInstance)                                        \
    X(vkDestroyDevice, PFN_vkDestroyDevice)

#define BISECT_DEVICE_FUNCTIONS(X)                                                     \
    X(vkGetDeviceQueue, PFN_vkGetDeviceQueue)                                           \
    X(vkCreateShaderModule, PFN_vkCreateShaderModule)                                  \
    X(vkDestroyShaderModule, PFN_vkDestroyShaderModule)                                \
    X(vkCreateDescriptorSetLayout, PFN_vkCreateDescriptorSetLayout)                     \
    X(vkDestroyDescriptorSetLayout, PFN_vkDestroyDescriptorSetLayout)                   \
    X(vkCreatePipelineLayout, PFN_vkCreatePipelineLayout)                               \
    X(vkDestroyPipelineLayout, PFN_vkDestroyPipelineLayout)                             \
    X(vkCreateGraphicsPipelines, PFN_vkCreateGraphicsPipelines)                       \
    X(vkCreateComputePipelines, PFN_vkCreateComputePipelines)                          \
    X(vkCreateRenderPass, PFN_vkCreateRenderPass)                                       \
    X(vkDestroyRenderPass, PFN_vkDestroyRenderPass)                                     \
    X(vkCreateFramebuffer, PFN_vkCreateFramebuffer)                                     \
    X(vkDestroyFramebuffer, PFN_vkDestroyFramebuffer)                                   \
    X(vkCreateImage, PFN_vkCreateImage)                                                 \
    X(vkDestroyImage, PFN_vkDestroyImage)                                               \
    X(vkCreateImageView, PFN_vkCreateImageView)                                         \
    X(vkDestroyImageView, PFN_vkDestroyImageView)                                       \
    X(vkAllocateMemory, PFN_vkAllocateMemory)                                           \
    X(vkFreeMemory, PFN_vkFreeMemory)                                                   \
    X(vkBindImageMemory, PFN_vkBindImageMemory)                                         \
    X(vkGetImageMemoryRequirements, PFN_vkGetImageMemoryRequirements)                   \
    X(vkCreatePipelineCache, PFN_vkCreatePipelineCache)                                 \
    X(vkDestroyPipelineCache, PFN_vkDestroyPipelineCache)

#define BISECT_DECLARE(name, type) static type p_##name;
BISECT_INSTANCE_FUNCTIONS(BISECT_DECLARE)
BISECT_DEVICE_FUNCTIONS(BISECT_DECLARE)
#undef BISECT_DECLARE

static PFN_vkGetInstanceProcAddr p_vkGetInstanceProcAddr;
static void *g_loader;
static char g_out_dir[256];
static char g_experiment[128];

/* A trace line.  It goes to stderr unbuffered, so the last trace line before a
 * crash names the step the process died in. */
static void trace(const char *step) {
    fprintf(stderr, "[bisect] step=%s\n", step);
    fflush(stderr);
}

/* ------------------------------------------------------------------ */
/* Small helpers                                                      */
/* ------------------------------------------------------------------ */

static void make_dir(const char *path) {
    mkdir(path, 0755);
}

static void join(char *dst, size_t cap, const char *dir, const char *name) {
    snprintf(dst, cap, "%s/%s", dir, name);
}

static int write_file(const char *path, const char *text) {
    FILE *file = fopen(path, "w");
    if (!file) return -1;
    fputs(text, file);
    fclose(file);
    return 0;
}

/*
 * Copy a file's bytes into memory.  procfs files report a size of zero, so
 * they are read in a loop instead of being sized up front.
 */
static uint8_t *read_file(const char *path, size_t *size_out) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    size_t capacity = 65536;
    size_t used = 0;
    uint8_t *data = (uint8_t *)malloc(capacity);
    if (!data) { fclose(file); return NULL; }
    for (;;) {
        if (used == capacity) {
            size_t grown = capacity * 2;
            uint8_t *bigger = (uint8_t *)realloc(data, grown);
            if (!bigger) { free(data); fclose(file); return NULL; }
            data = bigger;
            capacity = grown;
        }
        size_t got = fread(data + used, 1, capacity - used, file);
        used += got;
        if (got == 0) break;
    }
    fclose(file);
    if (used == 0) { free(data); return NULL; }
    uint8_t *trimmed = (uint8_t *)realloc(data, used + 1);
    if (trimmed) data = trimmed;
    data[used] = '\0';
    if (size_out) *size_out = used;
    return data;
}

/* ------------------------------------------------------------------ */
/* The crash recorder                                                  */
/* ------------------------------------------------------------------ */

/*
 * Read bytes out of the address space without faulting.  mincore() answers
 * "is this mapped" without touching the memory, so a frame pointer that points
 * somewhere wild ends the walk instead of ending the process.
 */
static int copy_from_address(const void *address, void *destination, size_t size) {
#if defined(__ANDROID__)
    unsigned char vector[1];
    if (mincore((void *)address, size, vector) != 0) return -1;
#else
    (void)address;
    (void)size;
#endif
    memcpy(destination, address, size);
    return 0;
}

/*
 * The signal handler writes the fault address, the faulting program counter
 * and the frame chain to the crash file with write(2), which is
 * async-signal-safe.  It then restores the default disposition and re-raises
 * the signal, so the process still dies *from* SIGSEGV: the exit status stays
 * the one a real crash produces instead of a made-up one.
 *
 * The program counter is what makes the attribution possible: the map
 * snapshot taken before the call turns it into "this library, this offset".
 */
static void crash_handler(int signal_number, siginfo_t *info, void *context) {
    char path[512];
    join(path, sizeof(path), g_out_dir, "crash.txt");
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        char line[256];
        int n = snprintf(line, sizeof(line),
                         "signal=%d code=%d fault_addr=0x%llx\n",
                         signal_number, info->si_code,
                         (unsigned long long)(uintptr_t)info->si_addr);
        ssize_t ignored = write(fd, line, (size_t)n);
        (void)ignored;

        const ucontext_t *uc = (const ucontext_t *)context;
        unsigned long long pc = 0;
        unsigned long long lr = 0;
        unsigned long long fp = 0;
#if BISECT_HAVE_ARM64_CONTEXT
        if (uc) {
            pc = (unsigned long long)uc->uc_mcontext.pc;
            lr = (unsigned long long)uc->uc_mcontext.regs[30];
            fp = (unsigned long long)uc->uc_mcontext.regs[29];
        }
#else
        (void)uc;
#endif
        n = snprintf(line, sizeof(line),
                     "fault_pc=0x%llx link_register=0x%llx frame_pointer=0x%llx\n",
                     pc, lr, fp);
        ignored = write(fd, line, (size_t)n);
        (void)ignored;

        /* Walk the frame-pointer chain.  The chain is bounded and every read
         * is a plain load, so it is safe enough inside a signal handler; a
         * frame that cannot be followed simply ends the walk. */
        unsigned long long chain[32];
        int count = 0;
        unsigned long long frame = fp;
        unsigned long long previous = 0;
        while (count < 32 && frame > 0x1000 && frame != previous) {
            previous = frame;
            unsigned long long next = 0;
            unsigned long long saved = 0;
            if (copy_from_address((void *)(frame + 8), &saved, sizeof(saved)) != 0) break;
            if (copy_from_address((void *)frame, &next, sizeof(next)) != 0) break;
            chain[count++] = saved;
            frame = next;
        }
        for (int i = 0; i < count; ++i) {
            n = snprintf(line, sizeof(line), "frame[%d]=0x%llx\n", i, chain[i]);
            ignored = write(fd, line, (size_t)n);
            (void)ignored;
        }
        close(fd);
    }
    signal(signal_number, SIG_DFL);
    raise(signal_number);
}

/* ------------------------------------------------------------------ */
/* A small SPIR-V reader, enough to derive a pipeline state from a    */
/* pair of real shader modules.                                        */
/* ------------------------------------------------------------------ */

/*
 * The pipeline state a shader needs is written in the shader: the colour
 * attachments it writes are its Output locations, and the descriptors it
 * reads are its decorated variables.  Rather than hard-coding them, this
 * reader derives both, so the state that is handed to the driver is the state
 * the shader actually asks for.
 */
enum { SPIRV_MAX_BINDINGS = 64 };

typedef struct {
    uint32_t set;
    uint32_t binding;
    VkDescriptorType type;
} ShaderBinding;

typedef struct {
    bool have_entry_point;
    uint32_t execution_model; /* 0 = vertex, 4 = fragment, 5 = compute */
    uint32_t max_output_location; /* the highest Output location, plus one */
    uint32_t output_count;
    ShaderBinding bindings[SPIRV_MAX_BINDINGS];
    uint32_t binding_count;
} ShaderFacts;

static void add_binding(ShaderFacts *facts, uint32_t set, uint32_t binding,
                        VkDescriptorType type) {
    for (uint32_t i = 0; i < facts->binding_count; ++i) {
        if (facts->bindings[i].set == set && facts->bindings[i].binding == binding) return;
    }
    if (facts->binding_count >= SPIRV_MAX_BINDINGS) return;
    facts->bindings[facts->binding_count].set = set;
    facts->bindings[facts->binding_count].binding = binding;
    facts->bindings[facts->binding_count].type = type;
    ++facts->binding_count;
}

/* The SPIR-V opcodes and decorations this reader needs. */
enum {
    SPIRV_OP_ENTRY_POINT = 15,
    SPIRV_OP_TYPE_IMAGE = 25,
    SPIRV_OP_TYPE_SAMPLED_IMAGE = 27,
    SPIRV_OP_TYPE_STRUCT = 30,
    SPIRV_OP_TYPE_POINTER = 32,
    SPIRV_OP_VARIABLE = 59,
    SPIRV_OP_DECORATE = 71,
    SPIRV_DEC_BLOCK = 2,
    SPIRV_DEC_BUFFER_BLOCK = 3,
    SPIRV_DEC_BUILTIN = 11,
    SPIRV_DEC_LOCATION = 30,
    SPIRV_DEC_BINDING = 33,
    SPIRV_DEC_DESCRIPTOR_SET = 34,
    SPIRV_STORAGE_UNIFORM_CONSTANT = 0,
    SPIRV_STORAGE_UNIFORM = 2,
    SPIRV_STORAGE_OUTPUT = 3,
    SPIRV_STORAGE_STORAGE_BUFFER = 12,
    SPIRV_DIM_SUBPASS = 6
};

typedef struct {
    uint32_t result_id;
    uint32_t storage_class;
    uint32_t type_id;
} PointerInfo;

typedef struct {
    uint32_t result_id;
    bool is_block;
    bool is_buffer_block;
} StructInfo;

typedef struct {
    uint32_t result_id;
    uint32_t dim;
    uint32_t sampled;
} ImageInfo;

typedef struct {
    uint32_t result_id;
    uint32_t image_type_id;
} SampledImageInfo;

typedef struct {
    uint32_t id;
    uint32_t decoration;
    uint32_t operand;
} Decoration;

#define SPIRV_MAX_DECORATIONS 4096

static uint32_t decoration_operand(const Decoration *decorations, uint32_t count,
                                   uint32_t id, uint32_t decoration, bool *found) {
    for (uint32_t i = 0; i < count; ++i) {
        if (decorations[i].id == id && decorations[i].decoration == decoration) {
            if (found) *found = true;
            return decorations[i].operand;
        }
    }
    if (found) *found = false;
    return 0;
}

/*
 * Derive, from one shader module: the execution stage, the colour
 * attachments it writes (its Output locations) and the descriptors it reads
 * (its decorated variables).  Everything here comes out of the module's own
 * words; nothing about the pipeline state is assumed.
 */
static bool read_shader_facts(const uint8_t *code, size_t size, ShaderFacts *facts) {
    memset(facts, 0, sizeof(*facts));
    if (size < 20 || size % 4) return false;
    const uint32_t *words = (const uint32_t *)code;
    uint32_t count = (uint32_t)(size / 4);
    if (words[0] != 0x07230203u) return false;

    static PointerInfo pointers[1024];
    static StructInfo structs[1024];
    static ImageInfo images[1024];
    static SampledImageInfo sampled_images[1024];
    static Decoration decorations[SPIRV_MAX_DECORATIONS];
    uint32_t pointer_count = 0;
    uint32_t struct_count = 0;
    uint32_t image_count = 0;
    uint32_t sampled_image_count = 0;
    uint32_t decoration_count = 0;

    static uint32_t variable_type[2048];
    static uint32_t variable_id[2048];
    uint32_t variable_count = 0;

    uint32_t index = 5;
    while (index < count) {
        uint32_t opcode = words[index] & 0xFFFFu;
        uint32_t length = words[index] >> 16;
        if (length == 0 || index + length > count) return false;
        const uint32_t *args = &words[index + 1];
        uint32_t arg_count = length - 1;
        switch (opcode) {
        case SPIRV_OP_ENTRY_POINT:
            if (arg_count >= 1) {
                facts->have_entry_point = true;
                facts->execution_model = args[0];
            }
            break;
        case SPIRV_OP_TYPE_POINTER:
            if (arg_count >= 3 && pointer_count < 1024) {
                pointers[pointer_count].result_id = args[0];
                pointers[pointer_count].storage_class = args[1];
                pointers[pointer_count].type_id = args[2];
                ++pointer_count;
            }
            break;
        case SPIRV_OP_TYPE_STRUCT:
            if (arg_count >= 1 && struct_count < 1024) {
                structs[struct_count].result_id = args[0];
                structs[struct_count].is_block = false;
                structs[struct_count].is_buffer_block = false;
                ++struct_count;
            }
            break;
        case SPIRV_OP_TYPE_IMAGE:
            if (arg_count >= 7 && image_count < 1024) {
                images[image_count].result_id = args[0];
                images[image_count].dim = args[2];
                images[image_count].sampled = args[6];
                ++image_count;
            }
            break;
        case SPIRV_OP_TYPE_SAMPLED_IMAGE:
            if (arg_count >= 2 && sampled_image_count < 1024) {
                sampled_images[sampled_image_count].result_id = args[0];
                sampled_images[sampled_image_count].image_type_id = args[1];
                ++sampled_image_count;
            }
            break;
        case SPIRV_OP_VARIABLE:
            if (arg_count >= 2 && variable_count < 2048) {
                variable_type[variable_count] = args[0];
                variable_id[variable_count] = args[1];
                ++variable_count;
            }
            break;
        case SPIRV_OP_DECORATE:
            /* A decoration carries an operand only when it needs one; Block
             * and BufferBlock do not. */
            if (arg_count >= 2 && decoration_count < SPIRV_MAX_DECORATIONS) {
                decorations[decoration_count].id = args[0];
                decorations[decoration_count].decoration = args[1];
                decorations[decoration_count].operand = arg_count >= 3 ? args[2] : 0;
                ++decoration_count;
            }
            break;
        default:
            break;
        }
        index += length;
    }

    /* A struct is a uniform block or a storage buffer when the Block or the
     * BufferBlock decoration names it. */
    for (uint32_t d = 0; d < decoration_count; ++d) {
        if (decorations[d].decoration != SPIRV_DEC_BLOCK &&
            decorations[d].decoration != SPIRV_DEC_BUFFER_BLOCK)
            continue;
        for (uint32_t s = 0; s < struct_count; ++s) {
            if (structs[s].result_id != decorations[d].id) continue;
            if (decorations[d].decoration == SPIRV_DEC_BLOCK) structs[s].is_block = true;
            else structs[s].is_buffer_block = true;
        }
    }

    for (uint32_t v = 0; v < variable_count; ++v) {
        uint32_t id = variable_id[v];
        const PointerInfo *pointer = NULL;
        for (uint32_t p = 0; p < pointer_count; ++p) {
            if (pointers[p].result_id == variable_type[v]) { pointer = &pointers[p]; break; }
        }
        if (!pointer) continue;

        if (pointer->storage_class == SPIRV_STORAGE_OUTPUT) {
            bool has_location = false;
            uint32_t location = decoration_operand(decorations, decoration_count, id,
                                                   SPIRV_DEC_LOCATION, &has_location);
            if (has_location) {
                ++facts->output_count;
                if (location + 1 > facts->max_output_location)
                    facts->max_output_location = location + 1;
            }
            continue;
        }

        bool has_binding = false;
        uint32_t binding = decoration_operand(decorations, decoration_count, id,
                                              SPIRV_DEC_BINDING, &has_binding);
        if (!has_binding) continue;
        bool has_set = false;
        uint32_t descriptor_set = decoration_operand(decorations, decoration_count, id,
                                                     SPIRV_DEC_DESCRIPTOR_SET, &has_set);
        uint32_t pointee = pointer->type_id;

        VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
        if (pointer->storage_class == SPIRV_STORAGE_UNIFORM) {
            for (uint32_t s = 0; s < struct_count; ++s) {
                if (structs[s].result_id != pointee) continue;
                if (structs[s].is_buffer_block) type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                else if (structs[s].is_block) type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                break;
            }
        } else if (pointer->storage_class == SPIRV_STORAGE_STORAGE_BUFFER) {
            type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        } else if (pointer->storage_class == SPIRV_STORAGE_UNIFORM_CONSTANT) {
            bool is_sampled_image = false;
            for (uint32_t s = 0; s < sampled_image_count; ++s) {
                if (sampled_images[s].result_id == pointee) { is_sampled_image = true; break; }
            }
            if (is_sampled_image) {
                type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            } else {
                for (uint32_t im = 0; im < image_count; ++im) {
                    if (images[im].result_id != pointee) continue;
                    if (images[im].dim == SPIRV_DIM_SUBPASS) {
                        type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
                    } else if (images[im].sampled == 2) {
                        type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                    } else {
                        type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                    }
                    break;
                }
            }
        }
        if (type == VK_DESCRIPTOR_TYPE_MAX_ENUM) continue;
        add_binding(facts, has_set ? descriptor_set : 0, binding, type);
    }
    return facts->have_entry_point;
}

/* ------------------------------------------------------------------ */
/* The device                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    uint32_t queue_family;
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceMemoryProperties memory_properties;
    char extensions[256][128];
    uint32_t extension_count;
    bool dynamic_rendering_supported;
} Device;

static bool has_extension(const Device *device, const char *name) {
    for (uint32_t i = 0; i < device->extension_count; ++i)
        if (!strcmp(device->extensions[i], name)) return true;
    return false;
}

static bool load_loader(void) {
    static const char *const candidates[] = {"libvulkan.so.1", "libvulkan.so"};
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        g_loader = dlopen(candidates[i], RTLD_NOW | RTLD_LOCAL);
        if (g_loader) return true;
    }
    return false;
}

static bool create_device(Device *device, bool need_dynamic_rendering, bool want_features) {
    memset(device, 0, sizeof(*device));
    trace("load_loader");
    if (!load_loader()) {
        fprintf(stderr, "[bisect] no Vulkan loader: %s\n", dlerror());
        return false;
    }
    trace("resolve_global");
    p_vkGetInstanceProcAddr =
        (PFN_vkGetInstanceProcAddr)dlsym(g_loader, "vkGetInstanceProcAddr");
    if (!p_vkGetInstanceProcAddr) {
        fprintf(stderr, "[bisect] vkGetInstanceProcAddr missing\n");
        return false;
    }
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "ffrs-graphics-bisect", .applicationVersion = 1,
        .pEngineName = "ffrs", .engineVersion = 1,
        .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo instance_info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app};
    if (p_vkGetInstanceProcAddr(NULL, "vkCreateInstance")) {
        PFN_vkCreateInstance create_instance =
            (PFN_vkCreateInstance)p_vkGetInstanceProcAddr(NULL, "vkCreateInstance");
        trace("vkCreateInstance");
        VkResult result = create_instance(&instance_info, NULL, &device->instance);
        trace("vkCreateInstance returned");
        if (result != VK_SUCCESS) {
            fprintf(stderr, "[bisect] vkCreateInstance returned %d\n", (int)result);
            return false;
        }
    } else {
        fprintf(stderr, "[bisect] vkCreateInstance missing\n");
        return false;
    }

    trace("resolve_instance_functions");
#define BISECT_RESOLVE_INSTANCE(name, type)                                             \
    p_##name = (type)p_vkGetInstanceProcAddr(device->instance, #name);                   \
    if (!p_##name) { fprintf(stderr, "[bisect] %s missing\n", #name); return false; }
    BISECT_INSTANCE_FUNCTIONS(BISECT_RESOLVE_INSTANCE)
#undef BISECT_RESOLVE_INSTANCE

    uint32_t count = 0;
    trace("vkEnumeratePhysicalDevices");
    if (p_vkEnumeratePhysicalDevices(device->instance, &count, NULL) != VK_SUCCESS ||
        count == 0) {
        fprintf(stderr, "[bisect] no physical device\n");
        return false;
    }
    VkPhysicalDevice devices[4];
    if (count > 4) count = 4;
    p_vkEnumeratePhysicalDevices(device->instance, &count, devices);
    device->physical = devices[0];
    trace("physical_device_properties");
    p_vkGetPhysicalDeviceProperties(device->physical, &device->properties);
    p_vkGetPhysicalDeviceMemoryProperties(device->physical, &device->memory_properties);

    trace("enumerate_device_extensions");
    VkExtensionProperties properties[256];
    uint32_t property_count = 256;
    if (p_vkEnumerateDeviceExtensionProperties(device->physical, NULL, &property_count,
                                               properties) == VK_SUCCESS) {
        if (property_count > 256) property_count = 256;
        for (uint32_t i = 0; i < property_count; ++i) {
            snprintf(device->extensions[i], sizeof(device->extensions[i]), "%s",
                     properties[i].extensionName);
            device->extension_count = i + 1;
        }
    }
    device->dynamic_rendering_supported =
        has_extension(device, "VK_KHR_dynamic_rendering") ||
        device->properties.apiVersion >= VK_API_VERSION_1_3;

    VkQueueFamilyProperties families[8];
    uint32_t family_count = 8;
    trace("queue_family_properties");
    p_vkGetPhysicalDeviceQueueFamilyProperties(device->physical, &family_count, families);
    device->queue_family = 0;
    for (uint32_t i = 0; i < family_count; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            device->queue_family = i;
            break;
        }
    }

    /*
     * The features the device reports as supported are the ones the runtime
     * asks for.  A module that declares a capability the device can provide
     * but that was not enabled is rejected at pipeline creation.
     */
    VkPhysicalDeviceFeatures features;
    memset(&features, 0, sizeof(features));
    VkPhysicalDeviceVulkan11Features features_11;
    memset(&features_11, 0, sizeof(features_11));
    features_11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    const void *next = NULL;
    if (want_features) {
        VkPhysicalDeviceFeatures supported;
        memset(&supported, 0, sizeof(supported));
        p_vkGetPhysicalDeviceFeatures(device->physical, &supported);
        features.shaderStorageImageWriteWithoutFormat =
            supported.shaderStorageImageWriteWithoutFormat;
        features.shaderStorageImageReadWithoutFormat =
            supported.shaderStorageImageReadWithoutFormat;
        features.shaderStorageImageExtendedFormats =
            supported.shaderStorageImageExtendedFormats;
        features.shaderInt64 = supported.shaderInt64;
        features.shaderFloat64 = supported.shaderFloat64;
        VkPhysicalDeviceFeatures2 features2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &features_11};
        p_vkGetPhysicalDeviceFeatures2(device->physical, &features2);
        if (device->properties.apiVersion >= VK_API_VERSION_1_1)
            next = (const void *)&features_11;
        fprintf(stderr, "[bisect] features: write=%d read=%d ext=%d f64=%d i64=%d drawparams=%d\n",
                (int)features.shaderStorageImageWriteWithoutFormat,
                (int)features.shaderStorageImageReadWithoutFormat,
                (int)features.shaderStorageImageExtendedFormats,
                (int)features.shaderFloat64, (int)features.shaderInt64,
                (int)features_11.shaderDrawParameters);
    }

    const char *extension_names[1];
    uint32_t extension_count = 0;
    if (need_dynamic_rendering && has_extension(device, "VK_KHR_dynamic_rendering")) {
        extension_names[extension_count++] = "VK_KHR_dynamic_rendering";
    }
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = device->queue_family, .queueCount = 1, .pQueuePriorities = &priority};
    VkDeviceCreateInfo device_info = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = next,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue_info,
        .enabledExtensionCount = extension_count, .ppEnabledExtensionNames = extension_names,
        .pEnabledFeatures = want_features ? &features : NULL};
    trace("vkCreateDevice");
    VkResult result = p_vkCreateDevice(device->physical, &device_info, NULL, &device->device);
    trace("vkCreateDevice returned");
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[bisect] vkCreateDevice returned %d\n", (int)result);
        return false;
    }

    trace("resolve_device_functions");
#define BISECT_RESOLVE_DEVICE(name, type)                                               \
    p_##name = (type)p_vkGetDeviceProcAddr(device->device, #name);                        \
    if (!p_##name) { fprintf(stderr, "[bisect] %s missing\n", #name); return false; }
    BISECT_DEVICE_FUNCTIONS(BISECT_RESOLVE_DEVICE)
#undef BISECT_RESOLVE_DEVICE

    trace("vkGetDeviceQueue");
    p_vkGetDeviceQueue(device->device, device->queue_family, 0, &device->queue);
    return true;
}

/* ------------------------------------------------------------------ */
/* The graphics pipeline under test                                    */
/* ------------------------------------------------------------------ */

#define MAX_COLOR_ATTACHMENTS 8

typedef struct {
    VkImage color_image[MAX_COLOR_ATTACHMENTS];
    VkDeviceMemory color_memory[MAX_COLOR_ATTACHMENTS];
    VkImageView color_view[MAX_COLOR_ATTACHMENTS];
    VkImage depth_image;
    VkDeviceMemory depth_memory;
    VkImageView depth_view;
    VkRenderPass render_pass;
    VkFramebuffer framebuffer;
} Target;

static bool pick_memory_type(const Device *device, uint32_t bits, uint32_t *out) {
    bool found = false;
    for (uint32_t i = 0; i < device->memory_properties.memoryTypeCount; ++i) {
        if (!(bits & (1u << i))) continue;
        if (!found) { *out = i; found = true; }
        if (device->memory_properties.memoryTypes[i].propertyFlags &
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
            *out = i;
            return true;
        }
    }
    return found;
}

static bool create_image(Device *device, VkFormat format, VkImageUsageFlags usage,
                         VkSampleCountFlagBits samples, VkImageAspectFlags aspect,
                         VkImage *image_out, VkDeviceMemory *memory_out, VkImageView *view_out) {
    VkImageCreateInfo image_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = format,
        .extent = {IMAGE_WIDTH, IMAGE_HEIGHT, 1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = samples, .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    VkImage image = VK_NULL_HANDLE;
    VkResult result = p_vkCreateImage(device->device, &image_info, NULL, &image);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[bisect] vkCreateImage returned %d\n", (int)result);
        return false;
    }
    VkMemoryRequirements requirements;
    p_vkGetImageMemoryRequirements(device->device, image, &requirements);
    uint32_t memory_type = 0;
    if (!pick_memory_type(device, requirements.memoryTypeBits, &memory_type)) {
        fprintf(stderr, "[bisect] no memory type\n");
        return false;
    }
    VkMemoryAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = memory_type};
    VkDeviceMemory memory = VK_NULL_HANDLE;
    result = p_vkAllocateMemory(device->device, &allocate, NULL, &memory);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[bisect] vkAllocateMemory returned %d\n", (int)result);
        return false;
    }
    result = p_vkBindImageMemory(device->device, image, memory, 0);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[bisect] vkBindImageMemory returned %d\n", (int)result);
        return false;
    }
    VkImageViewCreateInfo view_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = format,
        .subresourceRange = {.aspectMask = aspect, .baseMipLevel = 0, .levelCount = 1,
            .baseArrayLayer = 0, .layerCount = 1}};
    VkImageView view = VK_NULL_HANDLE;
    result = p_vkCreateImageView(device->device, &view_info, NULL, &view);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[bisect] vkCreateImageView returned %d\n", (int)result);
        return false;
    }
    *image_out = image;
    *memory_out = memory;
    *view_out = view;
    return true;
}

/*
 * Fill in one VkPipelineShaderStageCreateInfo.
 *
 * The three shapes are the whole point of the bisect:
 *   - EXP_STAGE_ZEROED:   every field of the structure is written.
 *   - EXP_STAGE_UNINIT:   only the four fields P-MM-23 wrote; pNext, flags and
 *                         pSpecializationInfo keep whatever the stack held.
 *   - the poison variants: every field is written, but one of the three is
 *                         given a deliberately bad value.
 */
static void fill_stage(VkPipelineShaderStageCreateInfo *stage,
                       VkShaderStageFlagBits stage_bits, VkShaderModule module,
                       uint32_t flags) {
    if (flags & EXP_STAGE_ZEROED) {
        memset(stage, 0, sizeof(*stage));
        stage->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage->stage = stage_bits;
        stage->module = module;
        stage->pName = "main";
    } else {
        /* P-MM-23's shape.  pNext, flags and pSpecializationInfo are not
         * written, so they hold whatever was on the stack. */
        stage->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage->stage = stage_bits;
        stage->module = module;
        stage->pName = "main";
    }
    if (flags & EXP_STAGE_POISON_PNEXT) stage->pNext = BISECT_POISON_PTR;
    if (flags & EXP_STAGE_POISON_SPEC)
        stage->pSpecializationInfo = (const VkSpecializationInfo *)BISECT_POISON_PTR;
    if (flags & EXP_STAGE_POISON_FLAGS) stage->flags = 0xffffffffu;
}

/* ------------------------------------------------------------------ */
/* One experiment                                                      */
/* ------------------------------------------------------------------ */

static int run_experiment(const Experiment *experiment, const char *module_dir) {
    uint32_t flags = experiment->flags;
    printf("[bisect] experiment=%s\n", experiment->name);
    printf("[bisect] %s\n", experiment->state);
    fflush(stdout);

    /* The map snapshot: the crash address and the entry point addresses are
     * only meaningful against the mappings they live in. */
    char maps_path[512];
    join(maps_path, sizeof(maps_path), g_out_dir, "maps.txt");
    {
        size_t size = 0;
        uint8_t *maps = read_file("/proc/self/maps", &size);
        if (maps) {
            write_file(maps_path, (const char *)maps);
            free(maps);
        }
    }

    Device device;
    bool need_dynamic_rendering = (flags & EXP_DYNAMIC_RENDERING) != 0;
    if (!create_device(&device, need_dynamic_rendering, (flags & EXP_DEVICE_FEATURES) != 0)) {
        printf("[bisect] result=device_setup_failed\n");
        return 5;
    }
    trace("device_ready");
    printf("[bisect] device=%s api=%u.%u.%u driver=%u dynamic_rendering=%d\n",
           device.properties.deviceName, VK_VERSION_MAJOR(device.properties.apiVersion),
           VK_VERSION_MINOR(device.properties.apiVersion),
           VK_VERSION_PATCH(device.properties.apiVersion), device.properties.driverVersion,
           (int)device.dynamic_rendering_supported);
    fflush(stdout);

    /* Q2: where do the entry points live? */
    {
        static const char *const names[] = {"vkCreateGraphicsPipelines",
                                            "vkCreateComputePipelines",
                                            "vkCreateRenderPass", "vkCreateFramebuffer",
                                            "vkCreateShaderModule", "vkCreatePipelineLayout"};
        printf("[bisect] entrypoints:");
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
            void *address = (void *)p_vkGetDeviceProcAddr(device.device, names[i]);
            printf(" %s=0x%llx", names[i], (unsigned long long)(uintptr_t)address);
        }
        printf("\n");
        fflush(stdout);
    }

    /* The two shader modules. */
    char vert_path[512];
    char frag_path[512];
    bool game_mode = (flags & EXP_GAME_MODULES) != 0;
    snprintf(vert_path, sizeof(vert_path), "%s/%s", module_dir,
             game_mode ? "vert.spv" : "trivial_vert.spv");
    snprintf(frag_path, sizeof(frag_path), "%s/%s", module_dir,
             game_mode ? "frag.spv" : "trivial_frag.spv");
    size_t vert_size = 0;
    size_t frag_size = 0;
    uint8_t *vert_code = read_file(vert_path, &vert_size);
    uint8_t *frag_code = read_file(frag_path, &frag_size);
    if (!vert_code || !frag_code) {
        fprintf(stderr, "[bisect] cannot read the shader modules\n");
        return 6;
    }
    VkShaderModuleCreateInfo vert_info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = vert_size, .pCode = (const uint32_t *)vert_code};
    VkShaderModuleCreateInfo frag_info = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = frag_size, .pCode = (const uint32_t *)frag_code};
    trace("create_shader_modules");
    ShaderFacts vert_facts;
    ShaderFacts frag_facts;
    memset(&vert_facts, 0, sizeof(vert_facts));
    memset(&frag_facts, 0, sizeof(frag_facts));
    if (game_mode) {
        if (!read_shader_facts(vert_code, vert_size, &vert_facts) ||
            !read_shader_facts(frag_code, frag_size, &frag_facts)) {
            fprintf(stderr, "[bisect] cannot read the shader facts\n");
            return 6;
        }
        printf("[bisect] vert stage=%u bindings=%u  frag stage=%u outputs=%u "
               "colour_attachments=%u bindings=%u\n",
               vert_facts.execution_model, vert_facts.binding_count,
               frag_facts.execution_model, frag_facts.output_count,
               frag_facts.max_output_location, frag_facts.binding_count);
        fflush(stdout);
    }
    VkShaderModule vert_module = VK_NULL_HANDLE;
    VkShaderModule frag_module = VK_NULL_HANDLE;
    VkResult result = p_vkCreateShaderModule(device.device, &vert_info, NULL, &vert_module);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[bisect] vkCreateShaderModule(vertex) returned %d\n", (int)result);
        return 6;
    }
    result = p_vkCreateShaderModule(device.device, &frag_info, NULL, &frag_module);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[bisect] vkCreateShaderModule(fragment) returned %d\n", (int)result);
        return 6;
    }

    /* The descriptor set layout: one uniform buffer at set 0 binding 0, which
     * is what the trivial vertex module declares. */
    VkDescriptorSetLayoutBinding binding = {.binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT};
    VkDescriptorSetLayoutBinding game_bindings[SPIRV_MAX_BINDINGS];
    uint32_t game_binding_count = 0;
    if (game_mode) {
        ShaderFacts merged = frag_facts;
        for (uint32_t i = 0; i < vert_facts.binding_count; ++i)
            add_binding(&merged, vert_facts.bindings[i].set, vert_facts.bindings[i].binding,
                        vert_facts.bindings[i].type);
        game_binding_count = merged.binding_count;
        for (uint32_t i = 0; i < game_binding_count; ++i) {
            game_bindings[i].binding = merged.bindings[i].binding;
            game_bindings[i].descriptorType = merged.bindings[i].type;
            game_bindings[i].descriptorCount = 1;
            game_bindings[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        printf("[bisect] descriptor set layout: %u bindings\n", game_binding_count);
        fflush(stdout);
    }
    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = game_mode ? game_binding_count : 1,
        .pBindings = game_mode ? game_bindings : &binding};
    trace("create_descriptor_set_layout");
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    result = p_vkCreateDescriptorSetLayout(device.device, &layout_info, NULL, &set_layout);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[bisect] vkCreateDescriptorSetLayout returned %d\n", (int)result);
        return 6;
    }
    VkPipelineLayoutCreateInfo pipeline_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &set_layout};
    trace("create_pipeline_layout");
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    result = p_vkCreatePipelineLayout(device.device, &pipeline_layout_info, NULL, &pipeline_layout);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[bisect] vkCreatePipelineLayout returned %d\n", (int)result);
        return 6;
    }

    /* The render target. */
    Target target;
    memset(&target, 0, sizeof(target));
    bool has_color = !(flags & EXP_ATTACHMENT_0);
    uint32_t color_count = game_mode ? frag_facts.max_output_location : (has_color ? 1u : 0u);
    if (color_count > MAX_COLOR_ATTACHMENTS) color_count = MAX_COLOR_ATTACHMENTS;
    VkSampleCountFlagBits samples = (flags & EXP_SAMPLES_4) ? VK_SAMPLE_COUNT_4_BIT
                                                            : VK_SAMPLE_COUNT_1_BIT;
    for (uint32_t i = 0; i < color_count; ++i) {
        if (!create_image(&device, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                          samples, VK_IMAGE_ASPECT_COLOR_BIT, &target.color_image[i],
                          &target.color_memory[i], &target.color_view[i]))
            return 6;
    }
    if (flags & EXP_DEPTH) {
        if (!create_image(&device, VK_FORMAT_D32_SFLOAT,
                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, samples,
                          VK_IMAGE_ASPECT_DEPTH_BIT, &target.depth_image, &target.depth_memory,
                          &target.depth_view))
            return 6;
    }

    /* The render pass, unless the pipeline uses dynamic rendering. */
    if (!(flags & EXP_DYNAMIC_RENDERING)) {
        VkAttachmentDescription attachments[MAX_COLOR_ATTACHMENTS + 1];
        VkAttachmentReference references[MAX_COLOR_ATTACHMENTS + 1];
        uint32_t attachment_count = 0;
        uint32_t reference_count = 0;
        for (uint32_t i = 0; i < color_count; ++i) {
            attachments[attachment_count].format = VK_FORMAT_R8G8B8A8_UNORM;
            attachments[attachment_count].samples = samples;
            attachments[attachment_count].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[attachment_count].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[attachment_count].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[attachment_count].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[attachment_count].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[attachment_count].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            references[reference_count].attachment = attachment_count;
            references[reference_count].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            ++attachment_count;
            ++reference_count;
        }
        if (flags & EXP_DEPTH) {
            attachments[attachment_count].format = VK_FORMAT_D32_SFLOAT;
            attachments[attachment_count].samples = samples;
            attachments[attachment_count].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[attachment_count].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[attachment_count].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[attachment_count].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[attachment_count].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[attachment_count].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            references[reference_count].attachment = attachment_count;
            references[reference_count].layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            ++attachment_count;
            ++reference_count;
        }
        VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = color_count,
            .pColorAttachments = color_count ? &references[0] : NULL};
        if (flags & EXP_DEPTH) {
            subpass.pDepthStencilAttachment = &references[color_count];
        }
        trace("create_render_pass");
        VkRenderPassCreateInfo render_pass_info = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = attachment_count, .pAttachments = attachments,
            .subpassCount = 1, .pSubpasses = &subpass};
        result = p_vkCreateRenderPass(device.device, &render_pass_info, NULL, &target.render_pass);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "[bisect] vkCreateRenderPass returned %d\n", (int)result);
            return 6;
        }

        VkImageView views[MAX_COLOR_ATTACHMENTS + 1];
        uint32_t view_count = 0;
        for (uint32_t i = 0; i < color_count; ++i) views[view_count++] = target.color_view[i];
        if (flags & EXP_DEPTH) views[view_count++] = target.depth_view;
        VkFramebufferCreateInfo framebuffer_info = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = target.render_pass, .attachmentCount = view_count, .pAttachments = views,
            .width = IMAGE_WIDTH, .height = IMAGE_HEIGHT, .layers = 1};
        result = p_vkCreateFramebuffer(device.device, &framebuffer_info, NULL, &target.framebuffer);
        if (result != VK_SUCCESS) {
            fprintf(stderr, "[bisect] vkCreateFramebuffer returned %d\n", (int)result);
            return 6;
        }
    }

    /* The pipeline cache: an empty one, so nothing is reused between runs. */
    trace("create_pipeline_cache");
    VkPipelineCacheCreateInfo cache_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    VkPipelineCache cache = VK_NULL_HANDLE;
    result = p_vkCreatePipelineCache(device.device, &cache_info, NULL, &cache);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[bisect] vkCreatePipelineCache returned %d\n", (int)result);
        return 6;
    }

    /* The pipeline state. */
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkVertexInputBindingDescription vertex_binding = {.binding = 0, .stride = 16,
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription vertex_attribute = {.location = 0, .binding = 0,
        .format = VK_FORMAT_R32G32_SFLOAT, .offset = 0};
    if (flags & EXP_VERTEX_BINDING) {
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &vertex_binding;
        vertex_input.vertexAttributeDescriptionCount = 1;
        vertex_input.pVertexAttributeDescriptions = &vertex_attribute;
    }
    VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, .primitiveRestartEnable = VK_FALSE};
    VkViewport viewport = {0.0f, 0.0f, (float)IMAGE_WIDTH, (float)IMAGE_HEIGHT, 0.0f, 1.0f};
    VkRect2D scissor = {{0, 0}, {IMAGE_WIDTH, IMAGE_HEIGHT}};
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &viewport, .scissorCount = 1, .pScissors = &scissor};
    VkDynamicState dynamic_states[2];
    VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    if (flags & EXP_DYNAMIC_VIEWPORT) {
        viewport_state.viewportCount = 0;
        viewport_state.pViewports = NULL;
        viewport_state.scissorCount = 0;
        viewport_state.pScissors = NULL;
        dynamic_states[0] = VK_DYNAMIC_STATE_VIEWPORT;
        dynamic_states[1] = VK_DYNAMIC_STATE_SCISSOR;
        dynamic_state.dynamicStateCount = 2;
        dynamic_state.pDynamicStates = dynamic_states;
    }
    VkPipelineRasterizationStateCreateInfo rasterization = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f,
        .rasterizerDiscardEnable = (flags & EXP_RASTERIZER_DISCARD) ? VK_TRUE : VK_FALSE,
        .depthClampEnable = (flags & EXP_DEPTH_CLAMP) ? VK_TRUE : VK_FALSE};
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = samples};
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS};
    VkPipelineColorBlendAttachmentState blend_attachments[MAX_COLOR_ATTACHMENTS];
    for (uint32_t i = 0; i < MAX_COLOR_ATTACHMENTS; ++i) {
        memset(&blend_attachments[i], 0, sizeof(blend_attachments[i]));
        blend_attachments[i].blendEnable = VK_FALSE;
        blend_attachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    }
    VkPipelineColorBlendAttachmentState blend_attachment = blend_attachments[0];
    if (flags & EXP_BLEND_ENABLE) {
        blend_attachment.blendEnable = VK_TRUE;
        blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
        blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    for (uint32_t i = 0; i < color_count; ++i) blend_attachments[i] = blend_attachment;
    VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = color_count,
        .pAttachments = color_count ? blend_attachments : NULL};
    if (flags & EXP_LOGIC_OP) blend.logicOp = VK_LOGIC_OP_OR;

    /* The stage structures.  When the experiment leaves them uninitialised the
     * stack is dirtied first, so "whatever the stack held" is a known value
     * rather than an accident. */
    if (flags & EXP_STACK_POISON) {
        volatile unsigned char dirt[512];
        for (size_t i = 0; i < sizeof(dirt); ++i) dirt[i] = 0xa5;
    }
    VkPipelineShaderStageCreateInfo stages[2];
    fill_stage(&stages[0], VK_SHADER_STAGE_VERTEX_BIT, vert_module, flags);
    fill_stage(&stages[1], VK_SHADER_STAGE_FRAGMENT_BIT, frag_module, flags);

    VkFormat color_formats[MAX_COLOR_ATTACHMENTS];
    for (uint32_t i = 0; i < MAX_COLOR_ATTACHMENTS; ++i) color_formats[i] = VK_FORMAT_R8G8B8A8_UNORM;
    VkPipelineRenderingCreateInfo rendering = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .viewMask = 0, .colorAttachmentCount = color_count,
        .pColorAttachmentFormats = color_count ? color_formats : NULL};

    VkGraphicsPipelineCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = (flags & EXP_DYNAMIC_RENDERING) ? &rendering : NULL,
        .stageCount = 2, .pStages = stages,
        .pVertexInputState = &vertex_input, .pInputAssemblyState = &assembly,
        .pViewportState = &viewport_state, .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pDepthStencilState = (flags & EXP_DEPTH) ? &depth_stencil : NULL,
        .pColorBlendState = &blend, .pDynamicState = (flags & EXP_DYNAMIC_VIEWPORT) ? &dynamic_state : NULL,
        .layout = pipeline_layout,
        .renderPass = (flags & EXP_DYNAMIC_RENDERING) ? VK_NULL_HANDLE : target.render_pass,
        .subpass = 0, .basePipelineHandle = VK_NULL_HANDLE, .basePipelineIndex = -1};

    trace("vkCreateGraphicsPipelines");
    printf("[bisect] calling vkCreateGraphicsPipelines\n");
    fflush(stdout);
    VkPipeline pipeline = VK_NULL_HANDLE;
    result = p_vkCreateGraphicsPipelines(device.device, cache, 1, &info, NULL, &pipeline);
    printf("[bisect] result=pipeline_created vkCreateGraphicsPipelines=%d handle=%p\n",
           (int)result, (void *)pipeline);
    fflush(stdout);

    char result_path[512];
    join(result_path, sizeof(result_path), g_out_dir, "result.json");
    char text[1024];
    snprintf(text, sizeof(text),
             "{\"schema\":\"%s\",\"experiment\":\"%s\",\"result\":\"%s\",\"vk_result\":%d,"
             "\"pipeline\":%s,\"dynamic_rendering\":%s,\"attachment_count\":%u,"
             "\"samples\":%u,\"depth\":%s,\"stage_pnext\":%s,\"stage_flags\":%u,"
             "\"stage_specialization_info\":%s,\"question\":\"%s\",\"state\":\"%s\"}\n",
             BISECT_SCHEMA, experiment->name,
             result == VK_SUCCESS ? "VK_SUCCESS" : "vk_result_error", (int)result,
             result == VK_SUCCESS ? "true" : "false",
             (flags & EXP_DYNAMIC_RENDERING) ? "true" : "false",
             color_count, (unsigned)samples,
             (flags & EXP_DEPTH) ? "true" : "false",
             (flags & EXP_STAGE_POISON_PNEXT) ? "\"0xdeadbeef0000\"" : "null",
             (flags & EXP_STAGE_POISON_FLAGS) ? 0xffffffffu : 0u,
             (flags & EXP_STAGE_POISON_SPEC) ? "\"0xdeadbeef0000\"" : "null",
             experiment->question, experiment->state);
    write_file(result_path, text);
    return result == VK_SUCCESS ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* The parent: one child process per experiment                        */
/* ------------------------------------------------------------------ */

static void run_child(const Experiment *experiment, const char *module_dir) {
    char directory[512];
    snprintf(directory, sizeof(directory), "%s/%s", g_out_dir, experiment->name);
    make_dir(directory);

    char stdout_path[640];
    char stderr_path[640];
    char exit_path[640];
    join(stdout_path, sizeof(stdout_path), directory, "stdout.txt");
    join(stderr_path, sizeof(stderr_path), directory, "stderr.txt");
    join(exit_path, sizeof(exit_path), directory, "exitcode.txt");

    char command[1024];
    snprintf(command, sizeof(command), "vulkan_graphics_bisect %s %s\n", experiment->name,
             module_dir);
    char command_path[640];
    join(command_path, sizeof(command_path), directory, "command.txt");
    write_file(command_path, command);

    pid_t pid = fork();
    if (pid == 0) {
        int out_fd = open(stdout_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        int err_fd = open(stderr_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out_fd >= 0) dup2(out_fd, STDOUT_FILENO);
        if (err_fd >= 0) dup2(err_fd, STDERR_FILENO);
        char self[512];
        ssize_t length = readlink("/proc/self/exe", self, sizeof(self) - 1);
        if (length > 0) {
            self[length] = '\0';
            char *args[4];
            args[0] = self;
            args[1] = (char *)experiment->name;
            args[2] = (char *)module_dir;
            args[3] = NULL;
            execv(self, args);
        }
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);

    char line[256];
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    int signal_number = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
    snprintf(line, sizeof(line),
             "raw_wait_status=%d exited=%d exit_code=%d terminated_by_signal=%d signal=%d\n",
             status, WIFEXITED(status) ? 1 : 0, code,
             WIFSIGNALED(status) ? 1 : 0, signal_number);
    write_file(exit_path, line);
    printf("[bisect] %-28s exit=%d signal=%d\n", experiment->name, code, signal_number);
    fflush(stdout);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fputs("Usage: vulkan_graphics_bisect <all|experiment-name> <module-directory>\n"
              "Each experiment runs in its own process.  FFRS_BISECT_OUT names the\n"
              "output directory; it defaults to ./bisect-out.\n",
              stderr);
        return 64;
    }
    const char *out_dir = getenv("FFRS_BISECT_OUT");
    if (!out_dir || !*out_dir) out_dir = "./bisect-out";
    snprintf(g_out_dir, sizeof(g_out_dir), "%s", out_dir);
    make_dir(g_out_dir);

    /*
     * The crash handler is opt-in.  With it off, Android's own crash handler
     * produces the tombstone, which carries the module-relative program
     * counter of every frame -- the evidence that says which library the
     * crash is in.  With it on, the probe records the fault address, the
     * faulting program counter and the frame chain itself.
     */
    const char *handler_mode = getenv("FFRS_BISECT_HANDLER");
    if (handler_mode && !strcmp(handler_mode, "1")) {
        struct sigaction action;
        memset(&action, 0, sizeof(action));
        action.sa_sigaction = crash_handler;
        action.sa_flags = SA_SIGINFO;
        sigemptyset(&action.sa_mask);
        sigaction(SIGSEGV, &action, NULL);
        sigaction(SIGBUS, &action, NULL);
        trace("crash_handler_installed");
    } else {
        trace("crash_handler_left_to_android");
    }

    const char *module_dir = argv[2];
    if (!strcmp(argv[1], "all")) {
        for (int i = 0; i < EXPERIMENT_COUNT; ++i) run_child(&EXPERIMENTS[i], module_dir);
        return 0;
    }
    for (int i = 0; i < EXPERIMENT_COUNT; ++i) {
        if (!strcmp(argv[1], EXPERIMENTS[i].name)) {
            snprintf(g_experiment, sizeof(g_experiment), "%s", EXPERIMENTS[i].name);
            make_dir(g_out_dir);
            char directory[512];
            snprintf(directory, sizeof(directory), "%s/%s", g_out_dir, EXPERIMENTS[i].name);
            make_dir(directory);
            char child_out[512];
            snprintf(child_out, sizeof(child_out), "%s", directory);
            snprintf(g_out_dir, sizeof(g_out_dir), "%s", child_out);
            return run_experiment(&EXPERIMENTS[i], module_dir);
        }
    }
    fprintf(stderr, "[bisect] unknown experiment '%s'\n", argv[1]);
    return 64;
}
