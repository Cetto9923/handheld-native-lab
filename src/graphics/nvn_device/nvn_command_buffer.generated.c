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
 * Every entry point that the format of the game this port runs has a record
 * for writes that record; every other one is written down in the diagnostic
 * channel. The replay of the recorded commands does not exist yet; nothing
 * here pretends that a command was executed.
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


/* The sizes of the SDK objects that the recorders copy at the moment of
 * the call, as the vendored header declares them. The recorder itself does
 * not have the header, so the sizes travel with the call. */
#define NVN_CB_SIZE_NVNblendState 0x8
#define NVN_CB_SIZE_NVNchannelMaskState 0x4
#define NVN_CB_SIZE_NVNcolorState 0x4
#define NVN_CB_SIZE_NVNdepthStencilState 0x8
#define NVN_CB_SIZE_NVNdevice 0x3000
#define NVN_CB_SIZE_NVNevent 0x40
#define NVN_CB_SIZE_NVNmemoryPool 0x100
#define NVN_CB_SIZE_NVNmultisampleState 0x18
#define NVN_CB_SIZE_NVNpolygonState 0x4
#define NVN_CB_SIZE_NVNprogram 0xC0
#define NVN_CB_SIZE_NVNsamplerPool 0x20
#define NVN_CB_SIZE_NVNsync 0x40
#define NVN_CB_SIZE_NVNtexture 0xC0
#define NVN_CB_SIZE_NVNtexturePool 0x20
#define NVN_CB_SIZE_NVNtextureView 0x28
#define NVN_CB_SIZE_NVNvertexAttribState 0x4
#define NVN_CB_SIZE_NVNvertexStreamState 0x8
#define NVN_CB_SIZE_NVNbufferRange 16
#define NVN_CB_SIZE_NVNimageHandle 8
#define NVN_CB_SIZE_NVNseparateSamplerHandle 8
#define NVN_CB_SIZE_NVNseparateTextureHandle 8
#define NVN_CB_SIZE_NVNtextureHandle 8
#define NVN_CB_SIZE_NVNviewportSwizzle 4

/* The flag of a resolve copy, as the reference names it. */
#define NVN_CB_COPY_RESOLVE 0x40000000

/* The entry points. */

