/* The NVN command buffer family of the FFRS RG477M native runtime.
 *
 * This file is generated. Do not edit it; edit the generator instead:
 *
 *     python3 scripts/runtime/make_nvn_command_buffer_family.py
 *
 * The generator reads the names the guest resolves from
 * evidence/nvn/main_nvn_ids.txt and the signatures from the vendored NVN
 * header in src/graphics/nvn/include/nvn/nvn.h, and writes this file. The
 * output depends on nothing else, so the same inputs always give the same
 * file.
 *
 * Every entry point records the arguments it was given and hands the
 * record to the recorder. The replay of the recorded commands does not
 * exist yet; nothing here pretends that a command was executed.
 */
#include "nvn_command_buffer.h"

#include "nvn_device.h"

#include <stddef.h>
#include <stdint.h>

/* The types of the NVN header that this family needs.
 *
 * The SDK objects are only ever named by a pointer, so they stay
 * incomplete here: the layout of an NVNobject is a property of the
 * Nintendo SDK, which this runtime does not have. The enumerations and the
 * handle wrappers are handed over by value, so they are complete. */
typedef struct nvn_cb_nvnblendstate NVNblendState;
typedef struct nvn_cb_nvnbufferrange NVNbufferRange;
typedef struct nvn_cb_nvnchannelmaskstate NVNchannelMaskState;
typedef struct nvn_cb_nvncolorstate NVNcolorState;
typedef struct nvn_cb_nvncommandbuffer NVNcommandBuffer;
typedef struct nvn_cb_nvncopyregion NVNcopyRegion;
typedef struct nvn_cb_nvndepthstencilstate NVNdepthStencilState;
typedef struct nvn_cb_nvndevice NVNdevice;
typedef struct nvn_cb_nvndrawtextureregion NVNdrawTextureRegion;
typedef struct nvn_cb_nvnevent NVNevent;
typedef struct nvn_cb_nvnmemorypool NVNmemoryPool;
typedef struct nvn_cb_nvnmultisamplestate NVNmultisampleState;
typedef struct nvn_cb_nvnpolygonstate NVNpolygonState;
typedef struct nvn_cb_nvnprogram NVNprogram;
typedef struct nvn_cb_nvnsamplerpool NVNsamplerPool;
typedef struct nvn_cb_nvnsync NVNsync;
typedef struct nvn_cb_nvntexture NVNtexture;
typedef struct nvn_cb_nvntexturepool NVNtexturePool;
typedef struct nvn_cb_nvntextureview NVNtextureView;
typedef struct nvn_cb_nvnvertexattribstate NVNvertexAttribState;
typedef struct nvn_cb_nvnvertexstreamstate NVNvertexStreamState;
typedef struct nvn_cb_nvnviewportswizzle NVNviewportSwizzle;

typedef uint8_t NVNboolean;

typedef uint64_t NVNbufferAddress;

typedef enum {
    NVN_COMMAND_BUFFER_MEMORY_EVENT_OUT_OF_COMMAND_MEMORY = 0x0,
    NVN_COMMAND_BUFFER_MEMORY_EVENT_OUT_OF_CONTROL_MEMORY = 0x1,

    NVN_COMMAND_BUFFER_MEMORY_EVENT_LARGE = 0x7FFFFFFF
} NVNcommandBufferMemoryEvent;

typedef uint64_t NVNcommandHandle;

typedef enum {
    NVN_CONDITIONAL_RENDER_MODE_RENDER_IF_EQUAL = 0x0,
    NVN_CONDITIONAL_RENDER_MODE_RENDER_IF_NOT_EQUAL = 0x1,

    NVN_CONDITIONAL_RENDER_MODE_LARGE = 0x7FFFFFFF
} NVNconditionalRenderMode;

typedef enum {
    NVN_COUNTER_TYPE_TIMESTAMP = 0x0,
    NVN_COUNTER_TYPE_SAMPLES_PASSED = 0x1,
    NVN_COUNTER_TYPE_INPUT_VERTICES = 0x2,
    NVN_COUNTER_TYPE_INPUT_PRIMITIVES = 0x3,
    NVN_COUNTER_TYPE_VERTEX_SHADER_INVOCATIONS = 0x4,
    NVN_COUNTER_TYPE_TESS_CONTROL_SHADER_INVOCATIONS = 0x5,
    NVN_COUNTER_TYPE_TESS_EVALUATION_SHADER_INVOCATIONS = 0x6,
    NVN_COUNTER_TYPE_GEOMETRY_SHADER_INVOCATIONS = 0x7,
    NVN_COUNTER_TYPE_FRAGMENT_SHADER_INVOCATIONS = 0x8,
    NVN_COUNTER_TYPE_TESS_EVALUATION_SHADER_PRIMITIVES = 0x9,
    NVN_COUNTER_TYPE_GEOMETRY_SHADER_PRIMITIVES = 0xA,
    NVN_COUNTER_TYPE_CLIPPER_INPUT_PRIMITIVES = 0xB,
    NVN_COUNTER_TYPE_CLIPPER_OUTPUT_PRIMITIVES = 0xC,
    NVN_COUNTER_TYPE_PRIMITIVES_GENERATED = 0xD,
    NVN_COUNTER_TYPE_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN = 0xE,
    NVN_COUNTER_TYPE_ZCULL_STATS = 0xF,
    NVN_COUNTER_TYPE_TIMESTAMP_TOP = 0x10,

    NVN_COUNTER_TYPE_LARGE = 0x7FFFFFFF
} NVNcounterType;

typedef enum {
    NVN_DRAW_PRIMITIVE_POINTS = 0x0,
    NVN_DRAW_PRIMITIVE_LINES = 0x1,
    NVN_DRAW_PRIMITIVE_LINE_LOOP = 0x2,
    NVN_DRAW_PRIMITIVE_LINE_STRIP = 0x3,
    NVN_DRAW_PRIMITIVE_TRIANGLES = 0x4,
    NVN_DRAW_PRIMITIVE_TRIANGLE_STRIP = 0x5,
    NVN_DRAW_PRIMITIVE_TRIANGLE_FAN = 0x6,
    NVN_DRAW_PRIMITIVE_QUADS = 0x7,
    NVN_DRAW_PRIMITIVE_QUAD_STRIP = 0x8,
    NVN_DRAW_PRIMITIVE_POLYGON = 0x9,
    NVN_DRAW_PRIMITIVE_LINES_ADJACENCY = 0xA,
    NVN_DRAW_PRIMITIVE_LINE_STRIP_ADJACENCY = 0xB,
    NVN_DRAW_PRIMITIVE_TRIANGLES_ADJACENCY = 0xC,
    NVN_DRAW_PRIMITIVE_TRIANGLE_STRIP_ADJACENCY = 0xD,
    NVN_DRAW_PRIMITIVE_PATCHES = 0xE,

    NVN_DRAW_PRIMITIVE_LARGE = 0x7FFFFFFF
} NVNdrawPrimitive;

typedef enum {
    NVN_EVENT_SIGNAL_LOCATION_TOP = 0x0,
    NVN_EVENT_SIGNAL_LOCATION_VERTEX_PIPE = 0x1,
    NVN_EVENT_SIGNAL_LOCATION_BOTTOM = 0x2,

    NVN_EVENT_SIGNAL_LOCATION_LARGE = 0x7FFFFFFF
} NVNeventSignalLocation;

typedef enum {
    NVN_EVENT_SIGNAL_MODE_WRITE = 0x0,
    NVN_EVENT_SIGNAL_MODE_ADD = 0x1,

    NVN_EVENT_SIGNAL_MODE_LARGE = 0x7FFFFFFF
} NVNeventSignalMode;

typedef enum {
    NVN_EVENT_WAIT_MODE_EQUAL = 0x0,
    NVN_EVENT_WAIT_MODE_GEQUAL_WRAP = 0x1,

    NVN_EVENT_WAIT_MODE_LARGE = 0x7FFFFFFF
} NVNeventWaitMode;

typedef enum {
    NVN_FACE_NONE = 0x0,
    NVN_FACE_FRONT = 0x1,
    NVN_FACE_BACK = 0x2,
    NVN_FACE_FRONT_AND_BACK = 0x3,

    NVN_FACE_LARGE = 0x7FFFFFFF
} NVNface;

typedef uint64_t NVNimageHandle;

typedef enum {
    NVN_INDEX_TYPE_UNSIGNED_BYTE = 0x0,
    NVN_INDEX_TYPE_UNSIGNED_SHORT = 0x1,
    NVN_INDEX_TYPE_UNSIGNED_INT = 0x2,

    NVN_INDEX_TYPE_LARGE = 0x7FFFFFFF
} NVNindexType;

typedef struct {
    uint64_t value;
} NVNseparateSamplerHandle;

typedef struct {
    uint64_t value;
} NVNseparateTextureHandle;

typedef enum {
    NVN_SHADER_STAGE_VERTEX = 0x0,
    NVN_SHADER_STAGE_FRAGMENT = 0x1,
    NVN_SHADER_STAGE_GEOMETRY = 0x2,
    NVN_SHADER_STAGE_TESS_CONTROL = 0x3,
    NVN_SHADER_STAGE_TESS_EVALUATION = 0x4,
    NVN_SHADER_STAGE_COMPUTE = 0x5,

    NVN_SHADER_STAGE_LARGE = 0x7FFFFFFF
} NVNshaderStage;

typedef enum {
    NVN_SYNC_CONDITION_ALL_GPU_COMMANDS_COMPLETE = 0x0,
    NVN_SYNC_CONDITION_GRAPHICS_WORLD_SPACE_COMPLETE = 0x1,

    NVN_SYNC_CONDITION_LARGE = 0x7FFFFFFF
} NVNsyncCondition;

typedef uint64_t NVNtextureHandle;

typedef enum {
    NVN_TILED_CACHE_ACTION_ENABLE = 0x1,
    NVN_TILED_CACHE_ACTION_DISABLE = 0x2,
    NVN_TILED_CACHE_ACTION_FLUSH = 0x3,
    NVN_TILED_CACHE_ACTION_FLUSH_NO_TILING = 0x4,
    NVN_TILED_CACHE_ACTION_ENABLE_RENDER_TARGET_BINNING = 0x5,
    NVN_TILED_CACHE_ACTION_DISABLE_RENDER_TARGET_BINNING = 0x6,

    NVN_TILED_CACHE_ACTION_LARGE = 0x7FFFFFFF
} NVNtiledCacheAction;

typedef void (*PFNNVNCOMMANDBUFFERMEMORYCALLBACKPROC)(NVNcommandBuffer*,
    NVNcommandBufferMemoryEvent, size_t, void*);


/* The entry points. */