void nvnCommandBufferAddCommandMemory(NVNcommandBuffer *commandBuffer, const NVNmemoryPool *arg1,
                                      ptrdiff_t arg2, size_t arg3) {
    (void)arg2;
    (void)arg1;
    nvn_command_buffer_add_command_memory(commandBuffer, (uint64_t)arg3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferAddControlMemory(NVNcommandBuffer *commandBuffer, void *arg1, size_t arg2) {
    (void)arg1;
    nvn_command_buffer_add_control_memory(commandBuffer, (uint64_t)arg2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBarrier(NVNcommandBuffer *commandBuffer, int arg1) {
    nvn_command_buffer_rec_barrier(commandBuffer, arg1);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBeginRecording(NVNcommandBuffer *commandBuffer) {
    nvn_command_buffer_begin_recording(commandBuffer);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBeginTransformFeedback(NVNcommandBuffer *commandBuffer,
                                            NVNbufferAddress arg1) {
    nvn_command_buffer_rec_transform_feedback_state(commandBuffer, 0, arg1);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindBlendState(NVNcommandBuffer *commandBuffer, const NVNblendState *arg1) {
    nvn_command_buffer_rec_bind_state(commandBuffer, 0, arg1, NVN_CB_SIZE_NVNblendState);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindChannelMaskState(NVNcommandBuffer *commandBuffer,
                                          const NVNchannelMaskState *arg1) {
    nvn_command_buffer_rec_bind_state(commandBuffer, 2, arg1, NVN_CB_SIZE_NVNchannelMaskState);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindColorState(NVNcommandBuffer *commandBuffer, const NVNcolorState *arg1) {
    nvn_command_buffer_rec_bind_state(commandBuffer, 1, arg1, NVN_CB_SIZE_NVNcolorState);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for
 * nvnCommandBufferBindCoverageModulationTable, so the call is not recorded in the command stream.
 * It is written down in the diagnostic channel with the arguments as they came; a pointer whose
 * size is not known is marked as one that was not followed. */
void nvnCommandBufferBindCoverageModulationTable(NVNcommandBuffer *commandBuffer,
                                                 const float *arg1) {
    uint64_t args[2];
    uint8_t unresolved[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    unresolved[0] = 0;
    unresolved[1] = 1;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferBindCoverageModulationTable", args, 2, unresolved);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindDepthStencilState(NVNcommandBuffer *commandBuffer,
                                           const NVNdepthStencilState *arg1) {
    nvn_command_buffer_rec_bind_state(commandBuffer, 5, arg1, NVN_CB_SIZE_NVNdepthStencilState);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindImage(NVNcommandBuffer *commandBuffer, NVNshaderStage arg1, int arg2,
                               NVNimageHandle arg3) {
    nvn_command_buffer_rec_bind_image(commandBuffer, arg1, arg2, arg3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindImages(NVNcommandBuffer *commandBuffer, NVNshaderStage arg1, int arg2,
                                int arg3, const NVNimageHandle *arg4) {
    nvn_command_buffer_rec_bind_images(commandBuffer, arg1, arg2, arg3, arg4);
    NVN_VOID_RETURN();
}

/* The signature of nvnCommandBufferBindImagesDeferred is not in the header this runtime was built
 * against, so the arguments are taken as plain words and written down as they came. Nothing is
 * dereferenced, so a signature with fewer or more arguments cannot do any harm here. The call is
 * recorded in the diagnostic channel only: the format of the game this port runs has no record for
 * a command whose shape is not known. */
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
    nvn_command_buffer_record_diagnostic(commandBuffer, "nvnCommandBufferBindImagesDeferred", args, 8);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindMultisampleState(NVNcommandBuffer *commandBuffer,
                                          const NVNmultisampleState *arg1) {
    nvn_command_buffer_rec_bind_state(commandBuffer, 3, arg1, NVN_CB_SIZE_NVNmultisampleState);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindPolygonState(NVNcommandBuffer *commandBuffer,
                                      const NVNpolygonState *arg1) {
    nvn_command_buffer_rec_bind_state(commandBuffer, 4, arg1, NVN_CB_SIZE_NVNpolygonState);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindProgram(NVNcommandBuffer *commandBuffer, const NVNprogram *arg1,
                                 int arg2) {
    nvn_command_buffer_rec_bind_program(commandBuffer, arg1, NVN_CB_SIZE_NVNprogram, arg2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindSeparateSampler(NVNcommandBuffer *commandBuffer, NVNshaderStage arg1,
                                         int arg2, NVNseparateSamplerHandle arg3) {
    nvn_command_buffer_rec_bind_separate_sampler(commandBuffer, arg1, arg2, arg3.value);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindSeparateSamplers(NVNcommandBuffer *commandBuffer, NVNshaderStage arg1,
                                          int arg2, int arg3,
                                          const NVNseparateSamplerHandle *arg4) {
    nvn_command_buffer_rec_bind_separate_samplers(commandBuffer, arg1, arg2, arg3, arg4);
    NVN_VOID_RETURN();
}

/* The signature of nvnCommandBufferBindSeparateSamplersDeferred is not in the header this runtime
 * was built against, so the arguments are taken as plain words and written down as they came.
 * Nothing is dereferenced, so a signature with fewer or more arguments cannot do any harm here. The
 * call is recorded in the diagnostic channel only: the format of the game this port runs has no
 * record for a command whose shape is not known. */
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
    nvn_command_buffer_record_diagnostic(commandBuffer, "nvnCommandBufferBindSeparateSamplersDeferred", args, 8);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindSeparateTexture(NVNcommandBuffer *commandBuffer, NVNshaderStage arg1,
                                         int arg2, NVNseparateTextureHandle arg3) {
    nvn_command_buffer_rec_bind_separate_texture(commandBuffer, arg1, arg2, arg3.value);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindSeparateTextures(NVNcommandBuffer *commandBuffer, NVNshaderStage arg1,
                                          int arg2, int arg3,
                                          const NVNseparateTextureHandle *arg4) {
    nvn_command_buffer_rec_bind_separate_textures(commandBuffer, arg1, arg2, arg3, arg4);
    NVN_VOID_RETURN();
}

/* The signature of nvnCommandBufferBindSeparateTexturesDeferred is not in the header this runtime
 * was built against, so the arguments are taken as plain words and written down as they came.
 * Nothing is dereferenced, so a signature with fewer or more arguments cannot do any harm here. The
 * call is recorded in the diagnostic channel only: the format of the game this port runs has no
 * record for a command whose shape is not known. */
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
    nvn_command_buffer_record_diagnostic(commandBuffer, "nvnCommandBufferBindSeparateTexturesDeferred", args, 8);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindStorageBuffer(NVNcommandBuffer *commandBuffer, NVNshaderStage arg1,
                                       int arg2, NVNbufferAddress arg3, size_t arg4) {
    nvn_command_buffer_rec_bind_storage_buffer(commandBuffer, arg1, arg2, arg3, arg4);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindStorageBuffers(NVNcommandBuffer *commandBuffer, NVNshaderStage arg1,
                                        int arg2, int arg3, const NVNbufferRange *arg4) {
    nvn_command_buffer_rec_bind_storage_buffers(commandBuffer, arg1, arg2, arg3, arg4, NVN_CB_SIZE_NVNbufferRange);
    NVN_VOID_RETURN();
}

/* The signature of nvnCommandBufferBindStorageBuffersDeferred is not in the header this runtime was
 * built against, so the arguments are taken as plain words and written down as they came. Nothing
 * is dereferenced, so a signature with fewer or more arguments cannot do any harm here. The call is
 * recorded in the diagnostic channel only: the format of the game this port runs has no record for
 * a command whose shape is not known. */
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
    nvn_command_buffer_record_diagnostic(commandBuffer, "nvnCommandBufferBindStorageBuffersDeferred", args, 8);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindTexture(NVNcommandBuffer *commandBuffer, NVNshaderStage arg1, int arg2,
                                 NVNtextureHandle arg3) {
    nvn_command_buffer_rec_bind_texture(commandBuffer, arg1, arg2, arg3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindTextures(NVNcommandBuffer *commandBuffer, NVNshaderStage arg1, int arg2,
                                  int arg3, const NVNtextureHandle *arg4) {
    nvn_command_buffer_rec_bind_textures(commandBuffer, arg1, arg2, arg3, arg4);
    NVN_VOID_RETURN();
}

/* The signature of nvnCommandBufferBindTexturesDeferred is not in the header this runtime was built
 * against, so the arguments are taken as plain words and written down as they came. Nothing is
 * dereferenced, so a signature with fewer or more arguments cannot do any harm here. The call is
 * recorded in the diagnostic channel only: the format of the game this port runs has no record for
 * a command whose shape is not known. */
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
    nvn_command_buffer_record_diagnostic(commandBuffer, "nvnCommandBufferBindTexturesDeferred", args, 8);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindTransformFeedbackBuffer(NVNcommandBuffer *commandBuffer, int arg1,
                                                 NVNbufferAddress arg2, size_t arg3) {
    nvn_command_buffer_rec_bind_transform_feedback_buffer(commandBuffer, arg1, arg2, arg3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindTransformFeedbackBuffers(NVNcommandBuffer *commandBuffer, int arg1,
                                                  int arg2, const NVNbufferRange *arg3) {
    nvn_command_buffer_rec_bind_transform_feedback_buffers(commandBuffer, arg1, arg2, arg3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindUniformBuffer(NVNcommandBuffer *commandBuffer, NVNshaderStage arg1,
                                       int arg2, NVNbufferAddress arg3, size_t arg4) {
    nvn_command_buffer_rec_bind_uniform_buffer(commandBuffer, arg1, arg2, arg3, arg4);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindUniformBuffers(NVNcommandBuffer *commandBuffer, NVNshaderStage arg1,
                                        int arg2, int arg3, const NVNbufferRange *arg4) {
    nvn_command_buffer_rec_bind_uniform_buffers(commandBuffer, arg1, arg2, arg3, arg4, NVN_CB_SIZE_NVNbufferRange);
    NVN_VOID_RETURN();
}

/* The signature of nvnCommandBufferBindUniformBuffersDeferred is not in the header this runtime was
 * built against, so the arguments are taken as plain words and written down as they came. Nothing
 * is dereferenced, so a signature with fewer or more arguments cannot do any harm here. The call is
 * recorded in the diagnostic channel only: the format of the game this port runs has no record for
 * a command whose shape is not known. */
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
    nvn_command_buffer_record_diagnostic(commandBuffer, "nvnCommandBufferBindUniformBuffersDeferred", args, 8);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindVertexAttribState(NVNcommandBuffer *commandBuffer, int arg1,
                                           const NVNvertexAttribState *arg2) {
    nvn_command_buffer_rec_bind_vertex_attrib_state(commandBuffer, arg1, arg2, NVN_CB_SIZE_NVNvertexAttribState);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindVertexBuffer(NVNcommandBuffer *commandBuffer, int arg1,
                                      NVNbufferAddress arg2, size_t arg3) {
    nvn_command_buffer_rec_bind_vertex_buffer(commandBuffer, arg1, arg2, arg3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindVertexBuffers(NVNcommandBuffer *commandBuffer, int arg1, int arg2,
                                       const NVNbufferRange *arg3) {
    nvn_command_buffer_rec_bind_vertex_buffers(commandBuffer, arg1, arg2, arg3, NVN_CB_SIZE_NVNbufferRange);
    NVN_VOID_RETURN();
}

void nvnCommandBufferBindVertexStreamState(NVNcommandBuffer *commandBuffer, int arg1,
                                           const NVNvertexStreamState *arg2) {
    nvn_command_buffer_rec_bind_vertex_stream_state(commandBuffer, arg1, arg2, NVN_CB_SIZE_NVNvertexStreamState);
    NVN_VOID_RETURN();
}

void nvnCommandBufferCallCommands(NVNcommandBuffer *commandBuffer, int arg1,
                                  const NVNcommandHandle *arg2) {
    nvn_command_buffer_rec_call_commands(commandBuffer, arg1, arg2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferClearBuffer(NVNcommandBuffer *commandBuffer, NVNbufferAddress arg1,
                                 size_t arg2, uint32_t arg3) {
    nvn_command_buffer_rec_clear_buffer(commandBuffer, arg1, arg2, arg3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferClearColor(NVNcommandBuffer *commandBuffer, int arg1, const float *arg2,
                                int arg3) {
    nvn_command_buffer_rec_clear_color(commandBuffer, arg1, arg2, arg3, 0);
    NVN_VOID_RETURN();
}

void nvnCommandBufferClearColori(NVNcommandBuffer *commandBuffer, int arg1, const int *arg2,
                                 int arg3) {
    nvn_command_buffer_rec_clear_color(commandBuffer, arg1, arg2, arg3, 1);
    NVN_VOID_RETURN();
}

void nvnCommandBufferClearColorui(NVNcommandBuffer *commandBuffer, int arg1, const uint32_t *arg2,
                                  int arg3) {
    nvn_command_buffer_rec_clear_color(commandBuffer, arg1, arg2, arg3, 1);
    NVN_VOID_RETURN();
}

void nvnCommandBufferClearDepthStencil(NVNcommandBuffer *commandBuffer, float arg1, NVNboolean arg2,
                                       int arg3, int arg4) {
    nvn_command_buffer_rec_clear_depth_stencil(commandBuffer, arg1, arg2, arg3, arg4);
    NVN_VOID_RETURN();
}

void nvnCommandBufferClearTexture(NVNcommandBuffer *commandBuffer, const NVNtexture *arg1,
                                  const NVNtextureView *arg2, const NVNcopyRegion *arg3,
                                  const float *arg4, int arg5) {
    nvn_command_buffer_rec_clear_texture(commandBuffer, arg1, arg2, arg3, arg4, arg5, 0);
    NVN_VOID_RETURN();
}

void nvnCommandBufferClearTexturei(NVNcommandBuffer *commandBuffer, const NVNtexture *arg1,
                                   const NVNtextureView *arg2, const NVNcopyRegion *arg3,
                                   const int *arg4, int arg5) {
    nvn_command_buffer_rec_clear_texture(commandBuffer, arg1, arg2, arg3, arg4, arg5, 1);
    NVN_VOID_RETURN();
}

void nvnCommandBufferClearTextureui(NVNcommandBuffer *commandBuffer, const NVNtexture *arg1,
                                    const NVNtextureView *arg2, const NVNcopyRegion *arg3,
                                    const uint32_t *arg4, int arg5) {
    nvn_command_buffer_rec_clear_texture(commandBuffer, arg1, arg2, arg3, arg4, arg5, 1);
    NVN_VOID_RETURN();
}

void nvnCommandBufferCopyBufferToBuffer(NVNcommandBuffer *commandBuffer, NVNbufferAddress arg1,
                                        NVNbufferAddress arg2, size_t arg3, int arg4) {
    (void)arg4;
    nvn_command_buffer_rec_copy_buffer_to_buffer(commandBuffer, arg1, arg2, arg3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferCopyBufferToTexture(NVNcommandBuffer *commandBuffer, NVNbufferAddress arg1,
                                         const NVNtexture *arg2, const NVNtextureView *arg3,
                                         const NVNcopyRegion *arg4, int arg5) {
    nvn_command_buffer_rec_copy_buffer_to_texture(commandBuffer, arg1, arg2, arg3, arg4, arg5);
    NVN_VOID_RETURN();
}

void nvnCommandBufferCopyCommands(NVNcommandBuffer *commandBuffer, int arg1,
                                  const NVNcommandHandle *arg2) {
    nvn_command_buffer_rec_call_commands(commandBuffer, arg1, arg2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferCopyTextureToBuffer(NVNcommandBuffer *commandBuffer, const NVNtexture *arg1,
                                         const NVNtextureView *arg2, const NVNcopyRegion *arg3,
                                         NVNbufferAddress arg4, int arg5) {
    nvn_command_buffer_rec_copy_texture_to_buffer(commandBuffer, arg1, arg2, arg3, arg4, arg5);
    NVN_VOID_RETURN();
}

void nvnCommandBufferCopyTextureToTexture(NVNcommandBuffer *commandBuffer, const NVNtexture *arg1,
                                          const NVNtextureView *arg2, const NVNcopyRegion *arg3,
                                          const NVNtexture *arg4, const NVNtextureView *arg5,
                                          const NVNcopyRegion *arg6, int arg7) {
    nvn_command_buffer_rec_copy_texture_to_texture(commandBuffer, arg1, arg2, arg3, arg4, arg5, arg6, arg7);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for nvnCommandBufferDiscardColor, so the call
 * is not recorded in the command stream. It is written down in the diagnostic channel with the
 * arguments as they came; a pointer whose size is not known is marked as one that was not followed. */
void nvnCommandBufferDiscardColor(NVNcommandBuffer *commandBuffer, int arg1) {
    uint64_t args[2];
    uint8_t unresolved[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    unresolved[0] = 0;
    unresolved[1] = 0;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferDiscardColor", args, 2, unresolved);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for nvnCommandBufferDiscardDepthStencil, so
 * the call is not recorded in the command stream. It is written down in the diagnostic channel with
 * the arguments as they came; a pointer whose size is not known is marked as one that was not
 * followed. */
void nvnCommandBufferDiscardDepthStencil(NVNcommandBuffer *commandBuffer) {
    uint64_t args[1];
    uint8_t unresolved[1];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    unresolved[0] = 0;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferDiscardDepthStencil", args, 1, unresolved);
    NVN_VOID_RETURN();
}

void nvnCommandBufferDispatchCompute(NVNcommandBuffer *commandBuffer, int arg1, int arg2,
                                     int arg3) {
    nvn_command_buffer_rec_dispatch(commandBuffer, arg1, arg2, arg3, 0);
    NVN_VOID_RETURN();
}

void nvnCommandBufferDispatchComputeIndirect(NVNcommandBuffer *commandBuffer,
                                             NVNbufferAddress arg1) {
    nvn_command_buffer_rec_dispatch(commandBuffer, 0, 0, 0, arg1);
    NVN_VOID_RETURN();
}

void nvnCommandBufferDownsample(NVNcommandBuffer *commandBuffer, const NVNtexture *arg1,
                                const NVNtexture *arg2) {
    nvn_command_buffer_rec_copy_texture_to_texture(commandBuffer, arg1, 0, 0, arg2, 0, 0, NVN_CB_COPY_RESOLVE);
    NVN_VOID_RETURN();
}

void nvnCommandBufferDownsampleTextureView(NVNcommandBuffer *commandBuffer, const NVNtexture *arg1,
                                           const NVNtextureView *arg2, const NVNtexture *arg3,
                                           const NVNtextureView *arg4) {
    nvn_command_buffer_rec_copy_texture_to_texture(commandBuffer, arg1, arg2, 0, arg3, arg4, 0, NVN_CB_COPY_RESOLVE);
    NVN_VOID_RETURN();
}

void nvnCommandBufferDrawArrays(NVNcommandBuffer *commandBuffer, NVNdrawPrimitive arg1, int arg2,
                                int arg3) {
    nvn_command_buffer_rec_draw(commandBuffer, arg1, arg2, arg3, 1, 0, 0);
    NVN_VOID_RETURN();
}

void nvnCommandBufferDrawArraysIndirect(NVNcommandBuffer *commandBuffer, NVNdrawPrimitive arg1,
                                        NVNbufferAddress arg2) {
    nvn_command_buffer_rec_draw(commandBuffer, arg1, 0, 0, 1, 0, arg2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferDrawArraysInstanced(NVNcommandBuffer *commandBuffer, NVNdrawPrimitive arg1,
                                         int arg2, int arg3, int arg4, int arg5) {
    nvn_command_buffer_rec_draw(commandBuffer, arg1, arg2, arg3, arg4, arg5, 0);
    NVN_VOID_RETURN();
}

void nvnCommandBufferDrawElements(NVNcommandBuffer *commandBuffer, NVNdrawPrimitive arg1,
                                  NVNindexType arg2, int arg3, NVNbufferAddress arg4) {
    nvn_command_buffer_rec_draw_indexed(commandBuffer, arg1, arg2, arg3, 0, 0, 1, arg4, 0);
    NVN_VOID_RETURN();
}

void nvnCommandBufferDrawElementsBaseVertex(NVNcommandBuffer *commandBuffer, NVNdrawPrimitive arg1,
                                            NVNindexType arg2, int arg3, NVNbufferAddress arg4,
                                            int arg5) {
    nvn_command_buffer_rec_draw_indexed(commandBuffer, arg1, arg2, arg3, arg5, 0, 1, arg4, 0);
    NVN_VOID_RETURN();
}

void nvnCommandBufferDrawElementsIndirect(NVNcommandBuffer *commandBuffer, NVNdrawPrimitive arg1,
                                          NVNindexType arg2, NVNbufferAddress arg3,
                                          NVNbufferAddress arg4) {
    nvn_command_buffer_rec_draw_indexed(commandBuffer, arg1, arg2, 0, 0, 0, 1, arg3, arg4);
    NVN_VOID_RETURN();
}

void nvnCommandBufferDrawElementsInstanced(NVNcommandBuffer *commandBuffer, NVNdrawPrimitive arg1,
                                           NVNindexType arg2, int arg3, NVNbufferAddress arg4,
                                           int arg5, int arg6, int arg7) {
    nvn_command_buffer_rec_draw_indexed(commandBuffer, arg1, arg2, arg3, arg5, arg6, arg7, arg4, 0);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for nvnCommandBufferDrawTexture, so the call
 * is not recorded in the command stream. It is written down in the diagnostic channel with the
 * arguments as they came; a pointer whose size is not known is marked as one that was not followed. */
void nvnCommandBufferDrawTexture(NVNcommandBuffer *commandBuffer, NVNtextureHandle arg1,
                                 const NVNdrawTextureRegion *arg2,
                                 const NVNdrawTextureRegion *arg3) {
    uint64_t args[4];
    uint8_t unresolved[4];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)arg1;
    args[2] = (uint64_t)(uintptr_t)arg2;
    args[3] = (uint64_t)(uintptr_t)arg3;
    unresolved[0] = 0;
    unresolved[1] = 0;
    unresolved[2] = 1;
    unresolved[3] = 1;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferDrawTexture", args, 4, unresolved);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for nvnCommandBufferDrawTransformFeedback, so
 * the call is not recorded in the command stream. It is written down in the diagnostic channel with
 * the arguments as they came; a pointer whose size is not known is marked as one that was not
 * followed. */
void nvnCommandBufferDrawTransformFeedback(NVNcommandBuffer *commandBuffer, NVNdrawPrimitive arg1,
                                           NVNbufferAddress arg2) {
    uint64_t args[3];
    uint8_t unresolved[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)arg2;
    unresolved[0] = 0;
    unresolved[1] = 0;
    unresolved[2] = 0;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferDrawTransformFeedback", args, 3, unresolved);
    NVN_VOID_RETURN();
}

NVNcommandHandle nvnCommandBufferEndRecording(NVNcommandBuffer *commandBuffer) {
    return (NVNcommandHandle)nvn_command_buffer_end_recording(commandBuffer);
}

void nvnCommandBufferEndTransformFeedback(NVNcommandBuffer *commandBuffer, NVNbufferAddress arg1) {
    nvn_command_buffer_rec_transform_feedback_state(commandBuffer, 1, arg1);
    NVN_VOID_RETURN();
}

void nvnCommandBufferFenceSync(NVNcommandBuffer *commandBuffer, NVNsync *arg1,
                               NVNsyncCondition arg2, int arg3) {
    (void)arg3;
    (void)arg2;
    nvn_command_buffer_rec_fence_sync(commandBuffer, arg1);
    NVN_VOID_RETURN();
}

void nvnCommandBufferFinalize(NVNcommandBuffer *commandBuffer) {
    nvn_command_buffer_finalize(commandBuffer);
    NVN_VOID_RETURN();
}

size_t nvnCommandBufferGetCommandMemoryFree(const NVNcommandBuffer *commandBuffer) {
    return (size_t)nvn_command_buffer_get_command_memory_free(commandBuffer);
}

size_t nvnCommandBufferGetCommandMemorySize(const NVNcommandBuffer *commandBuffer) {
    return (size_t)nvn_command_buffer_get_command_memory_size(commandBuffer);
}

size_t nvnCommandBufferGetCommandMemoryUsed(const NVNcommandBuffer *commandBuffer) {
    return (size_t)nvn_command_buffer_get_command_memory_used(commandBuffer);
}

size_t nvnCommandBufferGetControlMemoryFree(const NVNcommandBuffer *commandBuffer) {
    return (size_t)nvn_command_buffer_get_control_memory_free(commandBuffer);
}

size_t nvnCommandBufferGetControlMemorySize(const NVNcommandBuffer *commandBuffer) {
    return (size_t)nvn_command_buffer_get_control_memory_size(commandBuffer);
}

size_t nvnCommandBufferGetControlMemoryUsed(const NVNcommandBuffer *commandBuffer) {
    nvn_command_buffer_report_used_is_unknown(commandBuffer, "control memory");
    return 0;
}

ptrdiff_t nvnCommandBufferGetCopyImageStride(const NVNcommandBuffer *commandBuffer) {
    return (ptrdiff_t)nvn_command_buffer_get_copy_image_stride(commandBuffer);
}

ptrdiff_t nvnCommandBufferGetCopyRowStride(const NVNcommandBuffer *commandBuffer) {
    return (ptrdiff_t)nvn_command_buffer_get_copy_row_stride(commandBuffer);
}

PFNNVNCOMMANDBUFFERMEMORYCALLBACKPROC
nvnCommandBufferGetMemoryCallback(const NVNcommandBuffer *commandBuffer) {
    return (PFNNVNCOMMANDBUFFERMEMORYCALLBACKPROC)
        (uintptr_t)nvn_command_buffer_get_memory_callback(commandBuffer);
}

void nvnCommandBufferGetMemoryCallbackData(const NVNcommandBuffer *commandBuffer) {
    /* The header this runtime was built against declares this entry point as
     * returning nothing, but the value it names is the callback data the guest
     * set, and the guest reads it from the result register. The value goes
     * there directly, because a C function that returns nothing cannot hand
     * it over otherwise. */
    nvn_command_buffer_set_result(
        nvn_command_buffer_get_memory_callback_data(commandBuffer));
}

NVNboolean nvnCommandBufferInitialize(NVNcommandBuffer *commandBuffer, NVNdevice *arg1) {
    (void)arg1;
    int initialized = nvn_command_buffer_initialize(commandBuffer);
    return (NVNboolean)(initialized ? 1 : 0);
}

/* The signature of nvnCommandBufferInsertCheckpointDynamic is not in the header this runtime was
 * built against, so the arguments are taken as plain words and written down as they came. Nothing
 * is dereferenced, so a signature with fewer or more arguments cannot do any harm here. The call is
 * recorded in the diagnostic channel only: the format of the game this port runs has no record for
 * a command whose shape is not known. */
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
    nvn_command_buffer_record_diagnostic(commandBuffer, "nvnCommandBufferInsertCheckpointDynamic", args, 8);
    NVN_VOID_RETURN();
}

/* The signature of nvnCommandBufferInsertCheckpointStatic is not in the header this runtime was
 * built against, so the arguments are taken as plain words and written down as they came. Nothing
 * is dereferenced, so a signature with fewer or more arguments cannot do any harm here. The call is
 * recorded in the diagnostic channel only: the format of the game this port runs has no record for
 * a command whose shape is not known. */
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
    nvn_command_buffer_record_diagnostic(commandBuffer, "nvnCommandBufferInsertCheckpointStatic", args, 8);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for nvnCommandBufferInsertDebugMarker, so the
 * call is not recorded in the command stream. It is written down in the diagnostic channel with the
 * arguments as they came; a pointer whose size is not known is marked as one that was not followed. */
void nvnCommandBufferInsertDebugMarker(NVNcommandBuffer *commandBuffer, const char *arg1) {
    uint64_t args[2];
    uint8_t unresolved[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    unresolved[0] = 0;
    unresolved[1] = 1;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferInsertDebugMarker", args, 2, unresolved);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for nvnCommandBufferInsertDebugMarkerDynamic,
 * so the call is not recorded in the command stream. It is written down in the diagnostic channel
 * with the arguments as they came; a pointer whose size is not known is marked as one that was not
 * followed. */
void nvnCommandBufferInsertDebugMarkerDynamic(NVNcommandBuffer *commandBuffer, uint32_t arg1,
                                              const char *arg2) {
    uint64_t args[3];
    uint8_t unresolved[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uintptr_t)arg2;
    unresolved[0] = 0;
    unresolved[1] = 0;
    unresolved[2] = 1;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferInsertDebugMarkerDynamic", args, 3, unresolved);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for nvnCommandBufferInsertDebugMarkerStatic,
 * so the call is not recorded in the command stream. It is written down in the diagnostic channel
 * with the arguments as they came; a pointer whose size is not known is marked as one that was not
 * followed. */
void nvnCommandBufferInsertDebugMarkerStatic(NVNcommandBuffer *commandBuffer, uint32_t arg1,
                                             const char *arg2) {
    uint64_t args[3];
    uint8_t unresolved[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uintptr_t)arg2;
    unresolved[0] = 0;
    unresolved[1] = 0;
    unresolved[2] = 1;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferInsertDebugMarkerStatic", args, 3, unresolved);
    NVN_VOID_RETURN();
}

NVNboolean nvnCommandBufferIsRecording(const NVNcommandBuffer *commandBuffer) {
    return (NVNboolean)(nvn_command_buffer_is_recording(commandBuffer) ? 1 : 0);
}

/* The format of the game this port runs has no record for
 * nvnCommandBufferMultiDrawArraysIndirectCount, so the call is not recorded in the command stream.
 * It is written down in the diagnostic channel with the arguments as they came; a pointer whose
 * size is not known is marked as one that was not followed. */
void nvnCommandBufferMultiDrawArraysIndirectCount(NVNcommandBuffer *commandBuffer,
                                                  NVNdrawPrimitive arg1, NVNbufferAddress arg2,
                                                  NVNbufferAddress arg3, int arg4, ptrdiff_t arg5) {
    uint64_t args[6];
    uint8_t unresolved[6];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)arg2;
    args[3] = (uint64_t)arg3;
    args[4] = (uint64_t)(uint32_t)arg4;
    args[5] = (uint64_t)arg5;
    unresolved[0] = 0;
    unresolved[1] = 0;
    unresolved[2] = 0;
    unresolved[3] = 0;
    unresolved[4] = 0;
    unresolved[5] = 0;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferMultiDrawArraysIndirectCount", args, 6, unresolved);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for
 * nvnCommandBufferMultiDrawElementsIndirectCount, so the call is not recorded in the command
 * stream. It is written down in the diagnostic channel with the arguments as they came; a pointer
 * whose size is not known is marked as one that was not followed. */
void nvnCommandBufferMultiDrawElementsIndirectCount(NVNcommandBuffer *commandBuffer,
                                                    NVNdrawPrimitive arg1, NVNindexType arg2,
                                                    NVNbufferAddress arg3, NVNbufferAddress arg4,
                                                    NVNbufferAddress arg5, int arg6,
                                                    ptrdiff_t arg7) {
    uint64_t args[8];
    uint8_t unresolved[8];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)arg3;
    args[4] = (uint64_t)arg4;
    args[5] = (uint64_t)arg5;
    args[6] = (uint64_t)(uint32_t)arg6;
    args[7] = (uint64_t)arg7;
    unresolved[0] = 0;
    unresolved[1] = 0;
    unresolved[2] = 0;
    unresolved[3] = 0;
    unresolved[4] = 0;
    unresolved[5] = 0;
    unresolved[6] = 0;
    unresolved[7] = 0;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferMultiDrawElementsIndirectCount", args, 8, unresolved);
    NVN_VOID_RETURN();
}

/* The signature of nvnCommandBufferOverrideSubtileSize is not in the header this runtime was built
 * against, so the arguments are taken as plain words and written down as they came. Nothing is
 * dereferenced, so a signature with fewer or more arguments cannot do any harm here. The call is
 * recorded in the diagnostic channel only: the format of the game this port runs has no record for
 * a command whose shape is not known. */
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
    nvn_command_buffer_record_diagnostic(commandBuffer, "nvnCommandBufferOverrideSubtileSize", args, 8);
    NVN_VOID_RETURN();
}

void nvnCommandBufferPauseTransformFeedback(NVNcommandBuffer *commandBuffer,
                                            NVNbufferAddress arg1) {
    nvn_command_buffer_rec_transform_feedback_state(commandBuffer, 2, arg1);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for nvnCommandBufferPopDebugGroup, so the
 * call is not recorded in the command stream. It is written down in the diagnostic channel with the
 * arguments as they came; a pointer whose size is not known is marked as one that was not followed. */
void nvnCommandBufferPopDebugGroup(NVNcommandBuffer *commandBuffer) {
    uint64_t args[1];
    uint8_t unresolved[1];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    unresolved[0] = 0;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferPopDebugGroup", args, 1, unresolved);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for nvnCommandBufferPopDebugGroupId, so the
 * call is not recorded in the command stream. It is written down in the diagnostic channel with the
 * arguments as they came; a pointer whose size is not known is marked as one that was not followed. */
void nvnCommandBufferPopDebugGroupId(NVNcommandBuffer *commandBuffer, uint32_t arg1) {
    uint64_t args[2];
    uint8_t unresolved[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    unresolved[0] = 0;
    unresolved[1] = 0;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferPopDebugGroupId", args, 2, unresolved);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for nvnCommandBufferPushDebugGroup, so the
 * call is not recorded in the command stream. It is written down in the diagnostic channel with the
 * arguments as they came; a pointer whose size is not known is marked as one that was not followed. */
void nvnCommandBufferPushDebugGroup(NVNcommandBuffer *commandBuffer, uint32_t arg1,
                                    const char *arg2) {
    uint64_t args[3];
    uint8_t unresolved[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uintptr_t)arg2;
    unresolved[0] = 0;
    unresolved[1] = 0;
    unresolved[2] = 1;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferPushDebugGroup", args, 3, unresolved);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for nvnCommandBufferPushDebugGroupDynamic, so
 * the call is not recorded in the command stream. It is written down in the diagnostic channel with
 * the arguments as they came; a pointer whose size is not known is marked as one that was not
 * followed. */
void nvnCommandBufferPushDebugGroupDynamic(NVNcommandBuffer *commandBuffer, uint32_t arg1,
                                           const char *arg2) {
    uint64_t args[3];
    uint8_t unresolved[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uintptr_t)arg2;
    unresolved[0] = 0;
    unresolved[1] = 0;
    unresolved[2] = 1;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferPushDebugGroupDynamic", args, 3, unresolved);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for nvnCommandBufferPushDebugGroupStatic, so
 * the call is not recorded in the command stream. It is written down in the diagnostic channel with
 * the arguments as they came; a pointer whose size is not known is marked as one that was not
 * followed. */
void nvnCommandBufferPushDebugGroupStatic(NVNcommandBuffer *commandBuffer, uint32_t arg1,
                                          const char *arg2) {
    uint64_t args[3];
    uint8_t unresolved[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uintptr_t)arg2;
    unresolved[0] = 0;
    unresolved[1] = 0;
    unresolved[2] = 1;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferPushDebugGroupStatic", args, 3, unresolved);
    NVN_VOID_RETURN();
}

void nvnCommandBufferReportCounter(NVNcommandBuffer *commandBuffer, NVNcounterType arg1,
                                   NVNbufferAddress arg2) {
    nvn_command_buffer_rec_report_counter(commandBuffer, arg1, arg2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferReportValue(NVNcommandBuffer *commandBuffer, uint32_t arg1,
                                 NVNbufferAddress arg2) {
    nvn_command_buffer_rec_report_value(commandBuffer, arg1, arg2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferResetCounter(NVNcommandBuffer *commandBuffer, NVNcounterType arg1) {
    nvn_command_buffer_rec_reset_counter(commandBuffer, arg1);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for nvnCommandBufferResolveDepthBuffer, so
 * the call is not recorded in the command stream. It is written down in the diagnostic channel with
 * the arguments as they came; a pointer whose size is not known is marked as one that was not
 * followed. */
void nvnCommandBufferResolveDepthBuffer(NVNcommandBuffer *commandBuffer) {
    uint64_t args[1];
    uint8_t unresolved[1];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    unresolved[0] = 0;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferResolveDepthBuffer", args, 1, unresolved);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for nvnCommandBufferRestoreZCullData, so the
 * call is not recorded in the command stream. It is written down in the diagnostic channel with the
 * arguments as they came; a pointer whose size is not known is marked as one that was not followed. */
void nvnCommandBufferRestoreZCullData(NVNcommandBuffer *commandBuffer, NVNbufferAddress arg1,
                                      size_t arg2) {
    uint64_t args[3];
    uint8_t unresolved[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)arg1;
    args[2] = (uint64_t)arg2;
    unresolved[0] = 0;
    unresolved[1] = 0;
    unresolved[2] = 0;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferRestoreZCullData", args, 3, unresolved);
    NVN_VOID_RETURN();
}

void nvnCommandBufferResumeTransformFeedback(NVNcommandBuffer *commandBuffer,
                                             NVNbufferAddress arg1) {
    nvn_command_buffer_rec_transform_feedback_state(commandBuffer, 3, arg1);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for nvnCommandBufferSaveZCullData, so the
 * call is not recorded in the command stream. It is written down in the diagnostic channel with the
 * arguments as they came; a pointer whose size is not known is marked as one that was not followed. */
void nvnCommandBufferSaveZCullData(NVNcommandBuffer *commandBuffer, NVNbufferAddress arg1,
                                   size_t arg2) {
    uint64_t args[3];
    uint8_t unresolved[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)arg1;
    args[2] = (uint64_t)arg2;
    unresolved[0] = 0;
    unresolved[1] = 0;
    unresolved[2] = 0;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferSaveZCullData", args, 3, unresolved);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for nvnCommandBufferSetAlphaRef, so the call
 * is not recorded in the command stream. It is written down in the diagnostic channel with the
 * arguments as they came; a pointer whose size is not known is marked as one that was not followed. */
void nvnCommandBufferSetAlphaRef(NVNcommandBuffer *commandBuffer, float arg1) {
    uint64_t args[2];
    uint8_t unresolved[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = nvn_command_buffer_float_bits(arg1);
    unresolved[0] = 0;
    unresolved[1] = 0;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferSetAlphaRef", args, 2, unresolved);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetBlendColor(NVNcommandBuffer *commandBuffer, const float *arg1) {
    nvn_command_buffer_rec_set_blend_color(commandBuffer, arg1);
    NVN_VOID_RETURN();
}

/* The signature of nvnCommandBufferSetColorReductionEnable is not in the header this runtime was
 * built against, so the arguments are taken as plain words and written down as they came. Nothing
 * is dereferenced, so a signature with fewer or more arguments cannot do any harm here. The call is
 * recorded in the diagnostic channel only: the format of the game this port runs has no record for
 * a command whose shape is not known. */
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
    nvn_command_buffer_record_diagnostic(commandBuffer, "nvnCommandBufferSetColorReductionEnable", args, 8);
    NVN_VOID_RETURN();
}

/* The signature of nvnCommandBufferSetColorReductionThresholds is not in the header this runtime
 * was built against, so the arguments are taken as plain words and written down as they came.
 * Nothing is dereferenced, so a signature with fewer or more arguments cannot do any harm here. The
 * call is recorded in the diagnostic channel only: the format of the game this port runs has no
 * record for a command whose shape is not known. */
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
    nvn_command_buffer_record_diagnostic(commandBuffer, "nvnCommandBufferSetColorReductionThresholds", args, 8);
    NVN_VOID_RETURN();
}

/* The signature of nvnCommandBufferSetCommandMemoryCallbackEnabled is not in the header this
 * runtime was built against, so the arguments are taken as plain words and written down as they
 * came. Nothing is dereferenced, so a signature with fewer or more arguments cannot do any harm
 * here. The call is recorded in the diagnostic channel only: the format of the game this port runs
 * has no record for a command whose shape is not known. */
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
    nvn_command_buffer_record_diagnostic(commandBuffer, "nvnCommandBufferSetCommandMemoryCallbackEnabled", args, 8);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for
 * nvnCommandBufferSetConservativeRasterDilate, so the call is not recorded in the command stream.
 * It is written down in the diagnostic channel with the arguments as they came; a pointer whose
 * size is not known is marked as one that was not followed. */
void nvnCommandBufferSetConservativeRasterDilate(NVNcommandBuffer *commandBuffer, float arg1) {
    uint64_t args[2];
    uint8_t unresolved[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = nvn_command_buffer_float_bits(arg1);
    unresolved[0] = 0;
    unresolved[1] = 0;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferSetConservativeRasterDilate", args, 2, unresolved);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for
 * nvnCommandBufferSetConservativeRasterEnable, so the call is not recorded in the command stream.
 * It is written down in the diagnostic channel with the arguments as they came; a pointer whose
 * size is not known is marked as one that was not followed. */
void nvnCommandBufferSetConservativeRasterEnable(NVNcommandBuffer *commandBuffer, NVNboolean arg1) {
    uint64_t args[2];
    uint8_t unresolved[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint8_t)arg1;
    unresolved[0] = 0;
    unresolved[1] = 0;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferSetConservativeRasterEnable", args, 2, unresolved);
    NVN_VOID_RETURN();
}

/* The signature of nvnCommandBufferSetControlMemoryCallbackEnabled is not in the header this
 * runtime was built against, so the arguments are taken as plain words and written down as they
 * came. Nothing is dereferenced, so a signature with fewer or more arguments cannot do any harm
 * here. The call is recorded in the diagnostic channel only: the format of the game this port runs
 * has no record for a command whose shape is not known. */
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
    nvn_command_buffer_record_diagnostic(commandBuffer, "nvnCommandBufferSetControlMemoryCallbackEnabled", args, 8);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetCopyImageStride(NVNcommandBuffer *commandBuffer, ptrdiff_t arg1) {
    nvn_command_buffer_set_copy_image_stride(commandBuffer, (int64_t)arg1);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetCopyRowStride(NVNcommandBuffer *commandBuffer, ptrdiff_t arg1) {
    nvn_command_buffer_set_copy_row_stride(commandBuffer, (int64_t)arg1);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for nvnCommandBufferSetDebugLabel, so the
 * call is not recorded in the command stream. It is written down in the diagnostic channel with the
 * arguments as they came; a pointer whose size is not known is marked as one that was not followed. */
void nvnCommandBufferSetDebugLabel(NVNcommandBuffer *commandBuffer, const char *arg1) {
    uint64_t args[2];
    uint8_t unresolved[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    unresolved[0] = 0;
    unresolved[1] = 1;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferSetDebugLabel", args, 2, unresolved);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for nvnCommandBufferSetDepthBounds, so the
 * call is not recorded in the command stream. It is written down in the diagnostic channel with the
 * arguments as they came; a pointer whose size is not known is marked as one that was not followed. */
void nvnCommandBufferSetDepthBounds(NVNcommandBuffer *commandBuffer, NVNboolean arg1, float arg2,
                                    float arg3) {
    uint64_t args[4];
    uint8_t unresolved[4];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint8_t)arg1;
    args[2] = nvn_command_buffer_float_bits(arg2);
    args[3] = nvn_command_buffer_float_bits(arg3);
    unresolved[0] = 0;
    unresolved[1] = 0;
    unresolved[2] = 0;
    unresolved[3] = 0;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferSetDepthBounds", args, 4, unresolved);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetDepthClamp(NVNcommandBuffer *commandBuffer, NVNboolean arg1) {
    nvn_command_buffer_rec_set_depth_clamp(commandBuffer, arg1);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetDepthRange(NVNcommandBuffer *commandBuffer, float arg1, float arg2) {
    nvn_command_buffer_rec_set_depth_range(commandBuffer, arg1, arg2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetDepthRanges(NVNcommandBuffer *commandBuffer, int arg1, int arg2,
                                    const float *arg3) {
    nvn_command_buffer_rec_set_depth_ranges(commandBuffer, arg1, arg2, arg3);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for
 * nvnCommandBufferSetInnerTessellationLevels, so the call is not recorded in the command stream. It
 * is written down in the diagnostic channel with the arguments as they came; a pointer whose size
 * is not known is marked as one that was not followed. */
void nvnCommandBufferSetInnerTessellationLevels(NVNcommandBuffer *commandBuffer,
                                                const float *arg1) {
    uint64_t args[2];
    uint8_t unresolved[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    unresolved[0] = 0;
    unresolved[1] = 1;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferSetInnerTessellationLevels", args, 2, unresolved);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for nvnCommandBufferSetLineWidth, so the call
 * is not recorded in the command stream. It is written down in the diagnostic channel with the
 * arguments as they came; a pointer whose size is not known is marked as one that was not followed. */
void nvnCommandBufferSetLineWidth(NVNcommandBuffer *commandBuffer, float arg1) {
    uint64_t args[2];
    uint8_t unresolved[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = nvn_command_buffer_float_bits(arg1);
    unresolved[0] = 0;
    unresolved[1] = 0;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferSetLineWidth", args, 2, unresolved);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetMemoryCallback(NVNcommandBuffer *commandBuffer,
                                       PFNNVNCOMMANDBUFFERMEMORYCALLBACKPROC arg1) {
    nvn_command_buffer_set_memory_callback(commandBuffer, (uint64_t)(uintptr_t)arg1);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetMemoryCallbackData(NVNcommandBuffer *commandBuffer, void *arg1) {
    nvn_command_buffer_set_memory_callback_data(commandBuffer, (uint64_t)(uintptr_t)arg1);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for
 * nvnCommandBufferSetOuterTessellationLevels, so the call is not recorded in the command stream. It
 * is written down in the diagnostic channel with the arguments as they came; a pointer whose size
 * is not known is marked as one that was not followed. */
void nvnCommandBufferSetOuterTessellationLevels(NVNcommandBuffer *commandBuffer,
                                                const float *arg1) {
    uint64_t args[2];
    uint8_t unresolved[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    unresolved[0] = 0;
    unresolved[1] = 1;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferSetOuterTessellationLevels", args, 2, unresolved);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for nvnCommandBufferSetPatchSize, so the call
 * is not recorded in the command stream. It is written down in the diagnostic channel with the
 * arguments as they came; a pointer whose size is not known is marked as one that was not followed. */
void nvnCommandBufferSetPatchSize(NVNcommandBuffer *commandBuffer, int arg1) {
    uint64_t args[2];
    uint8_t unresolved[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    unresolved[0] = 0;
    unresolved[1] = 0;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferSetPatchSize", args, 2, unresolved);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for nvnCommandBufferSetPointSize, so the call
 * is not recorded in the command stream. It is written down in the diagnostic channel with the
 * arguments as they came; a pointer whose size is not known is marked as one that was not followed. */
void nvnCommandBufferSetPointSize(NVNcommandBuffer *commandBuffer, float arg1) {
    uint64_t args[2];
    uint8_t unresolved[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = nvn_command_buffer_float_bits(arg1);
    unresolved[0] = 0;
    unresolved[1] = 0;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferSetPointSize", args, 2, unresolved);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetPolygonOffsetClamp(NVNcommandBuffer *commandBuffer, float arg1, float arg2,
                                           float arg3) {
    nvn_command_buffer_rec_set_polygon_offset(commandBuffer, arg1, arg2, arg3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetPrimitiveRestart(NVNcommandBuffer *commandBuffer, NVNboolean arg1,
                                         int arg2) {
    nvn_command_buffer_rec_set_primitive_restart(commandBuffer, arg1, arg2);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for nvnCommandBufferSetProgramSubroutines, so
 * the call is not recorded in the command stream. It is written down in the diagnostic channel with
 * the arguments as they came; a pointer whose size is not known is marked as one that was not
 * followed. */
void nvnCommandBufferSetProgramSubroutines(NVNcommandBuffer *commandBuffer, NVNprogram *arg1,
                                           NVNshaderStage arg2, const int arg3, const int arg4,
                                           const int *arg5) {
    uint64_t args[6];
    uint8_t unresolved[6];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)(uint32_t)arg3;
    args[4] = (uint64_t)(uint32_t)arg4;
    args[5] = (uint64_t)(uintptr_t)arg5;
    unresolved[0] = 0;
    unresolved[1] = 0;
    unresolved[2] = 0;
    unresolved[3] = 0;
    unresolved[4] = 0;
    unresolved[5] = 1;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferSetProgramSubroutines", args, 6, unresolved);
    NVN_VOID_RETURN();
}

/* The signature of nvnCommandBufferSetProvokingVertexMode is not in the header this runtime was
 * built against, so the arguments are taken as plain words and written down as they came. Nothing
 * is dereferenced, so a signature with fewer or more arguments cannot do any harm here. The call is
 * recorded in the diagnostic channel only: the format of the game this port runs has no record for
 * a command whose shape is not known. */
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
    nvn_command_buffer_record_diagnostic(commandBuffer, "nvnCommandBufferSetProvokingVertexMode", args, 8);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetRasterizerDiscard(NVNcommandBuffer *commandBuffer, NVNboolean arg1) {
    nvn_command_buffer_rec_set_rasterizer_discard(commandBuffer, arg1);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetRenderEnable(NVNcommandBuffer *commandBuffer, NVNboolean arg1) {
    nvn_command_buffer_rec_set_render_enable(commandBuffer, arg1);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetRenderEnableConditional(NVNcommandBuffer *commandBuffer,
                                                NVNconditionalRenderMode arg1,
                                                NVNbufferAddress arg2) {
    nvn_command_buffer_rec_set_render_enable_conditional(commandBuffer, arg1, arg2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetRenderTargets(NVNcommandBuffer *commandBuffer, int arg1,
                                      const NVNtexture* const *arg2,
                                      const NVNtextureView* const *arg3, const NVNtexture *arg4,
                                      const NVNtextureView *arg5) {
    nvn_command_buffer_rec_set_render_targets(commandBuffer, arg1, (const void *const *)arg2, NVN_CB_SIZE_NVNtexture, (const void *const *)arg3, NVN_CB_SIZE_NVNtextureView, arg4, arg5);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for nvnCommandBufferSetSampleMask, so the
 * call is not recorded in the command stream. It is written down in the diagnostic channel with the
 * arguments as they came; a pointer whose size is not known is marked as one that was not followed. */
void nvnCommandBufferSetSampleMask(NVNcommandBuffer *commandBuffer, int arg1) {
    uint64_t args[2];
    uint8_t unresolved[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    unresolved[0] = 0;
    unresolved[1] = 0;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferSetSampleMask", args, 2, unresolved);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetSamplerPool(NVNcommandBuffer *commandBuffer, const NVNsamplerPool *arg1) {
    nvn_command_buffer_rec_set_sampler_pool(commandBuffer, arg1);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetScissor(NVNcommandBuffer *commandBuffer, int arg1, int arg2, int arg3,
                                int arg4) {
    nvn_command_buffer_rec_set_scissor(commandBuffer, arg1, arg2, arg3, arg4);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetScissors(NVNcommandBuffer *commandBuffer, int arg1, int arg2,
                                 const int *arg3) {
    nvn_command_buffer_rec_set_scissors(commandBuffer, arg1, arg2, arg3);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for nvnCommandBufferSetShaderScratchMemory,
 * so the call is not recorded in the command stream. It is written down in the diagnostic channel
 * with the arguments as they came; a pointer whose size is not known is marked as one that was not
 * followed. */
void nvnCommandBufferSetShaderScratchMemory(NVNcommandBuffer *commandBuffer,
                                            const NVNmemoryPool *arg1, ptrdiff_t arg2,
                                            size_t arg3) {
    uint64_t args[4];
    uint8_t unresolved[4];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    args[2] = (uint64_t)arg2;
    args[3] = (uint64_t)arg3;
    unresolved[0] = 0;
    unresolved[1] = 0;
    unresolved[2] = 0;
    unresolved[3] = 0;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferSetShaderScratchMemory", args, 4, unresolved);
    NVN_VOID_RETURN();
}

/* The signature of nvnCommandBufferSetStencilCullCriteria is not in the header this runtime was
 * built against, so the arguments are taken as plain words and written down as they came. Nothing
 * is dereferenced, so a signature with fewer or more arguments cannot do any harm here. The call is
 * recorded in the diagnostic channel only: the format of the game this port runs has no record for
 * a command whose shape is not known. */
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
    nvn_command_buffer_record_diagnostic(commandBuffer, "nvnCommandBufferSetStencilCullCriteria", args, 8);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetStencilMask(NVNcommandBuffer *commandBuffer, NVNface arg1, int arg2) {
    nvn_command_buffer_rec_set_stencil(commandBuffer, arg1, 0, arg2, 0, 1);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetStencilRef(NVNcommandBuffer *commandBuffer, NVNface arg1, int arg2) {
    nvn_command_buffer_rec_set_stencil(commandBuffer, arg1, 0, 0, arg2, 2);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetStencilValueMask(NVNcommandBuffer *commandBuffer, NVNface arg1, int arg2) {
    nvn_command_buffer_rec_set_stencil(commandBuffer, arg1, arg2, 0, 0, 0);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for nvnCommandBufferSetSubpixelPrecisionBias,
 * so the call is not recorded in the command stream. It is written down in the diagnostic channel
 * with the arguments as they came; a pointer whose size is not known is marked as one that was not
 * followed. */
void nvnCommandBufferSetSubpixelPrecisionBias(NVNcommandBuffer *commandBuffer, int arg1, int arg2) {
    uint64_t args[3];
    uint8_t unresolved[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    unresolved[0] = 0;
    unresolved[1] = 0;
    unresolved[2] = 0;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferSetSubpixelPrecisionBias", args, 3, unresolved);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetTexturePool(NVNcommandBuffer *commandBuffer, const NVNtexturePool *arg1) {
    nvn_command_buffer_rec_set_texture_pool(commandBuffer, arg1);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for nvnCommandBufferSetTiledCacheAction, so
 * the call is not recorded in the command stream. It is written down in the diagnostic channel with
 * the arguments as they came; a pointer whose size is not known is marked as one that was not
 * followed. */
void nvnCommandBufferSetTiledCacheAction(NVNcommandBuffer *commandBuffer,
                                         NVNtiledCacheAction arg1) {
    uint64_t args[2];
    uint8_t unresolved[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    unresolved[0] = 0;
    unresolved[1] = 0;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferSetTiledCacheAction", args, 2, unresolved);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for nvnCommandBufferSetTiledCacheTileSize, so
 * the call is not recorded in the command stream. It is written down in the diagnostic channel with
 * the arguments as they came; a pointer whose size is not known is marked as one that was not
 * followed. */
void nvnCommandBufferSetTiledCacheTileSize(NVNcommandBuffer *commandBuffer, int arg1, int arg2) {
    uint64_t args[3];
    uint8_t unresolved[3];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uint32_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    unresolved[0] = 0;
    unresolved[1] = 0;
    unresolved[2] = 0;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferSetTiledCacheTileSize", args, 3, unresolved);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetViewport(NVNcommandBuffer *commandBuffer, int arg1, int arg2, int arg3,
                                 int arg4) {
    nvn_command_buffer_rec_set_viewport(commandBuffer, arg1, arg2, arg3, arg4);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetViewportSwizzles(NVNcommandBuffer *commandBuffer, int arg1, int arg2,
                                         const NVNviewportSwizzle *arg3) {
    nvn_command_buffer_rec_set_viewport_swizzles(commandBuffer, arg1, arg2, arg3);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSetViewports(NVNcommandBuffer *commandBuffer, int arg1, int arg2,
                                  const float *arg3) {
    nvn_command_buffer_rec_set_viewports(commandBuffer, arg1, arg2, arg3);
    NVN_VOID_RETURN();
}

/* The signature of nvnCommandBufferSetZCullZF32CompressionEnable is not in the header this runtime
 * was built against, so the arguments are taken as plain words and written down as they came.
 * Nothing is dereferenced, so a signature with fewer or more arguments cannot do any harm here. The
 * call is recorded in the diagnostic channel only: the format of the game this port runs has no
 * record for a command whose shape is not known. */
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
    nvn_command_buffer_record_diagnostic(commandBuffer, "nvnCommandBufferSetZCullZF32CompressionEnable", args, 8);
    NVN_VOID_RETURN();
}

void nvnCommandBufferSignalEvent(NVNcommandBuffer *commandBuffer, const NVNevent *arg1,
                                 NVNeventSignalMode arg2, NVNeventSignalLocation arg3, int arg4,
                                 uint32_t arg5) {
    (void)arg4;
    (void)arg3;
    nvn_command_buffer_rec_signal_event(commandBuffer, arg1, arg2, arg5);
    NVN_VOID_RETURN();
}

void nvnCommandBufferTiledDownsample(NVNcommandBuffer *commandBuffer, const NVNtexture *arg1,
                                     const NVNtexture *arg2) {
    nvn_command_buffer_rec_copy_texture_to_texture(commandBuffer, arg1, 0, 0, arg2, 0, 0, NVN_CB_COPY_RESOLVE);
    NVN_VOID_RETURN();
}

void nvnCommandBufferTiledDownsampleTextureView(NVNcommandBuffer *commandBuffer,
                                                const NVNtexture *arg1, const NVNtextureView *arg2,
                                                const NVNtexture *arg3,
                                                const NVNtextureView *arg4) {
    nvn_command_buffer_rec_copy_texture_to_texture(commandBuffer, arg1, arg2, 0, arg3, arg4, 0, NVN_CB_COPY_RESOLVE);
    NVN_VOID_RETURN();
}

void nvnCommandBufferUpdateUniformBuffer(NVNcommandBuffer *commandBuffer, NVNbufferAddress arg1,
                                         size_t arg2, ptrdiff_t arg3, size_t arg4,
                                         const void *arg5) {
    (void)arg2;
    nvn_command_buffer_rec_update_uniform_buffer(commandBuffer, arg1, arg3, arg4, arg5);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for nvnCommandBufferWaitEvent, so the call is
 * not recorded in the command stream. It is written down in the diagnostic channel with the
 * arguments as they came; a pointer whose size is not known is marked as one that was not followed. */
void nvnCommandBufferWaitEvent(NVNcommandBuffer *commandBuffer, const NVNevent *arg1,
                               NVNeventWaitMode arg2, uint32_t arg3) {
    uint64_t args[4];
    uint8_t unresolved[4];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    args[2] = (uint64_t)(uint32_t)arg2;
    args[3] = (uint64_t)(uint32_t)arg3;
    unresolved[0] = 0;
    unresolved[1] = 0;
    unresolved[2] = 0;
    unresolved[3] = 0;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferWaitEvent", args, 4, unresolved);
    NVN_VOID_RETURN();
}

/* The format of the game this port runs has no record for nvnCommandBufferWaitSync, so the call is
 * not recorded in the command stream. It is written down in the diagnostic channel with the
 * arguments as they came; a pointer whose size is not known is marked as one that was not followed. */
void nvnCommandBufferWaitSync(NVNcommandBuffer *commandBuffer, const NVNsync *arg1) {
    uint64_t args[2];
    uint8_t unresolved[2];
    args[0] = (uint64_t)(uintptr_t)commandBuffer;
    args[1] = (uint64_t)(uintptr_t)arg1;
    unresolved[0] = 0;
    unresolved[1] = 0;
    nvn_command_buffer_record_unmapped(commandBuffer, "nvnCommandBufferWaitSync", args, 2, unresolved);
    NVN_VOID_RETURN();
}

/* ------------------------------------------------------------------ */
/* The names                                                           */
/* ------------------------------------------------------------------ */

static const nvn_command_buffer_family_entry s_command_buffer_family[] = {
    { "nvnCommandBufferAddCommandMemory",
      (void *)(uintptr_t)nvnCommandBufferAddCommandMemory,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferAddControlMemory",
      (void *)(uintptr_t)nvnCommandBufferAddControlMemory,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferBarrier",
      (void *)(uintptr_t)nvnCommandBufferBarrier,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferBeginRecording",
      (void *)(uintptr_t)nvnCommandBufferBeginRecording,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferBeginTransformFeedback",
      (void *)(uintptr_t)nvnCommandBufferBeginTransformFeedback,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferBindBlendState",
      (void *)(uintptr_t)nvnCommandBufferBindBlendState,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferBindChannelMaskState",
      (void *)(uintptr_t)nvnCommandBufferBindChannelMaskState,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferBindColorState",
      (void *)(uintptr_t)nvnCommandBufferBindColorState,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferBindCoverageModulationTable",
      (void *)(uintptr_t)nvnCommandBufferBindCoverageModulationTable,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferBindDepthStencilState",
      (void *)(uintptr_t)nvnCommandBufferBindDepthStencilState,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferBindImage",
      (void *)(uintptr_t)nvnCommandBufferBindImage,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferBindImages",
      (void *)(uintptr_t)nvnCommandBufferBindImages,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferBindImagesDeferred",
      (void *)(uintptr_t)nvnCommandBufferBindImagesDeferred,
      NVN_CB_ENTRY_DIAGNOSTIC_ONLY },
    { "nvnCommandBufferBindMultisampleState",
      (void *)(uintptr_t)nvnCommandBufferBindMultisampleState,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferBindPolygonState",
      (void *)(uintptr_t)nvnCommandBufferBindPolygonState,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferBindProgram",
      (void *)(uintptr_t)nvnCommandBufferBindProgram,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferBindSeparateSampler",
      (void *)(uintptr_t)nvnCommandBufferBindSeparateSampler,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferBindSeparateSamplers",
      (void *)(uintptr_t)nvnCommandBufferBindSeparateSamplers,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferBindSeparateSamplersDeferred",
      (void *)(uintptr_t)nvnCommandBufferBindSeparateSamplersDeferred,
      NVN_CB_ENTRY_DIAGNOSTIC_ONLY },
    { "nvnCommandBufferBindSeparateTexture",
      (void *)(uintptr_t)nvnCommandBufferBindSeparateTexture,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferBindSeparateTextures",
      (void *)(uintptr_t)nvnCommandBufferBindSeparateTextures,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferBindSeparateTexturesDeferred",
      (void *)(uintptr_t)nvnCommandBufferBindSeparateTexturesDeferred,
      NVN_CB_ENTRY_DIAGNOSTIC_ONLY },
    { "nvnCommandBufferBindStorageBuffer",
      (void *)(uintptr_t)nvnCommandBufferBindStorageBuffer,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferBindStorageBuffers",
      (void *)(uintptr_t)nvnCommandBufferBindStorageBuffers,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferBindStorageBuffersDeferred",
      (void *)(uintptr_t)nvnCommandBufferBindStorageBuffersDeferred,
      NVN_CB_ENTRY_DIAGNOSTIC_ONLY },
    { "nvnCommandBufferBindTexture",
      (void *)(uintptr_t)nvnCommandBufferBindTexture,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferBindTextures",
      (void *)(uintptr_t)nvnCommandBufferBindTextures,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferBindTexturesDeferred",
      (void *)(uintptr_t)nvnCommandBufferBindTexturesDeferred,
      NVN_CB_ENTRY_DIAGNOSTIC_ONLY },
    { "nvnCommandBufferBindTransformFeedbackBuffer",
      (void *)(uintptr_t)nvnCommandBufferBindTransformFeedbackBuffer,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferBindTransformFeedbackBuffers",
      (void *)(uintptr_t)nvnCommandBufferBindTransformFeedbackBuffers,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferBindUniformBuffer",
      (void *)(uintptr_t)nvnCommandBufferBindUniformBuffer,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferBindUniformBuffers",
      (void *)(uintptr_t)nvnCommandBufferBindUniformBuffers,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferBindUniformBuffersDeferred",
      (void *)(uintptr_t)nvnCommandBufferBindUniformBuffersDeferred,
      NVN_CB_ENTRY_DIAGNOSTIC_ONLY },
    { "nvnCommandBufferBindVertexAttribState",
      (void *)(uintptr_t)nvnCommandBufferBindVertexAttribState,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferBindVertexBuffer",
      (void *)(uintptr_t)nvnCommandBufferBindVertexBuffer,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferBindVertexBuffers",
      (void *)(uintptr_t)nvnCommandBufferBindVertexBuffers,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferBindVertexStreamState",
      (void *)(uintptr_t)nvnCommandBufferBindVertexStreamState,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferCallCommands",
      (void *)(uintptr_t)nvnCommandBufferCallCommands,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferClearBuffer",
      (void *)(uintptr_t)nvnCommandBufferClearBuffer,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferClearColor",
      (void *)(uintptr_t)nvnCommandBufferClearColor,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferClearColori",
      (void *)(uintptr_t)nvnCommandBufferClearColori,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferClearColorui",
      (void *)(uintptr_t)nvnCommandBufferClearColorui,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferClearDepthStencil",
      (void *)(uintptr_t)nvnCommandBufferClearDepthStencil,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferClearTexture",
      (void *)(uintptr_t)nvnCommandBufferClearTexture,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferClearTexturei",
      (void *)(uintptr_t)nvnCommandBufferClearTexturei,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferClearTextureui",
      (void *)(uintptr_t)nvnCommandBufferClearTextureui,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferCopyBufferToBuffer",
      (void *)(uintptr_t)nvnCommandBufferCopyBufferToBuffer,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferCopyBufferToTexture",
      (void *)(uintptr_t)nvnCommandBufferCopyBufferToTexture,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferCopyCommands",
      (void *)(uintptr_t)nvnCommandBufferCopyCommands,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferCopyTextureToBuffer",
      (void *)(uintptr_t)nvnCommandBufferCopyTextureToBuffer,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferCopyTextureToTexture",
      (void *)(uintptr_t)nvnCommandBufferCopyTextureToTexture,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferDiscardColor",
      (void *)(uintptr_t)nvnCommandBufferDiscardColor,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferDiscardDepthStencil",
      (void *)(uintptr_t)nvnCommandBufferDiscardDepthStencil,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferDispatchCompute",
      (void *)(uintptr_t)nvnCommandBufferDispatchCompute,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferDispatchComputeIndirect",
      (void *)(uintptr_t)nvnCommandBufferDispatchComputeIndirect,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferDownsample",
      (void *)(uintptr_t)nvnCommandBufferDownsample,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferDownsampleTextureView",
      (void *)(uintptr_t)nvnCommandBufferDownsampleTextureView,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferDrawArrays",
      (void *)(uintptr_t)nvnCommandBufferDrawArrays,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferDrawArraysIndirect",
      (void *)(uintptr_t)nvnCommandBufferDrawArraysIndirect,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferDrawArraysInstanced",
      (void *)(uintptr_t)nvnCommandBufferDrawArraysInstanced,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferDrawElements",
      (void *)(uintptr_t)nvnCommandBufferDrawElements,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferDrawElementsBaseVertex",
      (void *)(uintptr_t)nvnCommandBufferDrawElementsBaseVertex,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferDrawElementsIndirect",
      (void *)(uintptr_t)nvnCommandBufferDrawElementsIndirect,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferDrawElementsInstanced",
      (void *)(uintptr_t)nvnCommandBufferDrawElementsInstanced,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferDrawTexture",
      (void *)(uintptr_t)nvnCommandBufferDrawTexture,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferDrawTransformFeedback",
      (void *)(uintptr_t)nvnCommandBufferDrawTransformFeedback,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferEndRecording",
      (void *)(uintptr_t)nvnCommandBufferEndRecording,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferEndTransformFeedback",
      (void *)(uintptr_t)nvnCommandBufferEndTransformFeedback,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferFenceSync",
      (void *)(uintptr_t)nvnCommandBufferFenceSync,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferFinalize",
      (void *)(uintptr_t)nvnCommandBufferFinalize,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferGetCommandMemoryFree",
      (void *)(uintptr_t)nvnCommandBufferGetCommandMemoryFree,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferGetCommandMemorySize",
      (void *)(uintptr_t)nvnCommandBufferGetCommandMemorySize,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferGetCommandMemoryUsed",
      (void *)(uintptr_t)nvnCommandBufferGetCommandMemoryUsed,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferGetControlMemoryFree",
      (void *)(uintptr_t)nvnCommandBufferGetControlMemoryFree,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferGetControlMemorySize",
      (void *)(uintptr_t)nvnCommandBufferGetControlMemorySize,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferGetControlMemoryUsed",
      (void *)(uintptr_t)nvnCommandBufferGetControlMemoryUsed,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferGetCopyImageStride",
      (void *)(uintptr_t)nvnCommandBufferGetCopyImageStride,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferGetCopyRowStride",
      (void *)(uintptr_t)nvnCommandBufferGetCopyRowStride,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferGetMemoryCallback",
      (void *)(uintptr_t)nvnCommandBufferGetMemoryCallback,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferGetMemoryCallbackData",
      (void *)(uintptr_t)nvnCommandBufferGetMemoryCallbackData,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferInitialize",
      (void *)(uintptr_t)nvnCommandBufferInitialize,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferInsertCheckpointDynamic",
      (void *)(uintptr_t)nvnCommandBufferInsertCheckpointDynamic,
      NVN_CB_ENTRY_DIAGNOSTIC_ONLY },
    { "nvnCommandBufferInsertCheckpointStatic",
      (void *)(uintptr_t)nvnCommandBufferInsertCheckpointStatic,
      NVN_CB_ENTRY_DIAGNOSTIC_ONLY },
    { "nvnCommandBufferInsertDebugMarker",
      (void *)(uintptr_t)nvnCommandBufferInsertDebugMarker,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferInsertDebugMarkerDynamic",
      (void *)(uintptr_t)nvnCommandBufferInsertDebugMarkerDynamic,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferInsertDebugMarkerStatic",
      (void *)(uintptr_t)nvnCommandBufferInsertDebugMarkerStatic,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferIsRecording",
      (void *)(uintptr_t)nvnCommandBufferIsRecording,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferMultiDrawArraysIndirectCount",
      (void *)(uintptr_t)nvnCommandBufferMultiDrawArraysIndirectCount,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferMultiDrawElementsIndirectCount",
      (void *)(uintptr_t)nvnCommandBufferMultiDrawElementsIndirectCount,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferOverrideSubtileSize",
      (void *)(uintptr_t)nvnCommandBufferOverrideSubtileSize,
      NVN_CB_ENTRY_DIAGNOSTIC_ONLY },
    { "nvnCommandBufferPauseTransformFeedback",
      (void *)(uintptr_t)nvnCommandBufferPauseTransformFeedback,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferPopDebugGroup",
      (void *)(uintptr_t)nvnCommandBufferPopDebugGroup,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferPopDebugGroupId",
      (void *)(uintptr_t)nvnCommandBufferPopDebugGroupId,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferPushDebugGroup",
      (void *)(uintptr_t)nvnCommandBufferPushDebugGroup,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferPushDebugGroupDynamic",
      (void *)(uintptr_t)nvnCommandBufferPushDebugGroupDynamic,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferPushDebugGroupStatic",
      (void *)(uintptr_t)nvnCommandBufferPushDebugGroupStatic,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferReportCounter",
      (void *)(uintptr_t)nvnCommandBufferReportCounter,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferReportValue",
      (void *)(uintptr_t)nvnCommandBufferReportValue,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferResetCounter",
      (void *)(uintptr_t)nvnCommandBufferResetCounter,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferResolveDepthBuffer",
      (void *)(uintptr_t)nvnCommandBufferResolveDepthBuffer,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferRestoreZCullData",
      (void *)(uintptr_t)nvnCommandBufferRestoreZCullData,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferResumeTransformFeedback",
      (void *)(uintptr_t)nvnCommandBufferResumeTransformFeedback,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSaveZCullData",
      (void *)(uintptr_t)nvnCommandBufferSaveZCullData,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetAlphaRef",
      (void *)(uintptr_t)nvnCommandBufferSetAlphaRef,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetBlendColor",
      (void *)(uintptr_t)nvnCommandBufferSetBlendColor,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetColorReductionEnable",
      (void *)(uintptr_t)nvnCommandBufferSetColorReductionEnable,
      NVN_CB_ENTRY_DIAGNOSTIC_ONLY },
    { "nvnCommandBufferSetColorReductionThresholds",
      (void *)(uintptr_t)nvnCommandBufferSetColorReductionThresholds,
      NVN_CB_ENTRY_DIAGNOSTIC_ONLY },
    { "nvnCommandBufferSetCommandMemoryCallbackEnabled",
      (void *)(uintptr_t)nvnCommandBufferSetCommandMemoryCallbackEnabled,
      NVN_CB_ENTRY_DIAGNOSTIC_ONLY },
    { "nvnCommandBufferSetConservativeRasterDilate",
      (void *)(uintptr_t)nvnCommandBufferSetConservativeRasterDilate,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetConservativeRasterEnable",
      (void *)(uintptr_t)nvnCommandBufferSetConservativeRasterEnable,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetControlMemoryCallbackEnabled",
      (void *)(uintptr_t)nvnCommandBufferSetControlMemoryCallbackEnabled,
      NVN_CB_ENTRY_DIAGNOSTIC_ONLY },
    { "nvnCommandBufferSetCopyImageStride",
      (void *)(uintptr_t)nvnCommandBufferSetCopyImageStride,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetCopyRowStride",
      (void *)(uintptr_t)nvnCommandBufferSetCopyRowStride,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetDebugLabel",
      (void *)(uintptr_t)nvnCommandBufferSetDebugLabel,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetDepthBounds",
      (void *)(uintptr_t)nvnCommandBufferSetDepthBounds,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetDepthClamp",
      (void *)(uintptr_t)nvnCommandBufferSetDepthClamp,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetDepthRange",
      (void *)(uintptr_t)nvnCommandBufferSetDepthRange,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetDepthRanges",
      (void *)(uintptr_t)nvnCommandBufferSetDepthRanges,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetInnerTessellationLevels",
      (void *)(uintptr_t)nvnCommandBufferSetInnerTessellationLevels,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetLineWidth",
      (void *)(uintptr_t)nvnCommandBufferSetLineWidth,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetMemoryCallback",
      (void *)(uintptr_t)nvnCommandBufferSetMemoryCallback,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetMemoryCallbackData",
      (void *)(uintptr_t)nvnCommandBufferSetMemoryCallbackData,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetOuterTessellationLevels",
      (void *)(uintptr_t)nvnCommandBufferSetOuterTessellationLevels,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetPatchSize",
      (void *)(uintptr_t)nvnCommandBufferSetPatchSize,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetPointSize",
      (void *)(uintptr_t)nvnCommandBufferSetPointSize,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetPolygonOffsetClamp",
      (void *)(uintptr_t)nvnCommandBufferSetPolygonOffsetClamp,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetPrimitiveRestart",
      (void *)(uintptr_t)nvnCommandBufferSetPrimitiveRestart,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetProgramSubroutines",
      (void *)(uintptr_t)nvnCommandBufferSetProgramSubroutines,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetProvokingVertexMode",
      (void *)(uintptr_t)nvnCommandBufferSetProvokingVertexMode,
      NVN_CB_ENTRY_DIAGNOSTIC_ONLY },
    { "nvnCommandBufferSetRasterizerDiscard",
      (void *)(uintptr_t)nvnCommandBufferSetRasterizerDiscard,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetRenderEnable",
      (void *)(uintptr_t)nvnCommandBufferSetRenderEnable,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetRenderEnableConditional",
      (void *)(uintptr_t)nvnCommandBufferSetRenderEnableConditional,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetRenderTargets",
      (void *)(uintptr_t)nvnCommandBufferSetRenderTargets,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetSampleMask",
      (void *)(uintptr_t)nvnCommandBufferSetSampleMask,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetSamplerPool",
      (void *)(uintptr_t)nvnCommandBufferSetSamplerPool,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetScissor",
      (void *)(uintptr_t)nvnCommandBufferSetScissor,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetScissors",
      (void *)(uintptr_t)nvnCommandBufferSetScissors,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetShaderScratchMemory",
      (void *)(uintptr_t)nvnCommandBufferSetShaderScratchMemory,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetStencilCullCriteria",
      (void *)(uintptr_t)nvnCommandBufferSetStencilCullCriteria,
      NVN_CB_ENTRY_DIAGNOSTIC_ONLY },
    { "nvnCommandBufferSetStencilMask",
      (void *)(uintptr_t)nvnCommandBufferSetStencilMask,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetStencilRef",
      (void *)(uintptr_t)nvnCommandBufferSetStencilRef,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetStencilValueMask",
      (void *)(uintptr_t)nvnCommandBufferSetStencilValueMask,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetSubpixelPrecisionBias",
      (void *)(uintptr_t)nvnCommandBufferSetSubpixelPrecisionBias,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetTexturePool",
      (void *)(uintptr_t)nvnCommandBufferSetTexturePool,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetTiledCacheAction",
      (void *)(uintptr_t)nvnCommandBufferSetTiledCacheAction,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetTiledCacheTileSize",
      (void *)(uintptr_t)nvnCommandBufferSetTiledCacheTileSize,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetViewport",
      (void *)(uintptr_t)nvnCommandBufferSetViewport,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetViewportSwizzles",
      (void *)(uintptr_t)nvnCommandBufferSetViewportSwizzles,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetViewports",
      (void *)(uintptr_t)nvnCommandBufferSetViewports,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferSetZCullZF32CompressionEnable",
      (void *)(uintptr_t)nvnCommandBufferSetZCullZF32CompressionEnable,
      NVN_CB_ENTRY_DIAGNOSTIC_ONLY },
    { "nvnCommandBufferSignalEvent",
      (void *)(uintptr_t)nvnCommandBufferSignalEvent,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferTiledDownsample",
      (void *)(uintptr_t)nvnCommandBufferTiledDownsample,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferTiledDownsampleTextureView",
      (void *)(uintptr_t)nvnCommandBufferTiledDownsampleTextureView,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferUpdateUniformBuffer",
      (void *)(uintptr_t)nvnCommandBufferUpdateUniformBuffer,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferWaitEvent",
      (void *)(uintptr_t)nvnCommandBufferWaitEvent,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
    { "nvnCommandBufferWaitSync",
      (void *)(uintptr_t)nvnCommandBufferWaitSync,
      NVN_CB_ENTRY_SIGNATURE_CONFIRMED },
};

size_t nvn_command_buffer_family_count(void) {
    return sizeof(s_command_buffer_family) / sizeof(s_command_buffer_family[0]);
}

const nvn_command_buffer_family_entry *nvn_command_buffer_family_entry_at(size_t index) {
    if (index >= nvn_command_buffer_family_count()) return NULL;
    return &s_command_buffer_family[index];
}