void nvnCommandBufferAddCommandMemory(NVNcommandBuffer *commandBuffer, const NVNmemoryPool *arg1,
                                      ptrdiff_t arg2, size_t arg3) {
    uint64_t args[4];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    args[2] = (uint64_t)arg2;
    args[3] = (uint64_t)arg3;
    nvn_command_buffer_record(0, commandBuffer, args, 4);
    nvn_command_buffer_add_command_memory(commandBuffer, (uint64_t)arg3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferAddControlMemory(NVNcommandBuffer *commandBuffer, void *arg1, size_t arg2) {
    uint64_t args[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    args[2] = (uint64_t)arg2;
    nvn_command_buffer_record(1, commandBuffer, args, 3);
    nvn_command_buffer_add_control_memory(commandBuffer, (uint64_t)arg2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBarrier(NVNcommandBuffer *commandBuffer, int arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    nvn_command_buffer_record(2, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBeginRecording(NVNcommandBuffer *commandBuffer) {
    uint64_t args[1];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    nvn_command_buffer_record(3, commandBuffer, args, 1);
    nvn_command_buffer_begin_recording(commandBuffer);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBeginTransformFeedback(NVNcommandBuffer *commandBuffer,
                                            NVNbufferAddress arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)arg1;
    nvn_command_buffer_record(4, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindBlendState(NVNcommandBuffer *commandBuffer, const NVNblendState *arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    nvn_command_buffer_record(5, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindChannelMaskState(NVNcommandBuffer *commandBuffer,
                                          const NVNchannelMaskState *arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    nvn_command_buffer_record(6, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindColorState(NVNcommandBuffer *commandBuffer, const NVNcolorState *arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    nvn_command_buffer_record(7, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindCoverageModulationTable(NVNcommandBuffer *commandBuffer,
                                                 const float *arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    nvn_command_buffer_record(8, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindDepthStencilState(NVNcommandBuffer *commandBuffer,
                                           const NVNdepthStencilState *arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    nvn_command_buffer_record(9, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindImage(NVNcommandBuffer *commandBuffer, NVNshaderStage arg1, int arg2,
                               NVNimageHandle arg3) {
    uint64_t args[4];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)arg3;
    nvn_command_buffer_record(10, commandBuffer, args, 4);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindImages(NVNcommandBuffer *commandBuffer, NVNshaderStage arg1, int arg2,
                                int arg3, const NVNimageHandle *arg4) {
    uint64_t args[5];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)(uint32_t)arg3;
    args[4] = (uint64_t)(uintptr_t)arg4;
    nvn_command_buffer_record(11, commandBuffer, args, 5);
    NVN_VOID_RETURN();
}

/* The signature of nvnCommandBufferBindImagesDeferred is not in the header this runtime was built
 * against, so the arguments are taken as plain words and written down as they came. Nothing is
 * dereferenced, so a signature with fewer or more arguments cannot do any harm here. The log shows
 * the call shape the device really uses. */
void nvnCommandBufferBindImagesDeferred(NVNcommandBuffer *commandBuffer, uint64_t arg1,
                                        uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5,
                                        uint64_t arg6, uint64_t arg7) {
    uint64_t args[8];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = arg1;
    args[2] = arg2;
    args[3] = arg3;
    args[4] = arg4;
    args[5] = arg5;
    args[6] = arg6;
    args[7] = arg7;
    nvn_command_buffer_record(12, commandBuffer, args, 8);
    nvn_command_buffer_log_raw("nvnCommandBufferBindImagesDeferred",
                                commandBuffer, args, 8);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindMultisampleState(NVNcommandBuffer *commandBuffer,
                                          const NVNmultisampleState *arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    nvn_command_buffer_record(13, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindPolygonState(NVNcommandBuffer *commandBuffer,
                                      const NVNpolygonState *arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    nvn_command_buffer_record(14, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindProgram(NVNcommandBuffer *commandBuffer, const NVNprogram *arg1,
                                 int arg2) {
    uint64_t args[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    nvn_command_buffer_record(15, commandBuffer, args, 3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindSeparateSampler(NVNcommandBuffer *commandBuffer, NVNshaderStage arg1,
                                         int arg2, NVNseparateSamplerHandle arg3) {
    uint64_t args[4];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)arg3.value;
    nvn_command_buffer_record(16, commandBuffer, args, 4);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindSeparateSamplers(NVNcommandBuffer *commandBuffer, NVNshaderStage arg1,
                                          int arg2, int arg3,
                                          const NVNseparateSamplerHandle *arg4) {
    uint64_t args[5];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)(uint32_t)arg3;
    args[4] = (uint64_t)(uintptr_t)arg4;
    nvn_command_buffer_record(17, commandBuffer, args, 5);
    NVN_VOID_RETURN();
}

/* The signature of nvnCommandBufferBindSeparateSamplersDeferred is not in the header this runtime
 * was built against, so the arguments are taken as plain words and written down as they came.
 * Nothing is dereferenced, so a signature with fewer or more arguments cannot do any harm here. The
 * log shows the call shape the device really uses. */
void nvnCommandBufferBindSeparateSamplersDeferred(NVNcommandBuffer *commandBuffer, uint64_t arg1,
                                                  uint64_t arg2, uint64_t arg3, uint64_t arg4,
                                                  uint64_t arg5, uint64_t arg6, uint64_t arg7) {
    uint64_t args[8];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = arg1;
    args[2] = arg2;
    args[3] = arg3;
    args[4] = arg4;
    args[5] = arg5;
    args[6] = arg6;
    args[7] = arg7;
    nvn_command_buffer_record(18, commandBuffer, args, 8);
    nvn_command_buffer_log_raw("nvnCommandBufferBindSeparateSamplersDeferred",
                                commandBuffer, args, 8);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindSeparateTexture(NVNcommandBuffer *commandBuffer, NVNshaderStage arg1,
                                         int arg2, NVNseparateTextureHandle arg3) {
    uint64_t args[4];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)arg3.value;
    nvn_command_buffer_record(19, commandBuffer, args, 4);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindSeparateTextures(NVNcommandBuffer *commandBuffer, NVNshaderStage arg1,
                                          int arg2, int arg3,
                                          const NVNseparateTextureHandle *arg4) {
    uint64_t args[5];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)(uint32_t)arg3;
    args[4] = (uint64_t)(uintptr_t)arg4;
    nvn_command_buffer_record(20, commandBuffer, args, 5);
    NVN_VOID_RETURN();
}

/* The signature of nvnCommandBufferBindSeparateTexturesDeferred is not in the header this runtime
 * was built against, so the arguments are taken as plain words and written down as they came.
 * Nothing is dereferenced, so a signature with fewer or more arguments cannot do any harm here. The
 * log shows the call shape the device really uses. */
void nvnCommandBufferBindSeparateTexturesDeferred(NVNcommandBuffer *commandBuffer, uint64_t arg1,
                                                  uint64_t arg2, uint64_t arg3, uint64_t arg4,
                                                  uint64_t arg5, uint64_t arg6, uint64_t arg7) {
    uint64_t args[8];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = arg1;
    args[2] = arg2;
    args[3] = arg3;
    args[4] = arg4;
    args[5] = arg5;
    args[6] = arg6;
    args[7] = arg7;
    nvn_command_buffer_record(21, commandBuffer, args, 8);
    nvn_command_buffer_log_raw("nvnCommandBufferBindSeparateTexturesDeferred",
                                commandBuffer, args, 8);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindStorageBuffer(NVNcommandBuffer *commandBuffer, NVNshaderStage arg1,
                                       int arg2, NVNbufferAddress arg3, size_t arg4) {
    uint64_t args[5];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)arg3;
    args[4] = (uint64_t)arg4;
    nvn_command_buffer_record(22, commandBuffer, args, 5);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindStorageBuffers(NVNcommandBuffer *commandBuffer, NVNshaderStage arg1,
                                        int arg2, int arg3, const NVNbufferRange *arg4) {
    uint64_t args[5];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)(uint32_t)arg3;
    args[4] = (uint64_t)(uintptr_t)arg4;
    nvn_command_buffer_record(23, commandBuffer, args, 5);
    NVN_VOID_RETURN();
}

/* The signature of nvnCommandBufferBindStorageBuffersDeferred is not in the header this runtime was
 * built against, so the arguments are taken as plain words and written down as they came. Nothing
 * is dereferenced, so a signature with fewer or more arguments cannot do any harm here. The log
 * shows the call shape the device really uses. */
void nvnCommandBufferBindStorageBuffersDeferred(NVNcommandBuffer *commandBuffer, uint64_t arg1,
                                                uint64_t arg2, uint64_t arg3, uint64_t arg4,
                                                uint64_t arg5, uint64_t arg6, uint64_t arg7) {
    uint64_t args[8];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = arg1;
    args[2] = arg2;
    args[3] = arg3;
    args[4] = arg4;
    args[5] = arg5;
    args[6] = arg6;
    args[7] = arg7;
    nvn_command_buffer_record(24, commandBuffer, args, 8);
    nvn_command_buffer_log_raw("nvnCommandBufferBindStorageBuffersDeferred",
                                commandBuffer, args, 8);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindTexture(NVNcommandBuffer *commandBuffer, NVNshaderStage arg1, int arg2,
                                 NVNtextureHandle arg3) {
    uint64_t args[4];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)arg3;
    nvn_command_buffer_record(25, commandBuffer, args, 4);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindTextures(NVNcommandBuffer *commandBuffer, NVNshaderStage arg1, int arg2,
                                  int arg3, const NVNtextureHandle *arg4) {
    uint64_t args[5];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)(uint32_t)arg3;
    args[4] = (uint64_t)(uintptr_t)arg4;
    nvn_command_buffer_record(26, commandBuffer, args, 5);
    NVN_VOID_RETURN();
}

/* The signature of nvnCommandBufferBindTexturesDeferred is not in the header this runtime was built
 * against, so the arguments are taken as plain words and written down as they came. Nothing is
 * dereferenced, so a signature with fewer or more arguments cannot do any harm here. The log shows
 * the call shape the device really uses. */
void nvnCommandBufferBindTexturesDeferred(NVNcommandBuffer *commandBuffer, uint64_t arg1,
                                          uint64_t arg2, uint64_t arg3, uint64_t arg4,
                                          uint64_t arg5, uint64_t arg6, uint64_t arg7) {
    uint64_t args[8];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = arg1;
    args[2] = arg2;
    args[3] = arg3;
    args[4] = arg4;
    args[5] = arg5;
    args[6] = arg6;
    args[7] = arg7;
    nvn_command_buffer_record(27, commandBuffer, args, 8);
    nvn_command_buffer_log_raw("nvnCommandBufferBindTexturesDeferred",
                                commandBuffer, args, 8);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindTransformFeedbackBuffer(NVNcommandBuffer *commandBuffer, int arg1,
                                                 NVNbufferAddress arg2, size_t arg3) {
    uint64_t args[4];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)arg2;
    args[3] = (uint64_t)arg3;
    nvn_command_buffer_record(28, commandBuffer, args, 4);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindTransformFeedbackBuffers(NVNcommandBuffer *commandBuffer, int arg1,
                                                  int arg2, const NVNbufferRange *arg3) {
    uint64_t args[4];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)(uintptr_t)arg3;
    nvn_command_buffer_record(29, commandBuffer, args, 4);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindUniformBuffer(NVNcommandBuffer *commandBuffer, NVNshaderStage arg1,
                                       int arg2, NVNbufferAddress arg3, size_t arg4) {
    uint64_t args[5];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)arg3;
    args[4] = (uint64_t)arg4;
    nvn_command_buffer_record(30, commandBuffer, args, 5);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindUniformBuffers(NVNcommandBuffer *commandBuffer, NVNshaderStage arg1,
                                        int arg2, int arg3, const NVNbufferRange *arg4) {
    uint64_t args[5];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)(uint32_t)arg3;
    args[4] = (uint64_t)(uintptr_t)arg4;
    nvn_command_buffer_record(31, commandBuffer, args, 5);
    NVN_VOID_RETURN();
}

/* The signature of nvnCommandBufferBindUniformBuffersDeferred is not in the header this runtime was
 * built against, so the arguments are taken as plain words and written down as they came. Nothing
 * is dereferenced, so a signature with fewer or more arguments cannot do any harm here. The log
 * shows the call shape the device really uses. */
void nvnCommandBufferBindUniformBuffersDeferred(NVNcommandBuffer *commandBuffer, uint64_t arg1,
                                                uint64_t arg2, uint64_t arg3, uint64_t arg4,
                                                uint64_t arg5, uint64_t arg6, uint64_t arg7) {
    uint64_t args[8];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = arg1;
    args[2] = arg2;
    args[3] = arg3;
    args[4] = arg4;
    args[5] = arg5;
    args[6] = arg6;
    args[7] = arg7;
    nvn_command_buffer_record(32, commandBuffer, args, 8);
    nvn_command_buffer_log_raw("nvnCommandBufferBindUniformBuffersDeferred",
                                commandBuffer, args, 8);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindVertexAttribState(NVNcommandBuffer *commandBuffer, int arg1,
                                           const NVNvertexAttribState *arg2) {
    uint64_t args[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uintptr_t)arg2;
    nvn_command_buffer_record(33, commandBuffer, args, 3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindVertexBuffer(NVNcommandBuffer *commandBuffer, int arg1,
                                      NVNbufferAddress arg2, size_t arg3) {
    uint64_t args[4];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)arg2;
    args[3] = (uint64_t)arg3;
    nvn_command_buffer_record(34, commandBuffer, args, 4);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindVertexBuffers(NVNcommandBuffer *commandBuffer, int arg1, int arg2,
                                       const NVNbufferRange *arg3) {
    uint64_t args[4];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)(uintptr_t)arg3;
    nvn_command_buffer_record(35, commandBuffer, args, 4);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindVertexStreamState(NVNcommandBuffer *commandBuffer, int arg1,
                                           const NVNvertexStreamState *arg2) {
    uint64_t args[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uintptr_t)arg2;
    nvn_command_buffer_record(36, commandBuffer, args, 3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferCallCommands(NVNcommandBuffer *commandBuffer, int arg1,
                                  const NVNcommandHandle *arg2) {
    uint64_t args[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uintptr_t)arg2;
    nvn_command_buffer_record(37, commandBuffer, args, 3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferClearBuffer(NVNcommandBuffer *commandBuffer, NVNbufferAddress arg1,
                                 size_t arg2, uint32_t arg3) {
    uint64_t args[4];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)arg1;
    args[2] = (uint64_t)arg2;
    args[3] = (uint64_t)(uint32_t)arg3;
    nvn_command_buffer_record(38, commandBuffer, args, 4);
    NVN_VOID_RETURN();
}

void nvnCommandBufferClearColor(NVNcommandBuffer *commandBuffer, int arg1, const float *arg2,
                                int arg3) {
    uint64_t args[4];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uintptr_t)arg2;
    args[3] = (uint64_t)(uint32_t)arg3;
    nvn_command_buffer_record(39, commandBuffer, args, 4);
    NVN_VOID_RETURN();
}

void nvnCommandBufferClearColori(NVNcommandBuffer *commandBuffer, int arg1, const int *arg2,
                                 int arg3) {
    uint64_t args[4];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uintptr_t)arg2;
    args[3] = (uint64_t)(uint32_t)arg3;
    nvn_command_buffer_record(40, commandBuffer, args, 4);
    NVN_VOID_RETURN();
}

void nvnCommandBufferClearColorui(NVNcommandBuffer *commandBuffer, int arg1, const uint32_t *arg2,
                                  int arg3) {
    uint64_t args[4];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uintptr_t)arg2;
    args[3] = (uint64_t)(uint32_t)arg3;
    nvn_command_buffer_record(41, commandBuffer, args, 4);
    NVN_VOID_RETURN();
}

void nvnCommandBufferClearDepthStencil(NVNcommandBuffer *commandBuffer, float arg1, NVNboolean arg2,
                                       int arg3, int arg4) {
    uint64_t args[5];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = nvn_command_buffer_float_bits(arg1);
    args[2] = (uint64_t)(uint8_t)arg2;
    args[3] = (uint64_t)(uint32_t)arg3;
    args[4] = (uint64_t)(uint32_t)arg4;
    nvn_command_buffer_record(42, commandBuffer, args, 5);
    NVN_VOID_RETURN();
}

void nvnCommandBufferClearTexture(NVNcommandBuffer *commandBuffer, const NVNtexture *arg1,
                                  const NVNtextureView *arg2, const NVNcopyRegion *arg3,
                                  const float *arg4, int arg5) {
    uint64_t args[6];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    args[2] = (uint64_t)(uintptr_t)arg2;
    args[3] = (uint64_t)(uintptr_t)arg3;
    args[4] = (uint64_t)(uintptr_t)arg4;
    args[5] = (uint64_t)(uint32_t)arg5;
    nvn_command_buffer_record(43, commandBuffer, args, 6);
    NVN_VOID_RETURN();
}

void nvnCommandBufferClearTexturei(NVNcommandBuffer *commandBuffer, const NVNtexture *arg1,
                                   const NVNtextureView *arg2, const NVNcopyRegion *arg3,
                                   const int *arg4, int arg5) {
    uint64_t args[6];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    args[2] = (uint64_t)(uintptr_t)arg2;
    args[3] = (uint64_t)(uintptr_t)arg3;
    args[4] = (uint64_t)(uintptr_t)arg4;
    args[5] = (uint64_t)(uint32_t)arg5;
    nvn_command_buffer_record(44, commandBuffer, args, 6);
    NVN_VOID_RETURN();
}

void nvnCommandBufferClearTextureui(NVNcommandBuffer *commandBuffer, const NVNtexture *arg1,
                                    const NVNtextureView *arg2, const NVNcopyRegion *arg3,
                                    const uint32_t *arg4, int arg5) {
    uint64_t args[6];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    args[2] = (uint64_t)(uintptr_t)arg2;
    args[3] = (uint64_t)(uintptr_t)arg3;
    args[4] = (uint64_t)(uintptr_t)arg4;
    args[5] = (uint64_t)(uint32_t)arg5;
    nvn_command_buffer_record(45, commandBuffer, args, 6);
    NVN_VOID_RETURN();
}

void nvnCommandBufferCopyBufferToBuffer(NVNcommandBuffer *commandBuffer, NVNbufferAddress arg1,
                                        NVNbufferAddress arg2, size_t arg3, int arg4) {
    uint64_t args[5];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)arg1;
    args[2] = (uint64_t)arg2;
    args[3] = (uint64_t)arg3;
    args[4] = (uint64_t)(uint32_t)arg4;
    nvn_command_buffer_record(46, commandBuffer, args, 5);
    NVN_VOID_RETURN();
}

void nvnCommandBufferCopyBufferToTexture(NVNcommandBuffer *commandBuffer, NVNbufferAddress arg1,
                                         const NVNtexture *arg2, const NVNtextureView *arg3,
                                         const NVNcopyRegion *arg4, int arg5) {
    uint64_t args[6];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)arg1;
    args[2] = (uint64_t)(uintptr_t)arg2;
    args[3] = (uint64_t)(uintptr_t)arg3;
    args[4] = (uint64_t)(uintptr_t)arg4;
    args[5] = (uint64_t)(uint32_t)arg5;
    nvn_command_buffer_record(47, commandBuffer, args, 6);
    NVN_VOID_RETURN();
}

void nvnCommandBufferCopyCommands(NVNcommandBuffer *commandBuffer, int arg1,
                                  const NVNcommandHandle *arg2) {
    uint64_t args[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uintptr_t)arg2;
    nvn_command_buffer_record(48, commandBuffer, args, 3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferCopyTextureToBuffer(NVNcommandBuffer *commandBuffer, const NVNtexture *arg1,
                                         const NVNtextureView *arg2, const NVNcopyRegion *arg3,
                                         NVNbufferAddress arg4, int arg5) {
    uint64_t args[6];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    args[2] = (uint64_t)(uintptr_t)arg2;
    args[3] = (uint64_t)(uintptr_t)arg3;
    args[4] = (uint64_t)arg4;
    args[5] = (uint64_t)(uint32_t)arg5;
    nvn_command_buffer_record(49, commandBuffer, args, 6);
    NVN_VOID_RETURN();
}

void nvnCommandBufferCopyTextureToTexture(NVNcommandBuffer *commandBuffer, const NVNtexture *arg1,
                                          const NVNtextureView *arg2, const NVNcopyRegion *arg3,
                                          const NVNtexture *arg4, const NVNtextureView *arg5,
                                          const NVNcopyRegion *arg6, int arg7) {
    uint64_t args[8];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    args[2] = (uint64_t)(uintptr_t)arg2;
    args[3] = (uint64_t)(uintptr_t)arg3;
    args[4] = (uint64_t)(uintptr_t)arg4;
    args[5] = (uint64_t)(uintptr_t)arg5;
    args[6] = (uint64_t)(uintptr_t)arg6;
    args[7] = (uint64_t)(uint32_t)arg7;
    nvn_command_buffer_record(50, commandBuffer, args, 8);
    NVN_VOID_RETURN();
}

void nvnCommandBufferDiscardColor(NVNcommandBuffer *commandBuffer, int arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    nvn_command_buffer_record(51, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferDiscardDepthStencil(NVNcommandBuffer *commandBuffer) {
    uint64_t args[1];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    nvn_command_buffer_record(52, commandBuffer, args, 1);
    NVN_VOID_RETURN();
}

void nvnCommandBufferDispatchCompute(NVNcommandBuffer *commandBuffer, int arg1, int arg2,
                                     int arg3) {
    uint64_t args[4];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)(uint32_t)arg3;
    nvn_command_buffer_record(53, commandBuffer, args, 4);
    NVN_VOID_RETURN();
}

void nvnCommandBufferDispatchComputeIndirect(NVNcommandBuffer *commandBuffer,
                                             NVNbufferAddress arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)arg1;
    nvn_command_buffer_record(54, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferDownsample(NVNcommandBuffer *commandBuffer, const NVNtexture *arg1,
                                const NVNtexture *arg2) {
    uint64_t args[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    args[2] = (uint64_t)(uintptr_t)arg2;
    nvn_command_buffer_record(55, commandBuffer, args, 3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferDownsampleTextureView(NVNcommandBuffer *commandBuffer, const NVNtexture *arg1,
                                           const NVNtextureView *arg2, const NVNtexture *arg3,
                                           const NVNtextureView *arg4) {
    uint64_t args[5];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    args[2] = (uint64_t)(uintptr_t)arg2;
    args[3] = (uint64_t)(uintptr_t)arg3;
    args[4] = (uint64_t)(uintptr_t)arg4;
    nvn_command_buffer_record(56, commandBuffer, args, 5);
    NVN_VOID_RETURN();
}

void nvnCommandBufferDrawArrays(NVNcommandBuffer *commandBuffer, NVNdrawPrimitive arg1, int arg2,
                                int arg3) {
    uint64_t args[4];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)(uint32_t)arg3;
    nvn_command_buffer_record(57, commandBuffer, args, 4);
    NVN_VOID_RETURN();
}

void nvnCommandBufferDrawArraysIndirect(NVNcommandBuffer *commandBuffer, NVNdrawPrimitive arg1,
                                        NVNbufferAddress arg2) {
    uint64_t args[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)arg2;
    nvn_command_buffer_record(58, commandBuffer, args, 3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferDrawArraysInstanced(NVNcommandBuffer *commandBuffer, NVNdrawPrimitive arg1,
                                         int arg2, int arg3, int arg4, int arg5) {
    uint64_t args[6];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)(uint32_t)arg3;
    args[4] = (uint64_t)(uint32_t)arg4;
    args[5] = (uint64_t)(uint32_t)arg5;
    nvn_command_buffer_record(59, commandBuffer, args, 6);
    NVN_VOID_RETURN();
}

void nvnCommandBufferDrawElements(NVNcommandBuffer *commandBuffer, NVNdrawPrimitive arg1,
                                  NVNindexType arg2, int arg3, NVNbufferAddress arg4) {
    uint64_t args[5];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)(uint32_t)arg3;
    args[4] = (uint64_t)arg4;
    nvn_command_buffer_record(60, commandBuffer, args, 5);
    NVN_VOID_RETURN();
}

void nvnCommandBufferDrawElementsBaseVertex(NVNcommandBuffer *commandBuffer, NVNdrawPrimitive arg1,
                                            NVNindexType arg2, int arg3, NVNbufferAddress arg4,
                                            int arg5) {
    uint64_t args[6];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)(uint32_t)arg3;
    args[4] = (uint64_t)arg4;
    args[5] = (uint64_t)(uint32_t)arg5;
    nvn_command_buffer_record(61, commandBuffer, args, 6);
    NVN_VOID_RETURN();
}

void nvnCommandBufferDrawElementsIndirect(NVNcommandBuffer *commandBuffer, NVNdrawPrimitive arg1,
                                          NVNindexType arg2, NVNbufferAddress arg3,
                                          NVNbufferAddress arg4) {
    uint64_t args[5];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)arg3;
    args[4] = (uint64_t)arg4;
    nvn_command_buffer_record(62, commandBuffer, args, 5);
    NVN_VOID_RETURN();
}

void nvnCommandBufferDrawElementsInstanced(NVNcommandBuffer *commandBuffer, NVNdrawPrimitive arg1,
                                           NVNindexType arg2, int arg3, NVNbufferAddress arg4,
                                           int arg5, int arg6, int arg7) {
    uint64_t args[8];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)(uint32_t)arg3;
    args[4] = (uint64_t)arg4;
    args[5] = (uint64_t)(uint32_t)arg5;
    args[6] = (uint64_t)(uint32_t)arg6;
    args[7] = (uint64_t)(uint32_t)arg7;
    nvn_command_buffer_record(63, commandBuffer, args, 8);
    NVN_VOID_RETURN();
}

void nvnCommandBufferDrawTexture(NVNcommandBuffer *commandBuffer, NVNtextureHandle arg1,
                                 const NVNdrawTextureRegion *arg2,
                                 const NVNdrawTextureRegion *arg3) {
    uint64_t args[4];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)arg1;
    args[2] = (uint64_t)(uintptr_t)arg2;
    args[3] = (uint64_t)(uintptr_t)arg3;
    nvn_command_buffer_record(64, commandBuffer, args, 4);
    NVN_VOID_RETURN();
}

void nvnCommandBufferDrawTransformFeedback(NVNcommandBuffer *commandBuffer, NVNdrawPrimitive arg1,
                                           NVNbufferAddress arg2) {
    uint64_t args[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)arg2;
    nvn_command_buffer_record(65, commandBuffer, args, 3);
    NVN_VOID_RETURN();
}

NVNcommandHandle nvnCommandBufferEndRecording(NVNcommandBuffer *commandBuffer) {
    uint64_t args[1];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    nvn_command_buffer_record(66, commandBuffer, args, 1);
    return (NVNcommandHandle)nvn_command_buffer_end_recording(commandBuffer);
}

void nvnCommandBufferEndTransformFeedback(NVNcommandBuffer *commandBuffer, NVNbufferAddress arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)arg1;
    nvn_command_buffer_record(67, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferFenceSync(NVNcommandBuffer *commandBuffer, NVNsync *arg1,
                               NVNsyncCondition arg2, int arg3) {
    uint64_t args[4];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)(uint32_t)arg3;
    nvn_command_buffer_record(68, commandBuffer, args, 4);
    NVN_VOID_RETURN();
}

void nvnCommandBufferFinalize(NVNcommandBuffer *commandBuffer) {
    uint64_t args[1];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    nvn_command_buffer_record(69, commandBuffer, args, 1);
    nvn_command_buffer_finalize(commandBuffer);
    NVN_VOID_RETURN();
}

size_t nvnCommandBufferGetCommandMemoryFree(const NVNcommandBuffer *commandBuffer) {
    uint64_t args[1];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    nvn_command_buffer_record(70, commandBuffer, args, 1);
    return (size_t)nvn_command_buffer_get_command_memory_free(commandBuffer);
}

size_t nvnCommandBufferGetCommandMemorySize(const NVNcommandBuffer *commandBuffer) {
    uint64_t args[1];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    nvn_command_buffer_record(71, commandBuffer, args, 1);
    return (size_t)nvn_command_buffer_get_command_memory_size(commandBuffer);
}

size_t nvnCommandBufferGetCommandMemoryUsed(const NVNcommandBuffer *commandBuffer) {
    uint64_t args[1];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    nvn_command_buffer_record(72, commandBuffer, args, 1);
    nvn_command_buffer_report_used_is_unknown(commandBuffer, "command memory");
    return 0;
}

size_t nvnCommandBufferGetControlMemoryFree(const NVNcommandBuffer *commandBuffer) {
    uint64_t args[1];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    nvn_command_buffer_record(73, commandBuffer, args, 1);
    return (size_t)nvn_command_buffer_get_control_memory_free(commandBuffer);
}

size_t nvnCommandBufferGetControlMemorySize(const NVNcommandBuffer *commandBuffer) {
    uint64_t args[1];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    nvn_command_buffer_record(74, commandBuffer, args, 1);
    return (size_t)nvn_command_buffer_get_control_memory_size(commandBuffer);
}

size_t nvnCommandBufferGetControlMemoryUsed(const NVNcommandBuffer *commandBuffer) {
    uint64_t args[1];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    nvn_command_buffer_record(75, commandBuffer, args, 1);
    nvn_command_buffer_report_used_is_unknown(commandBuffer, "control memory");
    return 0;
}

ptrdiff_t nvnCommandBufferGetCopyImageStride(const NVNcommandBuffer *commandBuffer) {
    uint64_t args[1];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    nvn_command_buffer_record(76, commandBuffer, args, 1);
    return (ptrdiff_t)nvn_command_buffer_get_copy_image_stride(commandBuffer);
}

ptrdiff_t nvnCommandBufferGetCopyRowStride(const NVNcommandBuffer *commandBuffer) {
    uint64_t args[1];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    nvn_command_buffer_record(77, commandBuffer, args, 1);
    return (ptrdiff_t)nvn_command_buffer_get_copy_row_stride(commandBuffer);
}

PFNNVNCOMMANDBUFFERMEMORYCALLBACKPROC
nvnCommandBufferGetMemoryCallback(const NVNcommandBuffer *commandBuffer) {
    uint64_t args[1];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    nvn_command_buffer_record(78, commandBuffer, args, 1);
    return (PFNNVNCOMMANDBUFFERMEMORYCALLBACKPROC)
        (uintptr_t)nvn_command_buffer_get_memory_callback(commandBuffer);
}

void nvnCommandBufferGetMemoryCallbackData(const NVNcommandBuffer *commandBuffer) {
    uint64_t args[1];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    nvn_command_buffer_record(79, commandBuffer, args, 1);
    /* The header this runtime was built against declares this entry point as
     * returning nothing, but the value it names is the callback data the guest
     * set, and the guest reads it from the result register. The value goes
     * there directly, because a C function that returns nothing cannot hand
     * it over otherwise. */
    nvn_command_buffer_set_result(
        nvn_command_buffer_get_memory_callback_data(commandBuffer));
}

NVNboolean nvnCommandBufferInitialize(NVNcommandBuffer *commandBuffer, NVNdevice *arg1) {
    uint64_t args[2];
    int initialized = nvn_command_buffer_initialize(commandBuffer);
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    nvn_command_buffer_record(80, commandBuffer, args, 2);
    return (NVNboolean)(initialized ? 1 : 0);
}

/* The signature of nvnCommandBufferInsertCheckpointDynamic is not in the header this runtime was
 * built against, so the arguments are taken as plain words and written down as they came. Nothing
 * is dereferenced, so a signature with fewer or more arguments cannot do any harm here. The log
 * shows the call shape the device really uses. */
void nvnCommandBufferInsertCheckpointDynamic(NVNcommandBuffer *commandBuffer, uint64_t arg1,
                                             uint64_t arg2, uint64_t arg3, uint64_t arg4,
                                             uint64_t arg5, uint64_t arg6, uint64_t arg7) {
    uint64_t args[8];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = arg1;
    args[2] = arg2;
    args[3] = arg3;
    args[4] = arg4;
    args[5] = arg5;
    args[6] = arg6;
    args[7] = arg7;
    nvn_command_buffer_record(81, commandBuffer, args, 8);
    nvn_command_buffer_log_raw("nvnCommandBufferInsertCheckpointDynamic",
                                commandBuffer, args, 8);
    NVN_VOID_RETURN();
}

/* The signature of nvnCommandBufferInsertCheckpointStatic is not in the header this runtime was
 * built against, so the arguments are taken as plain words and written down as they came. Nothing
 * is dereferenced, so a signature with fewer or more arguments cannot do any harm here. The log
 * shows the call shape the device really uses. */
void nvnCommandBufferInsertCheckpointStatic(NVNcommandBuffer *commandBuffer, uint64_t arg1,
                                            uint64_t arg2, uint64_t arg3, uint64_t arg4,
                                            uint64_t arg5, uint64_t arg6, uint64_t arg7) {
    uint64_t args[8];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = arg1;
    args[2] = arg2;
    args[3] = arg3;
    args[4] = arg4;
    args[5] = arg5;
    args[6] = arg6;
    args[7] = arg7;
    nvn_command_buffer_record(82, commandBuffer, args, 8);
    nvn_command_buffer_log_raw("nvnCommandBufferInsertCheckpointStatic",
                                commandBuffer, args, 8);
    NVN_VOID_RETURN();
}

void nvnCommandBufferInsertDebugMarker(NVNcommandBuffer *commandBuffer, const char *arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    nvn_command_buffer_record(83, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferInsertDebugMarkerDynamic(NVNcommandBuffer *commandBuffer, uint32_t arg1,
                                              const char *arg2) {
    uint64_t args[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uintptr_t)arg2;
    nvn_command_buffer_record(84, commandBuffer, args, 3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferInsertDebugMarkerStatic(NVNcommandBuffer *commandBuffer, uint32_t arg1,
                                             const char *arg2) {
    uint64_t args[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uintptr_t)arg2;
    nvn_command_buffer_record(85, commandBuffer, args, 3);
    NVN_VOID_RETURN();
}

NVNboolean nvnCommandBufferIsRecording(const NVNcommandBuffer *commandBuffer) {
    uint64_t args[1];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    nvn_command_buffer_record(86, commandBuffer, args, 1);
    return (NVNboolean)(nvn_command_buffer_is_recording(commandBuffer) ? 1 : 0);
}

void nvnCommandBufferMultiDrawArraysIndirectCount(NVNcommandBuffer *commandBuffer,
                                                  NVNdrawPrimitive arg1, NVNbufferAddress arg2,
                                                  NVNbufferAddress arg3, int arg4, ptrdiff_t arg5) {
    uint64_t args[6];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)arg2;
    args[3] = (uint64_t)arg3;
    args[4] = (uint64_t)(uint32_t)arg4;
    args[5] = (uint64_t)arg5;
    nvn_command_buffer_record(87, commandBuffer, args, 6);
    NVN_VOID_RETURN();
}

void nvnCommandBufferMultiDrawElementsIndirectCount(NVNcommandBuffer *commandBuffer,
                                                    NVNdrawPrimitive arg1, NVNindexType arg2,
                                                    NVNbufferAddress arg3, NVNbufferAddress arg4,
                                                    NVNbufferAddress arg5, int arg6,
                                                    ptrdiff_t arg7) {
    uint64_t args[8];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)arg3;
    args[4] = (uint64_t)arg4;
    args[5] = (uint64_t)arg5;
    args[6] = (uint64_t)(uint32_t)arg6;
    args[7] = (uint64_t)arg7;
    nvn_command_buffer_record(88, commandBuffer, args, 8);
    NVN_VOID_RETURN();
}

/* The signature of nvnCommandBufferOverrideSubtileSize is not in the header this runtime was built
 * against, so the arguments are taken as plain words and written down as they came. Nothing is
 * dereferenced, so a signature with fewer or more arguments cannot do any harm here. The log shows
 * the call shape the device really uses. */
void nvnCommandBufferOverrideSubtileSize(NVNcommandBuffer *commandBuffer, uint64_t arg1,
                                         uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5,
                                         uint64_t arg6, uint64_t arg7) {
    uint64_t args[8];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = arg1;
    args[2] = arg2;
    args[3] = arg3;
    args[4] = arg4;
    args[5] = arg5;
    args[6] = arg6;
    args[7] = arg7;
    nvn_command_buffer_record(89, commandBuffer, args, 8);
    nvn_command_buffer_log_raw("nvnCommandBufferOverrideSubtileSize",
                                commandBuffer, args, 8);
    NVN_VOID_RETURN();
}

void nvnCommandBufferPauseTransformFeedback(NVNcommandBuffer *commandBuffer,
                                            NVNbufferAddress arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)arg1;
    nvn_command_buffer_record(90, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferPopDebugGroup(NVNcommandBuffer *commandBuffer) {
    uint64_t args[1];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    nvn_command_buffer_record(91, commandBuffer, args, 1);
    NVN_VOID_RETURN();
}

void nvnCommandBufferPopDebugGroupId(NVNcommandBuffer *commandBuffer, uint32_t arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    nvn_command_buffer_record(92, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferPushDebugGroup(NVNcommandBuffer *commandBuffer, uint32_t arg1,
                                    const char *arg2) {
    uint64_t args[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uintptr_t)arg2;
    nvn_command_buffer_record(93, commandBuffer, args, 3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferPushDebugGroupDynamic(NVNcommandBuffer *commandBuffer, uint32_t arg1,
                                           const char *arg2) {
    uint64_t args[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uintptr_t)arg2;
    nvn_command_buffer_record(94, commandBuffer, args, 3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferPushDebugGroupStatic(NVNcommandBuffer *commandBuffer, uint32_t arg1,
                                          const char *arg2) {
    uint64_t args[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uintptr_t)arg2;
    nvn_command_buffer_record(95, commandBuffer, args, 3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferReportCounter(NVNcommandBuffer *commandBuffer, NVNcounterType arg1,
                                   NVNbufferAddress arg2) {
    uint64_t args[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)arg2;
    nvn_command_buffer_record(96, commandBuffer, args, 3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferReportValue(NVNcommandBuffer *commandBuffer, uint32_t arg1,
                                 NVNbufferAddress arg2) {
    uint64_t args[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)arg2;
    nvn_command_buffer_record(97, commandBuffer, args, 3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferResetCounter(NVNcommandBuffer *commandBuffer, NVNcounterType arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    nvn_command_buffer_record(98, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferResolveDepthBuffer(NVNcommandBuffer *commandBuffer) {
    uint64_t args[1];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    nvn_command_buffer_record(99, commandBuffer, args, 1);
    NVN_VOID_RETURN();
}

void nvnCommandBufferRestoreZCullData(NVNcommandBuffer *commandBuffer, NVNbufferAddress arg1,
                                      size_t arg2) {
    uint64_t args[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)arg1;
    args[2] = (uint64_t)arg2;
    nvn_command_buffer_record(100, commandBuffer, args, 3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferResumeTransformFeedback(NVNcommandBuffer *commandBuffer,
                                             NVNbufferAddress arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)arg1;
    nvn_command_buffer_record(101, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSaveZCullData(NVNcommandBuffer *commandBuffer, NVNbufferAddress arg1,
                                   size_t arg2) {
    uint64_t args[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)arg1;
    args[2] = (uint64_t)arg2;
    nvn_command_buffer_record(102, commandBuffer, args, 3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetAlphaRef(NVNcommandBuffer *commandBuffer, float arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = nvn_command_buffer_float_bits(arg1);
    nvn_command_buffer_record(103, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetBlendColor(NVNcommandBuffer *commandBuffer, const float *arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    nvn_command_buffer_record(104, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

/* The signature of nvnCommandBufferSetColorReductionEnable is not in the header this runtime was
 * built against, so the arguments are taken as plain words and written down as they came. Nothing
 * is dereferenced, so a signature with fewer or more arguments cannot do any harm here. The log
 * shows the call shape the device really uses. */
void nvnCommandBufferSetColorReductionEnable(NVNcommandBuffer *commandBuffer, uint64_t arg1,
                                             uint64_t arg2, uint64_t arg3, uint64_t arg4,
                                             uint64_t arg5, uint64_t arg6, uint64_t arg7) {
    uint64_t args[8];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = arg1;
    args[2] = arg2;
    args[3] = arg3;
    args[4] = arg4;
    args[5] = arg5;
    args[6] = arg6;
    args[7] = arg7;
    nvn_command_buffer_record(105, commandBuffer, args, 8);
    nvn_command_buffer_log_raw("nvnCommandBufferSetColorReductionEnable",
                                commandBuffer, args, 8);
    NVN_VOID_RETURN();
}

/* The signature of nvnCommandBufferSetColorReductionThresholds is not in the header this runtime
 * was built against, so the arguments are taken as plain words and written down as they came.
 * Nothing is dereferenced, so a signature with fewer or more arguments cannot do any harm here. The
 * log shows the call shape the device really uses. */
void nvnCommandBufferSetColorReductionThresholds(NVNcommandBuffer *commandBuffer, uint64_t arg1,
                                                 uint64_t arg2, uint64_t arg3, uint64_t arg4,
                                                 uint64_t arg5, uint64_t arg6, uint64_t arg7) {
    uint64_t args[8];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = arg1;
    args[2] = arg2;
    args[3] = arg3;
    args[4] = arg4;
    args[5] = arg5;
    args[6] = arg6;
    args[7] = arg7;
    nvn_command_buffer_record(106, commandBuffer, args, 8);
    nvn_command_buffer_log_raw("nvnCommandBufferSetColorReductionThresholds",
                                commandBuffer, args, 8);
    NVN_VOID_RETURN();
}

/* The signature of nvnCommandBufferSetCommandMemoryCallbackEnabled is not in the header this
 * runtime was built against, so the arguments are taken as plain words and written down as they
 * came. Nothing is dereferenced, so a signature with fewer or more arguments cannot do any harm
 * here. The log shows the call shape the device really uses. */
void nvnCommandBufferSetCommandMemoryCallbackEnabled(NVNcommandBuffer *commandBuffer, uint64_t arg1,
                                                     uint64_t arg2, uint64_t arg3, uint64_t arg4,
                                                     uint64_t arg5, uint64_t arg6, uint64_t arg7) {
    uint64_t args[8];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = arg1;
    args[2] = arg2;
    args[3] = arg3;
    args[4] = arg4;
    args[5] = arg5;
    args[6] = arg6;
    args[7] = arg7;
    nvn_command_buffer_record(107, commandBuffer, args, 8);
    nvn_command_buffer_log_raw("nvnCommandBufferSetCommandMemoryCallbackEnabled",
                                commandBuffer, args, 8);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetConservativeRasterDilate(NVNcommandBuffer *commandBuffer, float arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = nvn_command_buffer_float_bits(arg1);
    nvn_command_buffer_record(108, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetConservativeRasterEnable(NVNcommandBuffer *commandBuffer, NVNboolean arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint8_t)arg1;
    nvn_command_buffer_record(109, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

/* The signature of nvnCommandBufferSetControlMemoryCallbackEnabled is not in the header this
 * runtime was built against, so the arguments are taken as plain words and written down as they
 * came. Nothing is dereferenced, so a signature with fewer or more arguments cannot do any harm
 * here. The log shows the call shape the device really uses. */
void nvnCommandBufferSetControlMemoryCallbackEnabled(NVNcommandBuffer *commandBuffer, uint64_t arg1,
                                                     uint64_t arg2, uint64_t arg3, uint64_t arg4,
                                                     uint64_t arg5, uint64_t arg6, uint64_t arg7) {
    uint64_t args[8];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = arg1;
    args[2] = arg2;
    args[3] = arg3;
    args[4] = arg4;
    args[5] = arg5;
    args[6] = arg6;
    args[7] = arg7;
    nvn_command_buffer_record(110, commandBuffer, args, 8);
    nvn_command_buffer_log_raw("nvnCommandBufferSetControlMemoryCallbackEnabled",
                                commandBuffer, args, 8);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetCopyImageStride(NVNcommandBuffer *commandBuffer, ptrdiff_t arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)arg1;
    nvn_command_buffer_record(111, commandBuffer, args, 2);
    nvn_command_buffer_set_copy_image_stride(commandBuffer, (int64_t)arg1);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetCopyRowStride(NVNcommandBuffer *commandBuffer, ptrdiff_t arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)arg1;
    nvn_command_buffer_record(112, commandBuffer, args, 2);
    nvn_command_buffer_set_copy_row_stride(commandBuffer, (int64_t)arg1);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetDebugLabel(NVNcommandBuffer *commandBuffer, const char *arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    nvn_command_buffer_record(113, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetDepthBounds(NVNcommandBuffer *commandBuffer, NVNboolean arg1, float arg2,
                                    float arg3) {
    uint64_t args[4];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint8_t)arg1;
    args[2] = nvn_command_buffer_float_bits(arg2);
    args[3] = nvn_command_buffer_float_bits(arg3);
    nvn_command_buffer_record(114, commandBuffer, args, 4);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetDepthClamp(NVNcommandBuffer *commandBuffer, NVNboolean arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint8_t)arg1;
    nvn_command_buffer_record(115, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetDepthRange(NVNcommandBuffer *commandBuffer, float arg1, float arg2) {
    uint64_t args[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = nvn_command_buffer_float_bits(arg1);
    args[2] = nvn_command_buffer_float_bits(arg2);
    nvn_command_buffer_record(116, commandBuffer, args, 3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetDepthRanges(NVNcommandBuffer *commandBuffer, int arg1, int arg2,
                                    const float *arg3) {
    uint64_t args[4];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)(uintptr_t)arg3;
    nvn_command_buffer_record(117, commandBuffer, args, 4);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetInnerTessellationLevels(NVNcommandBuffer *commandBuffer,
                                                const float *arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    nvn_command_buffer_record(118, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetLineWidth(NVNcommandBuffer *commandBuffer, float arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = nvn_command_buffer_float_bits(arg1);
    nvn_command_buffer_record(119, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetMemoryCallback(NVNcommandBuffer *commandBuffer,
                                       PFNNVNCOMMANDBUFFERMEMORYCALLBACKPROC arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    nvn_command_buffer_record(120, commandBuffer, args, 2);
    nvn_command_buffer_set_memory_callback(commandBuffer, (uint64_t)(uintptr_t)arg1);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetMemoryCallbackData(NVNcommandBuffer *commandBuffer, void *arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    nvn_command_buffer_record(121, commandBuffer, args, 2);
    nvn_command_buffer_set_memory_callback_data(commandBuffer, (uint64_t)(uintptr_t)arg1);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetOuterTessellationLevels(NVNcommandBuffer *commandBuffer,
                                                const float *arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    nvn_command_buffer_record(122, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetPatchSize(NVNcommandBuffer *commandBuffer, int arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    nvn_command_buffer_record(123, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetPointSize(NVNcommandBuffer *commandBuffer, float arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = nvn_command_buffer_float_bits(arg1);
    nvn_command_buffer_record(124, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetPolygonOffsetClamp(NVNcommandBuffer *commandBuffer, float arg1, float arg2,
                                           float arg3) {
    uint64_t args[4];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = nvn_command_buffer_float_bits(arg1);
    args[2] = nvn_command_buffer_float_bits(arg2);
    args[3] = nvn_command_buffer_float_bits(arg3);
    nvn_command_buffer_record(125, commandBuffer, args, 4);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetPrimitiveRestart(NVNcommandBuffer *commandBuffer, NVNboolean arg1,
                                         int arg2) {
    uint64_t args[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint8_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    nvn_command_buffer_record(126, commandBuffer, args, 3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetProgramSubroutines(NVNcommandBuffer *commandBuffer, NVNprogram *arg1,
                                           NVNshaderStage arg2, const int arg3, const int arg4,
                                           const int *arg5) {
    uint64_t args[6];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)(uint32_t)arg3;
    args[4] = (uint64_t)(uint32_t)arg4;
    args[5] = (uint64_t)(uintptr_t)arg5;
    nvn_command_buffer_record(127, commandBuffer, args, 6);
    NVN_VOID_RETURN();
}

/* The signature of nvnCommandBufferSetProvokingVertexMode is not in the header this runtime was
 * built against, so the arguments are taken as plain words and written down as they came. Nothing
 * is dereferenced, so a signature with fewer or more arguments cannot do any harm here. The log
 * shows the call shape the device really uses. */
void nvnCommandBufferSetProvokingVertexMode(NVNcommandBuffer *commandBuffer, uint64_t arg1,
                                            uint64_t arg2, uint64_t arg3, uint64_t arg4,
                                            uint64_t arg5, uint64_t arg6, uint64_t arg7) {
    uint64_t args[8];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = arg1;
    args[2] = arg2;
    args[3] = arg3;
    args[4] = arg4;
    args[5] = arg5;
    args[6] = arg6;
    args[7] = arg7;
    nvn_command_buffer_record(128, commandBuffer, args, 8);
    nvn_command_buffer_log_raw("nvnCommandBufferSetProvokingVertexMode",
                                commandBuffer, args, 8);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetRasterizerDiscard(NVNcommandBuffer *commandBuffer, NVNboolean arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint8_t)arg1;
    nvn_command_buffer_record(129, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetRenderEnable(NVNcommandBuffer *commandBuffer, NVNboolean arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint8_t)arg1;
    nvn_command_buffer_record(130, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetRenderEnableConditional(NVNcommandBuffer *commandBuffer,
                                                NVNconditionalRenderMode arg1,
                                                NVNbufferAddress arg2) {
    uint64_t args[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)arg2;
    nvn_command_buffer_record(131, commandBuffer, args, 3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetRenderTargets(NVNcommandBuffer *commandBuffer, int arg1,
                                      const NVNtexture* const *arg2,
                                      const NVNtextureView* const *arg3, const NVNtexture *arg4,
                                      const NVNtextureView *arg5) {
    uint64_t args[6];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uintptr_t)arg2;
    args[3] = (uint64_t)(uintptr_t)arg3;
    args[4] = (uint64_t)(uintptr_t)arg4;
    args[5] = (uint64_t)(uintptr_t)arg5;
    nvn_command_buffer_record(132, commandBuffer, args, 6);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetSampleMask(NVNcommandBuffer *commandBuffer, int arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    nvn_command_buffer_record(133, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetSamplerPool(NVNcommandBuffer *commandBuffer, const NVNsamplerPool *arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    nvn_command_buffer_record(134, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetScissor(NVNcommandBuffer *commandBuffer, int arg1, int arg2, int arg3,
                                int arg4) {
    uint64_t args[5];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)(uint32_t)arg3;
    args[4] = (uint64_t)(uint32_t)arg4;
    nvn_command_buffer_record(135, commandBuffer, args, 5);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetScissors(NVNcommandBuffer *commandBuffer, int arg1, int arg2,
                                 const int *arg3) {
    uint64_t args[4];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)(uintptr_t)arg3;
    nvn_command_buffer_record(136, commandBuffer, args, 4);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetShaderScratchMemory(NVNcommandBuffer *commandBuffer,
                                            const NVNmemoryPool *arg1, ptrdiff_t arg2,
                                            size_t arg3) {
    uint64_t args[4];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    args[2] = (uint64_t)arg2;
    args[3] = (uint64_t)arg3;
    nvn_command_buffer_record(137, commandBuffer, args, 4);
    NVN_VOID_RETURN();
}

/* The signature of nvnCommandBufferSetStencilCullCriteria is not in the header this runtime was
 * built against, so the arguments are taken as plain words and written down as they came. Nothing
 * is dereferenced, so a signature with fewer or more arguments cannot do any harm here. The log
 * shows the call shape the device really uses. */
void nvnCommandBufferSetStencilCullCriteria(NVNcommandBuffer *commandBuffer, uint64_t arg1,
                                            uint64_t arg2, uint64_t arg3, uint64_t arg4,
                                            uint64_t arg5, uint64_t arg6, uint64_t arg7) {
    uint64_t args[8];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = arg1;
    args[2] = arg2;
    args[3] = arg3;
    args[4] = arg4;
    args[5] = arg5;
    args[6] = arg6;
    args[7] = arg7;
    nvn_command_buffer_record(138, commandBuffer, args, 8);
    nvn_command_buffer_log_raw("nvnCommandBufferSetStencilCullCriteria",
                                commandBuffer, args, 8);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetStencilMask(NVNcommandBuffer *commandBuffer, NVNface arg1, int arg2) {
    uint64_t args[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    nvn_command_buffer_record(139, commandBuffer, args, 3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetStencilRef(NVNcommandBuffer *commandBuffer, NVNface arg1, int arg2) {
    uint64_t args[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    nvn_command_buffer_record(140, commandBuffer, args, 3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetStencilValueMask(NVNcommandBuffer *commandBuffer, NVNface arg1, int arg2) {
    uint64_t args[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    nvn_command_buffer_record(141, commandBuffer, args, 3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetSubpixelPrecisionBias(NVNcommandBuffer *commandBuffer, int arg1, int arg2) {
    uint64_t args[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    nvn_command_buffer_record(142, commandBuffer, args, 3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetTexturePool(NVNcommandBuffer *commandBuffer, const NVNtexturePool *arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    nvn_command_buffer_record(143, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetTiledCacheAction(NVNcommandBuffer *commandBuffer,
                                         NVNtiledCacheAction arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    nvn_command_buffer_record(144, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetTiledCacheTileSize(NVNcommandBuffer *commandBuffer, int arg1, int arg2) {
    uint64_t args[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    nvn_command_buffer_record(145, commandBuffer, args, 3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetViewport(NVNcommandBuffer *commandBuffer, int arg1, int arg2, int arg3,
                                 int arg4) {
    uint64_t args[5];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)(uint32_t)arg3;
    args[4] = (uint64_t)(uint32_t)arg4;
    nvn_command_buffer_record(146, commandBuffer, args, 5);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetViewportSwizzles(NVNcommandBuffer *commandBuffer, int arg1, int arg2,
                                         const NVNviewportSwizzle *arg3) {
    uint64_t args[4];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)(uintptr_t)arg3;
    nvn_command_buffer_record(147, commandBuffer, args, 4);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetViewports(NVNcommandBuffer *commandBuffer, int arg1, int arg2,
                                  const float *arg3) {
    uint64_t args[4];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)(uintptr_t)arg3;
    nvn_command_buffer_record(148, commandBuffer, args, 4);
    NVN_VOID_RETURN();
}

/* The signature of nvnCommandBufferSetZCullZF32CompressionEnable is not in the header this runtime
 * was built against, so the arguments are taken as plain words and written down as they came.
 * Nothing is dereferenced, so a signature with fewer or more arguments cannot do any harm here. The
 * log shows the call shape the device really uses. */
void nvnCommandBufferSetZCullZF32CompressionEnable(NVNcommandBuffer *commandBuffer, uint64_t arg1,
                                                   uint64_t arg2, uint64_t arg3, uint64_t arg4,
                                                   uint64_t arg5, uint64_t arg6, uint64_t arg7) {
    uint64_t args[8];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = arg1;
    args[2] = arg2;
    args[3] = arg3;
    args[4] = arg4;
    args[5] = arg5;
    args[6] = arg6;
    args[7] = arg7;
    nvn_command_buffer_record(149, commandBuffer, args, 8);
    nvn_command_buffer_log_raw("nvnCommandBufferSetZCullZF32CompressionEnable",
                                commandBuffer, args, 8);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSignalEvent(NVNcommandBuffer *commandBuffer, const NVNevent *arg1,
                                 NVNeventSignalMode arg2, NVNeventSignalLocation arg3, int arg4,
                                 uint32_t arg5) {
    uint64_t args[6];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)(uint32_t)arg3;
    args[4] = (uint64_t)(uint32_t)arg4;
    args[5] = (uint64_t)(uint32_t)arg5;
    nvn_command_buffer_record(150, commandBuffer, args, 6);
    NVN_VOID_RETURN();
}

void nvnCommandBufferTiledDownsample(NVNcommandBuffer *commandBuffer, const NVNtexture *arg1,
                                     const NVNtexture *arg2) {
    uint64_t args[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    args[2] = (uint64_t)(uintptr_t)arg2;
    nvn_command_buffer_record(151, commandBuffer, args, 3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferTiledDownsampleTextureView(NVNcommandBuffer *commandBuffer,
                                                const NVNtexture *arg1, const NVNtextureView *arg2,
                                                const NVNtexture *arg3,
                                                const NVNtextureView *arg4) {
    uint64_t args[5];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    args[2] = (uint64_t)(uintptr_t)arg2;
    args[3] = (uint64_t)(uintptr_t)arg3;
    args[4] = (uint64_t)(uintptr_t)arg4;
    nvn_command_buffer_record(152, commandBuffer, args, 5);
    NVN_VOID_RETURN();
}

void nvnCommandBufferUpdateUniformBuffer(NVNcommandBuffer *commandBuffer, NVNbufferAddress arg1,
                                         size_t arg2, ptrdiff_t arg3, size_t arg4,
                                         const void *arg5) {
    uint64_t args[6];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)arg1;
    args[2] = (uint64_t)arg2;
    args[3] = (uint64_t)arg3;
    args[4] = (uint64_t)arg4;
    args[5] = (uint64_t)(uintptr_t)arg5;
    nvn_command_buffer_record(153, commandBuffer, args, 6);
    NVN_VOID_RETURN();
}

void nvnCommandBufferWaitEvent(NVNcommandBuffer *commandBuffer, const NVNevent *arg1,
                               NVNeventWaitMode arg2, uint32_t arg3) {
    uint64_t args[4];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)(uint32_t)arg3;
    nvn_command_buffer_record(154, commandBuffer, args, 4);
    NVN_VOID_RETURN();
}

void nvnCommandBufferWaitSync(NVNcommandBuffer *commandBuffer, const NVNsync *arg1) {
    uint64_t args[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    nvn_command_buffer_record(155, commandBuffer, args, 2);
    NVN_VOID_RETURN();
}

/* ------------------------------------------------------------------ */
/* The names                                                           */
/* ------------------------------------------------------------------ */

static const nvn_command_buffer_family_entry s_command_buffer_family[] = {
    { "nvnCommandBufferAddCommandMemory", (void *)(uintptr_t)nvnCommandBufferAddCommandMemory },
    { "nvnCommandBufferAddControlMemory", (void *)(uintptr_t)nvnCommandBufferAddControlMemory },
    { "nvnCommandBufferBarrier", (void *)(uintptr_t)nvnCommandBufferBarrier },
    { "nvnCommandBufferBeginRecording", (void *)(uintptr_t)nvnCommandBufferBeginRecording },
    { "nvnCommandBufferBeginTransformFeedback",
      (void *)(uintptr_t)nvnCommandBufferBeginTransformFeedback },
    { "nvnCommandBufferBindBlendState", (void *)(uintptr_t)nvnCommandBufferBindBlendState },
    { "nvnCommandBufferBindChannelMaskState",
      (void *)(uintptr_t)nvnCommandBufferBindChannelMaskState },
    { "nvnCommandBufferBindColorState", (void *)(uintptr_t)nvnCommandBufferBindColorState },
    { "nvnCommandBufferBindCoverageModulationTable",
      (void *)(uintptr_t)nvnCommandBufferBindCoverageModulationTable },
    { "nvnCommandBufferBindDepthStencilState",
      (void *)(uintptr_t)nvnCommandBufferBindDepthStencilState },
    { "nvnCommandBufferBindImage", (void *)(uintptr_t)nvnCommandBufferBindImage },
    { "nvnCommandBufferBindImages", (void *)(uintptr_t)nvnCommandBufferBindImages },
    { "nvnCommandBufferBindImagesDeferred", (void *)(uintptr_t)nvnCommandBufferBindImagesDeferred },
    { "nvnCommandBufferBindMultisampleState",
      (void *)(uintptr_t)nvnCommandBufferBindMultisampleState },
    { "nvnCommandBufferBindPolygonState", (void *)(uintptr_t)nvnCommandBufferBindPolygonState },
    { "nvnCommandBufferBindProgram", (void *)(uintptr_t)nvnCommandBufferBindProgram },
    { "nvnCommandBufferBindSeparateSampler",
      (void *)(uintptr_t)nvnCommandBufferBindSeparateSampler },
    { "nvnCommandBufferBindSeparateSamplers",
      (void *)(uintptr_t)nvnCommandBufferBindSeparateSamplers },
    { "nvnCommandBufferBindSeparateSamplersDeferred",
      (void *)(uintptr_t)nvnCommandBufferBindSeparateSamplersDeferred },
    { "nvnCommandBufferBindSeparateTexture",
      (void *)(uintptr_t)nvnCommandBufferBindSeparateTexture },
    { "nvnCommandBufferBindSeparateTextures",
      (void *)(uintptr_t)nvnCommandBufferBindSeparateTextures },
    { "nvnCommandBufferBindSeparateTexturesDeferred",
      (void *)(uintptr_t)nvnCommandBufferBindSeparateTexturesDeferred },
    { "nvnCommandBufferBindStorageBuffer", (void *)(uintptr_t)nvnCommandBufferBindStorageBuffer },
    { "nvnCommandBufferBindStorageBuffers", (void *)(uintptr_t)nvnCommandBufferBindStorageBuffers },
    { "nvnCommandBufferBindStorageBuffersDeferred",
      (void *)(uintptr_t)nvnCommandBufferBindStorageBuffersDeferred },
    { "nvnCommandBufferBindTexture", (void *)(uintptr_t)nvnCommandBufferBindTexture },
    { "nvnCommandBufferBindTextures", (void *)(uintptr_t)nvnCommandBufferBindTextures },
    { "nvnCommandBufferBindTexturesDeferred",
      (void *)(uintptr_t)nvnCommandBufferBindTexturesDeferred },
    { "nvnCommandBufferBindTransformFeedbackBuffer",
      (void *)(uintptr_t)nvnCommandBufferBindTransformFeedbackBuffer },
    { "nvnCommandBufferBindTransformFeedbackBuffers",
      (void *)(uintptr_t)nvnCommandBufferBindTransformFeedbackBuffers },
    { "nvnCommandBufferBindUniformBuffer", (void *)(uintptr_t)nvnCommandBufferBindUniformBuffer },
    { "nvnCommandBufferBindUniformBuffers", (void *)(uintptr_t)nvnCommandBufferBindUniformBuffers },
    { "nvnCommandBufferBindUniformBuffersDeferred",
      (void *)(uintptr_t)nvnCommandBufferBindUniformBuffersDeferred },
    { "nvnCommandBufferBindVertexAttribState",
      (void *)(uintptr_t)nvnCommandBufferBindVertexAttribState },
    { "nvnCommandBufferBindVertexBuffer", (void *)(uintptr_t)nvnCommandBufferBindVertexBuffer },
    { "nvnCommandBufferBindVertexBuffers", (void *)(uintptr_t)nvnCommandBufferBindVertexBuffers },
    { "nvnCommandBufferBindVertexStreamState",
      (void *)(uintptr_t)nvnCommandBufferBindVertexStreamState },
    { "nvnCommandBufferCallCommands", (void *)(uintptr_t)nvnCommandBufferCallCommands },
    { "nvnCommandBufferClearBuffer", (void *)(uintptr_t)nvnCommandBufferClearBuffer },
    { "nvnCommandBufferClearColor", (void *)(uintptr_t)nvnCommandBufferClearColor },
    { "nvnCommandBufferClearColori", (void *)(uintptr_t)nvnCommandBufferClearColori },
    { "nvnCommandBufferClearColorui", (void *)(uintptr_t)nvnCommandBufferClearColorui },
    { "nvnCommandBufferClearDepthStencil", (void *)(uintptr_t)nvnCommandBufferClearDepthStencil },
    { "nvnCommandBufferClearTexture", (void *)(uintptr_t)nvnCommandBufferClearTexture },
    { "nvnCommandBufferClearTexturei", (void *)(uintptr_t)nvnCommandBufferClearTexturei },
    { "nvnCommandBufferClearTextureui", (void *)(uintptr_t)nvnCommandBufferClearTextureui },
    { "nvnCommandBufferCopyBufferToBuffer", (void *)(uintptr_t)nvnCommandBufferCopyBufferToBuffer },
    { "nvnCommandBufferCopyBufferToTexture",
      (void *)(uintptr_t)nvnCommandBufferCopyBufferToTexture },
    { "nvnCommandBufferCopyCommands", (void *)(uintptr_t)nvnCommandBufferCopyCommands },
    { "nvnCommandBufferCopyTextureToBuffer",
      (void *)(uintptr_t)nvnCommandBufferCopyTextureToBuffer },
    { "nvnCommandBufferCopyTextureToTexture",
      (void *)(uintptr_t)nvnCommandBufferCopyTextureToTexture },
    { "nvnCommandBufferDiscardColor", (void *)(uintptr_t)nvnCommandBufferDiscardColor },
    { "nvnCommandBufferDiscardDepthStencil",
      (void *)(uintptr_t)nvnCommandBufferDiscardDepthStencil },
    { "nvnCommandBufferDispatchCompute", (void *)(uintptr_t)nvnCommandBufferDispatchCompute },
    { "nvnCommandBufferDispatchComputeIndirect",
      (void *)(uintptr_t)nvnCommandBufferDispatchComputeIndirect },
    { "nvnCommandBufferDownsample", (void *)(uintptr_t)nvnCommandBufferDownsample },
    { "nvnCommandBufferDownsampleTextureView",
      (void *)(uintptr_t)nvnCommandBufferDownsampleTextureView },
    { "nvnCommandBufferDrawArrays", (void *)(uintptr_t)nvnCommandBufferDrawArrays },
    { "nvnCommandBufferDrawArraysIndirect", (void *)(uintptr_t)nvnCommandBufferDrawArraysIndirect },
    { "nvnCommandBufferDrawArraysInstanced",
      (void *)(uintptr_t)nvnCommandBufferDrawArraysInstanced },
    { "nvnCommandBufferDrawElements", (void *)(uintptr_t)nvnCommandBufferDrawElements },
    { "nvnCommandBufferDrawElementsBaseVertex",
      (void *)(uintptr_t)nvnCommandBufferDrawElementsBaseVertex },
    { "nvnCommandBufferDrawElementsIndirect",
      (void *)(uintptr_t)nvnCommandBufferDrawElementsIndirect },
    { "nvnCommandBufferDrawElementsInstanced",
      (void *)(uintptr_t)nvnCommandBufferDrawElementsInstanced },
    { "nvnCommandBufferDrawTexture", (void *)(uintptr_t)nvnCommandBufferDrawTexture },
    { "nvnCommandBufferDrawTransformFeedback",
      (void *)(uintptr_t)nvnCommandBufferDrawTransformFeedback },
    { "nvnCommandBufferEndRecording", (void *)(uintptr_t)nvnCommandBufferEndRecording },
    { "nvnCommandBufferEndTransformFeedback",
      (void *)(uintptr_t)nvnCommandBufferEndTransformFeedback },
    { "nvnCommandBufferFenceSync", (void *)(uintptr_t)nvnCommandBufferFenceSync },
    { "nvnCommandBufferFinalize", (void *)(uintptr_t)nvnCommandBufferFinalize },
    { "nvnCommandBufferGetCommandMemoryFree",
      (void *)(uintptr_t)nvnCommandBufferGetCommandMemoryFree },
    { "nvnCommandBufferGetCommandMemorySize",
      (void *)(uintptr_t)nvnCommandBufferGetCommandMemorySize },
    { "nvnCommandBufferGetCommandMemoryUsed",
      (void *)(uintptr_t)nvnCommandBufferGetCommandMemoryUsed },
    { "nvnCommandBufferGetControlMemoryFree",
      (void *)(uintptr_t)nvnCommandBufferGetControlMemoryFree },
    { "nvnCommandBufferGetControlMemorySize",
      (void *)(uintptr_t)nvnCommandBufferGetControlMemorySize },
    { "nvnCommandBufferGetControlMemoryUsed",
      (void *)(uintptr_t)nvnCommandBufferGetControlMemoryUsed },
    { "nvnCommandBufferGetCopyImageStride", (void *)(uintptr_t)nvnCommandBufferGetCopyImageStride },
    { "nvnCommandBufferGetCopyRowStride", (void *)(uintptr_t)nvnCommandBufferGetCopyRowStride },
    { "nvnCommandBufferGetMemoryCallback", (void *)(uintptr_t)nvnCommandBufferGetMemoryCallback },
    { "nvnCommandBufferGetMemoryCallbackData",
      (void *)(uintptr_t)nvnCommandBufferGetMemoryCallbackData },
    { "nvnCommandBufferInitialize", (void *)(uintptr_t)nvnCommandBufferInitialize },
    { "nvnCommandBufferInsertCheckpointDynamic",
      (void *)(uintptr_t)nvnCommandBufferInsertCheckpointDynamic },
    { "nvnCommandBufferInsertCheckpointStatic",
      (void *)(uintptr_t)nvnCommandBufferInsertCheckpointStatic },
    { "nvnCommandBufferInsertDebugMarker", (void *)(uintptr_t)nvnCommandBufferInsertDebugMarker },
    { "nvnCommandBufferInsertDebugMarkerDynamic",
      (void *)(uintptr_t)nvnCommandBufferInsertDebugMarkerDynamic },
    { "nvnCommandBufferInsertDebugMarkerStatic",
      (void *)(uintptr_t)nvnCommandBufferInsertDebugMarkerStatic },
    { "nvnCommandBufferIsRecording", (void *)(uintptr_t)nvnCommandBufferIsRecording },
    { "nvnCommandBufferMultiDrawArraysIndirectCount",
      (void *)(uintptr_t)nvnCommandBufferMultiDrawArraysIndirectCount },
    { "nvnCommandBufferMultiDrawElementsIndirectCount",
      (void *)(uintptr_t)nvnCommandBufferMultiDrawElementsIndirectCount },
    { "nvnCommandBufferOverrideSubtileSize",
      (void *)(uintptr_t)nvnCommandBufferOverrideSubtileSize },
    { "nvnCommandBufferPauseTransformFeedback",
      (void *)(uintptr_t)nvnCommandBufferPauseTransformFeedback },
    { "nvnCommandBufferPopDebugGroup", (void *)(uintptr_t)nvnCommandBufferPopDebugGroup },
    { "nvnCommandBufferPopDebugGroupId", (void *)(uintptr_t)nvnCommandBufferPopDebugGroupId },
    { "nvnCommandBufferPushDebugGroup", (void *)(uintptr_t)nvnCommandBufferPushDebugGroup },
    { "nvnCommandBufferPushDebugGroupDynamic",
      (void *)(uintptr_t)nvnCommandBufferPushDebugGroupDynamic },
    { "nvnCommandBufferPushDebugGroupStatic",
      (void *)(uintptr_t)nvnCommandBufferPushDebugGroupStatic },
    { "nvnCommandBufferReportCounter", (void *)(uintptr_t)nvnCommandBufferReportCounter },
    { "nvnCommandBufferReportValue", (void *)(uintptr_t)nvnCommandBufferReportValue },
    { "nvnCommandBufferResetCounter", (void *)(uintptr_t)nvnCommandBufferResetCounter },
    { "nvnCommandBufferResolveDepthBuffer", (void *)(uintptr_t)nvnCommandBufferResolveDepthBuffer },
    { "nvnCommandBufferRestoreZCullData", (void *)(uintptr_t)nvnCommandBufferRestoreZCullData },
    { "nvnCommandBufferResumeTransformFeedback",
      (void *)(uintptr_t)nvnCommandBufferResumeTransformFeedback },
    { "nvnCommandBufferSaveZCullData", (void *)(uintptr_t)nvnCommandBufferSaveZCullData },
    { "nvnCommandBufferSetAlphaRef", (void *)(uintptr_t)nvnCommandBufferSetAlphaRef },
    { "nvnCommandBufferSetBlendColor", (void *)(uintptr_t)nvnCommandBufferSetBlendColor },
    { "nvnCommandBufferSetColorReductionEnable",
      (void *)(uintptr_t)nvnCommandBufferSetColorReductionEnable },
    { "nvnCommandBufferSetColorReductionThresholds",
      (void *)(uintptr_t)nvnCommandBufferSetColorReductionThresholds },
    { "nvnCommandBufferSetCommandMemoryCallbackEnabled",
      (void *)(uintptr_t)nvnCommandBufferSetCommandMemoryCallbackEnabled },
    { "nvnCommandBufferSetConservativeRasterDilate",
      (void *)(uintptr_t)nvnCommandBufferSetConservativeRasterDilate },
    { "nvnCommandBufferSetConservativeRasterEnable",
      (void *)(uintptr_t)nvnCommandBufferSetConservativeRasterEnable },
    { "nvnCommandBufferSetControlMemoryCallbackEnabled",
      (void *)(uintptr_t)nvnCommandBufferSetControlMemoryCallbackEnabled },
    { "nvnCommandBufferSetCopyImageStride", (void *)(uintptr_t)nvnCommandBufferSetCopyImageStride },
    { "nvnCommandBufferSetCopyRowStride", (void *)(uintptr_t)nvnCommandBufferSetCopyRowStride },
    { "nvnCommandBufferSetDebugLabel", (void *)(uintptr_t)nvnCommandBufferSetDebugLabel },
    { "nvnCommandBufferSetDepthBounds", (void *)(uintptr_t)nvnCommandBufferSetDepthBounds },
    { "nvnCommandBufferSetDepthClamp", (void *)(uintptr_t)nvnCommandBufferSetDepthClamp },
    { "nvnCommandBufferSetDepthRange", (void *)(uintptr_t)nvnCommandBufferSetDepthRange },
    { "nvnCommandBufferSetDepthRanges", (void *)(uintptr_t)nvnCommandBufferSetDepthRanges },
    { "nvnCommandBufferSetInnerTessellationLevels",
      (void *)(uintptr_t)nvnCommandBufferSetInnerTessellationLevels },
    { "nvnCommandBufferSetLineWidth", (void *)(uintptr_t)nvnCommandBufferSetLineWidth },
    { "nvnCommandBufferSetMemoryCallback", (void *)(uintptr_t)nvnCommandBufferSetMemoryCallback },
    { "nvnCommandBufferSetMemoryCallbackData",
      (void *)(uintptr_t)nvnCommandBufferSetMemoryCallbackData },
    { "nvnCommandBufferSetOuterTessellationLevels",
      (void *)(uintptr_t)nvnCommandBufferSetOuterTessellationLevels },
    { "nvnCommandBufferSetPatchSize", (void *)(uintptr_t)nvnCommandBufferSetPatchSize },
    { "nvnCommandBufferSetPointSize", (void *)(uintptr_t)nvnCommandBufferSetPointSize },
    { "nvnCommandBufferSetPolygonOffsetClamp",
      (void *)(uintptr_t)nvnCommandBufferSetPolygonOffsetClamp },
    { "nvnCommandBufferSetPrimitiveRestart",
      (void *)(uintptr_t)nvnCommandBufferSetPrimitiveRestart },
    { "nvnCommandBufferSetProgramSubroutines",
      (void *)(uintptr_t)nvnCommandBufferSetProgramSubroutines },
    { "nvnCommandBufferSetProvokingVertexMode",
      (void *)(uintptr_t)nvnCommandBufferSetProvokingVertexMode },
    { "nvnCommandBufferSetRasterizerDiscard",
      (void *)(uintptr_t)nvnCommandBufferSetRasterizerDiscard },
    { "nvnCommandBufferSetRenderEnable", (void *)(uintptr_t)nvnCommandBufferSetRenderEnable },
    { "nvnCommandBufferSetRenderEnableConditional",
      (void *)(uintptr_t)nvnCommandBufferSetRenderEnableConditional },
    { "nvnCommandBufferSetRenderTargets", (void *)(uintptr_t)nvnCommandBufferSetRenderTargets },
    { "nvnCommandBufferSetSampleMask", (void *)(uintptr_t)nvnCommandBufferSetSampleMask },
    { "nvnCommandBufferSetSamplerPool", (void *)(uintptr_t)nvnCommandBufferSetSamplerPool },
    { "nvnCommandBufferSetScissor", (void *)(uintptr_t)nvnCommandBufferSetScissor },
    { "nvnCommandBufferSetScissors", (void *)(uintptr_t)nvnCommandBufferSetScissors },
    { "nvnCommandBufferSetShaderScratchMemory",
      (void *)(uintptr_t)nvnCommandBufferSetShaderScratchMemory },
    { "nvnCommandBufferSetStencilCullCriteria",
      (void *)(uintptr_t)nvnCommandBufferSetStencilCullCriteria },
    { "nvnCommandBufferSetStencilMask", (void *)(uintptr_t)nvnCommandBufferSetStencilMask },
    { "nvnCommandBufferSetStencilRef", (void *)(uintptr_t)nvnCommandBufferSetStencilRef },
    { "nvnCommandBufferSetStencilValueMask",
      (void *)(uintptr_t)nvnCommandBufferSetStencilValueMask },
    { "nvnCommandBufferSetSubpixelPrecisionBias",
      (void *)(uintptr_t)nvnCommandBufferSetSubpixelPrecisionBias },
    { "nvnCommandBufferSetTexturePool", (void *)(uintptr_t)nvnCommandBufferSetTexturePool },
    { "nvnCommandBufferSetTiledCacheAction",
      (void *)(uintptr_t)nvnCommandBufferSetTiledCacheAction },
    { "nvnCommandBufferSetTiledCacheTileSize",
      (void *)(uintptr_t)nvnCommandBufferSetTiledCacheTileSize },
    { "nvnCommandBufferSetViewport", (void *)(uintptr_t)nvnCommandBufferSetViewport },
    { "nvnCommandBufferSetViewportSwizzles",
      (void *)(uintptr_t)nvnCommandBufferSetViewportSwizzles },
    { "nvnCommandBufferSetViewports", (void *)(uintptr_t)nvnCommandBufferSetViewports },
    { "nvnCommandBufferSetZCullZF32CompressionEnable",
      (void *)(uintptr_t)nvnCommandBufferSetZCullZF32CompressionEnable },
    { "nvnCommandBufferSignalEvent", (void *)(uintptr_t)nvnCommandBufferSignalEvent },
    { "nvnCommandBufferTiledDownsample", (void *)(uintptr_t)nvnCommandBufferTiledDownsample },
    { "nvnCommandBufferTiledDownsampleTextureView",
      (void *)(uintptr_t)nvnCommandBufferTiledDownsampleTextureView },
    { "nvnCommandBufferUpdateUniformBuffer",
      (void *)(uintptr_t)nvnCommandBufferUpdateUniformBuffer },
    { "nvnCommandBufferWaitEvent", (void *)(uintptr_t)nvnCommandBufferWaitEvent },
    { "nvnCommandBufferWaitSync", (void *)(uintptr_t)nvnCommandBufferWaitSync },
};

size_t nvn_command_buffer_family_count(void) {
    return sizeof(s_command_buffer_family) / sizeof(s_command_buffer_family[0]);
}

const nvn_command_buffer_family_entry *nvn_command_buffer_family_entry_at(size_t index) {
    if (index >= nvn_command_buffer_family_count()) return NULL;
    return &s_command_buffer_family[index];
}
