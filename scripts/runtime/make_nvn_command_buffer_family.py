#!/usr/bin/env python3
"""Generate the NVN command buffer family of the FFRS RG477M native runtime.

The guest resolves every NVN entry point by name at start-up. The 156 names it
resolves that belong to the command buffer family are listed in
evidence/nvn/main_nvn_ids.txt, and the signatures of those entry points are in
the vendored NVN 53.105 header in src/graphics/nvn/include/nvn/nvn.h.

The records that the entry points write are not this runtime's own format. The
command stream of the game this port runs is the format of the reference tree
that runs the same game (MIT, .local/refs/nxnative, revision 7a029f66):

    .local/refs/nxnative/dm/gfx/nvn/nvn_obj.h
        typedef struct { uint32_t op, size; } op_hdr;
        enum { OP_END, OP_JUMP, OP_BEGIN, OP_CALL, ... }

So every entry point that the reference's format has a record for calls the
recorder of that record, and every entry point that it has none for is
recorded in the diagnostic channel, where the pointers whose size is not known
are named and never followed.

This script writes src/graphics/nvn_device/nvn_command_buffer.generated.c:
the 156 entry points, the name table the dispatcher hands them out with, and
the status of every entry point.

Regenerate with:

    python3 scripts/runtime/make_nvn_command_buffer_family.py

The output depends on nothing but the two inputs, so the same inputs always
give the same file: there is no timestamp and no ordering that depends on the
filesystem.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
HEADER_PATH = ROOT / "src/graphics/nvn/include/nvn/nvn.h"
NAMES_PATH = ROOT / "evidence/nvn/main_nvn_ids.txt"
OUTPUT_PATH = ROOT / "src/graphics/nvn_device/nvn_command_buffer.generated.c"

FAMILY_PREFIX = "nvnCommandBuffer"
TYPEDEF_PREFIX = "PFNNVNCOMMANDBUFFER"
CALLBACK_TYPEDEF = "PFNNVNCOMMANDBUFFERMEMORYCALLBACKPROC"

# The names the guest resolves for which the header this runtime was built
# against holds no typedef. Their signatures are not known and are not
# guessed: they are taken as plain words, nothing is dereferenced, and they
# are reported as diagnostic only. The guest calls them already, so they must
# not trap, but they must not be reported as implemented either.
RAW_NAMES = [
    "nvnCommandBufferBindImagesDeferred",
    "nvnCommandBufferBindSeparateSamplersDeferred",
    "nvnCommandBufferBindSeparateTexturesDeferred",
    "nvnCommandBufferBindStorageBuffersDeferred",
    "nvnCommandBufferBindTexturesDeferred",
    "nvnCommandBufferBindUniformBuffersDeferred",
    "nvnCommandBufferInsertCheckpointDynamic",
    "nvnCommandBufferInsertCheckpointStatic",
    "nvnCommandBufferOverrideSubtileSize",
    "nvnCommandBufferSetColorReductionEnable",
    "nvnCommandBufferSetColorReductionThresholds",
    "nvnCommandBufferSetCommandMemoryCallbackEnabled",
    "nvnCommandBufferSetControlMemoryCallbackEnabled",
    "nvnCommandBufferSetProvokingVertexMode",
    "nvnCommandBufferSetStencilCullCriteria",
    "nvnCommandBufferSetZCullZF32CompressionEnable",
]

# The structs that are handed to an entry point by value. They are the handle
# wrappers of the SDK, not the SDK objects themselves, so their layout is
# copied. Every other SDK object is only ever named by a pointer here, and
# those stay incomplete types, because the layout of an NVNobject is a
# property of the Nintendo SDK that this runtime does not have.
BY_VALUE_STRUCTS = {"NVNseparateTextureHandle", "NVNseparateSamplerHandle"}

# The scalar typedefs of the header that are handed over by value.
BY_VALUE_SCALARS = {
    "NVNboolean": "uint8_t",
    "NVNtextureHandle": "uint64_t",
    "NVNimageHandle": "uint64_t",
    "NVNcommandHandle": "uint64_t",
    "NVNbufferAddress": "uint64_t",
}

# The 64 bit types of the header and of C itself.
WIDE_TYPES = {
    "uint64_t",
    "int64_t",
    "size_t",
    "ptrdiff_t",
    "NVNbufferAddress",
    "NVNcommandHandle",
    "NVNtextureHandle",
    "NVNimageHandle",
}

# The types C itself provides. They are not in the header and do not have to
# be copied into the output.
BUILTIN_TYPES = {
    "void",
    "char",
    "short",
    "int",
    "long",
    "float",
    "double",
    "unsigned",
    "int8_t",
    "uint8_t",
    "int16_t",
    "uint16_t",
    "int32_t",
    "uint32_t",
    "int64_t",
    "uint64_t",
    "size_t",
    "ptrdiff_t",
}

# ------------------------------------------------------------------ #
# How the pointer arguments of the family are treated                  #
# ------------------------------------------------------------------ #
#
# Three classes, and every pointer argument of the family is in one of
# them. The class decides what the recorder does with the argument:
#
#   object  : a fixed size object. It is copied at the moment of the call,
#             and the record holds the copy, not the pointer.
#   array   : an array whose element size is known and whose count comes
#             from the argument next to it. It is copied at the moment of
#             the call, and the count goes into the record.
#   unknown : the size is not known. The pointer is never dereferenced and
#             never guessed at; it is recorded as a pointer that was not
#             followed, so that whoever reads the record knows that the
#             data behind it is not in the record.

# The objects of a fixed size, with the size the header declares.
OBJECT_SIZES = {
    "NVNblendState": 0x8,
    "NVNchannelMaskState": 0x4,
    "NVNcolorState": 0x4,
    "NVNdepthStencilState": 0x8,
    "NVNdevice": 0x3000,
    "NVNevent": 0x40,
    "NVNmemoryPool": 0x100,
    "NVNmultisampleState": 0x18,
    "NVNpolygonState": 0x4,
    "NVNprogram": 0xC0,
    "NVNsamplerPool": 0x20,
    "NVNsync": 0x40,
    "NVNtexture": 0xC0,
    "NVNtexturePool": 0x20,
    "NVNtextureView": 0x28,
    "NVNvertexAttribState": 0x4,
    "NVNvertexStreamState": 0x8,
}

# The arrays whose element size is known. The count of the array is the
# argument right before it.
ARRAY_ELEMENT_SIZES = {
    "NVNbufferRange": 16,
    "NVNimageHandle": 8,
    "NVNseparateSamplerHandle": 8,
    "NVNseparateTextureHandle": 8,
    "NVNtextureHandle": 8,
    "NVNviewportSwizzle": 4,
}

# The arrays of plain numbers whose element size is known, but only when the
# argument next to them is the count of the array. Without a count they are
# of the unknown size class.
NUMBER_ARRAY_ELEMENT_SIZES = {
    "int": 4,
    "float": 4,
    "uint32_t": 4,
    "int32_t": 4,
}

# The pointer types whose size the runtime does not know, whatever is next
# to them.
UNKNOWN_POINTER_TYPES = {
    "char",
    "void",
    "NVNdrawTextureRegion",
}

# The entry points whose array of numbers has no count next to it, although
# the argument next to it looks like one. The third subroutine list of
# SetProgramSubroutines is such a case.
NO_COUNT = {
    "nvnCommandBufferSetProgramSubroutines",
}

# ------------------------------------------------------------------ #
# The recorder of every entry point that the reference's format has   #
# a record for.                                                       #
# ------------------------------------------------------------------ #
#
# The name is the entry point, the value is the recorder and the
# arguments that are handed to it. "arg0" is the command buffer itself.

RECORDERS = {
    "nvnCommandBufferBarrier": ("nvn_command_buffer_rec_barrier", ["arg1"]),
    "nvnCommandBufferBeginTransformFeedback": (
        "nvn_command_buffer_rec_transform_feedback_state", ["0", "arg1"]),
    "nvnCommandBufferBindBlendState": (
        "nvn_command_buffer_rec_bind_state", ["0", "arg1", "NVN_CB_SIZE_NVNblendState"]),
    "nvnCommandBufferBindChannelMaskState": (
        "nvn_command_buffer_rec_bind_state",
        ["2", "arg1", "NVN_CB_SIZE_NVNchannelMaskState"]),
    "nvnCommandBufferBindColorState": (
        "nvn_command_buffer_rec_bind_state", ["1", "arg1", "NVN_CB_SIZE_NVNcolorState"]),
    "nvnCommandBufferBindDepthStencilState": (
        "nvn_command_buffer_rec_bind_state",
        ["5", "arg1", "NVN_CB_SIZE_NVNdepthStencilState"]),
    "nvnCommandBufferBindImage": (
        "nvn_command_buffer_rec_bind_image", ["arg1", "arg2", "arg3"]),
    "nvnCommandBufferBindImages": (
        "nvn_command_buffer_rec_bind_images", ["arg1", "arg2", "arg3", "arg4"]),
    "nvnCommandBufferBindMultisampleState": (
        "nvn_command_buffer_rec_bind_state",
        ["3", "arg1", "NVN_CB_SIZE_NVNmultisampleState"]),
    "nvnCommandBufferBindPolygonState": (
        "nvn_command_buffer_rec_bind_state", ["4", "arg1", "NVN_CB_SIZE_NVNpolygonState"]),
    "nvnCommandBufferBindProgram": (
        "nvn_command_buffer_rec_bind_program",
        ["arg1", "NVN_CB_SIZE_NVNprogram", "arg2"]),
    "nvnCommandBufferBindSeparateSampler": (
        "nvn_command_buffer_rec_bind_separate_sampler", ["arg1", "arg2", "arg3.value"]),
    "nvnCommandBufferBindSeparateSamplers": (
        "nvn_command_buffer_rec_bind_separate_samplers", ["arg1", "arg2", "arg3", "arg4"]),
    "nvnCommandBufferBindSeparateTexture": (
        "nvn_command_buffer_rec_bind_separate_texture", ["arg1", "arg2", "arg3.value"]),
    "nvnCommandBufferBindSeparateTextures": (
        "nvn_command_buffer_rec_bind_separate_textures", ["arg1", "arg2", "arg3", "arg4"]),
    "nvnCommandBufferBindStorageBuffer": (
        "nvn_command_buffer_rec_bind_storage_buffer", ["arg1", "arg2", "arg3", "arg4"]),
    "nvnCommandBufferBindStorageBuffers": (
        "nvn_command_buffer_rec_bind_storage_buffers",
        ["arg1", "arg2", "arg3", "arg4", "NVN_CB_SIZE_NVNbufferRange"]),
    "nvnCommandBufferBindTexture": (
        "nvn_command_buffer_rec_bind_texture", ["arg1", "arg2", "arg3"]),
    "nvnCommandBufferBindTextures": (
        "nvn_command_buffer_rec_bind_textures", ["arg1", "arg2", "arg3", "arg4"]),
    "nvnCommandBufferBindTransformFeedbackBuffer": (
        "nvn_command_buffer_rec_bind_transform_feedback_buffer", ["arg1", "arg2", "arg3"]),
    "nvnCommandBufferBindTransformFeedbackBuffers": (
        "nvn_command_buffer_rec_bind_transform_feedback_buffers", ["arg1", "arg2", "arg3"]),
    "nvnCommandBufferBindUniformBuffer": (
        "nvn_command_buffer_rec_bind_uniform_buffer", ["arg1", "arg2", "arg3", "arg4"]),
    "nvnCommandBufferBindUniformBuffers": (
        "nvn_command_buffer_rec_bind_uniform_buffers",
        ["arg1", "arg2", "arg3", "arg4", "NVN_CB_SIZE_NVNbufferRange"]),
    "nvnCommandBufferBindVertexAttribState": (
        "nvn_command_buffer_rec_bind_vertex_attrib_state",
        ["arg1", "arg2", "NVN_CB_SIZE_NVNvertexAttribState"]),
    "nvnCommandBufferBindVertexBuffer": (
        "nvn_command_buffer_rec_bind_vertex_buffer", ["arg1", "arg2", "arg3"]),
    "nvnCommandBufferBindVertexBuffers": (
        "nvn_command_buffer_rec_bind_vertex_buffers",
        ["arg1", "arg2", "arg3", "NVN_CB_SIZE_NVNbufferRange"]),
    "nvnCommandBufferBindVertexStreamState": (
        "nvn_command_buffer_rec_bind_vertex_stream_state",
        ["arg1", "arg2", "NVN_CB_SIZE_NVNvertexStreamState"]),
    "nvnCommandBufferCallCommands": (
        "nvn_command_buffer_rec_call_commands", ["arg1", "arg2"]),
    "nvnCommandBufferClearBuffer": (
        "nvn_command_buffer_rec_clear_buffer", ["arg1", "arg2", "arg3"]),
    "nvnCommandBufferClearColor": (
        "nvn_command_buffer_rec_clear_color", ["arg1", "arg2", "arg3", "0"]),
    "nvnCommandBufferClearColori": (
        "nvn_command_buffer_rec_clear_color", ["arg1", "arg2", "arg3", "1"]),
    "nvnCommandBufferClearColorui": (
        "nvn_command_buffer_rec_clear_color", ["arg1", "arg2", "arg3", "1"]),
    "nvnCommandBufferClearDepthStencil": (
        "nvn_command_buffer_rec_clear_depth_stencil", ["arg1", "arg2", "arg3", "arg4"]),
    "nvnCommandBufferClearTexture": (
        "nvn_command_buffer_rec_clear_texture",
        ["arg1", "arg2", "arg3", "arg4", "arg5", "0"]),
    "nvnCommandBufferClearTexturei": (
        "nvn_command_buffer_rec_clear_texture",
        ["arg1", "arg2", "arg3", "arg4", "arg5", "1"]),
    "nvnCommandBufferClearTextureui": (
        "nvn_command_buffer_rec_clear_texture",
        ["arg1", "arg2", "arg3", "arg4", "arg5", "1"]),
    "nvnCommandBufferCopyBufferToBuffer": (
        "nvn_command_buffer_rec_copy_buffer_to_buffer", ["arg1", "arg2", "arg3"]),
    "nvnCommandBufferCopyBufferToTexture": (
        "nvn_command_buffer_rec_copy_buffer_to_texture",
        ["arg1", "arg2", "arg3", "arg4", "arg5"]),
    "nvnCommandBufferCopyCommands": (
        "nvn_command_buffer_rec_call_commands", ["arg1", "arg2"]),
    "nvnCommandBufferCopyTextureToBuffer": (
        "nvn_command_buffer_rec_copy_texture_to_buffer",
        ["arg1", "arg2", "arg3", "arg4", "arg5"]),
    "nvnCommandBufferCopyTextureToTexture": (
        "nvn_command_buffer_rec_copy_texture_to_texture",
        ["arg1", "arg2", "arg3", "arg4", "arg5", "arg6", "arg7"]),
    "nvnCommandBufferDispatchCompute": (
        "nvn_command_buffer_rec_dispatch", ["arg1", "arg2", "arg3", "0"]),
    "nvnCommandBufferDispatchComputeIndirect": (
        "nvn_command_buffer_rec_dispatch", ["0", "0", "0", "arg1"]),
    "nvnCommandBufferDownsample": (
        "nvn_command_buffer_rec_copy_texture_to_texture",
        ["arg1", "0", "0", "arg2", "0", "0", "NVN_CB_COPY_RESOLVE"]),
    "nvnCommandBufferDownsampleTextureView": (
        "nvn_command_buffer_rec_copy_texture_to_texture",
        ["arg1", "arg2", "0", "arg3", "arg4", "0", "NVN_CB_COPY_RESOLVE"]),
    "nvnCommandBufferDrawArrays": (
        "nvn_command_buffer_rec_draw", ["arg1", "arg2", "arg3", "1", "0", "0"]),
    "nvnCommandBufferDrawArraysIndirect": (
        "nvn_command_buffer_rec_draw", ["arg1", "0", "0", "1", "0", "arg2"]),
    "nvnCommandBufferDrawArraysInstanced": (
        "nvn_command_buffer_rec_draw", ["arg1", "arg2", "arg3", "arg4", "arg5", "0"]),
    "nvnCommandBufferDrawElements": (
        "nvn_command_buffer_rec_draw_indexed",
        ["arg1", "arg2", "arg3", "0", "0", "1", "arg4", "0"]),
    "nvnCommandBufferDrawElementsBaseVertex": (
        "nvn_command_buffer_rec_draw_indexed",
        ["arg1", "arg2", "arg3", "arg5", "0", "1", "arg4", "0"]),
    "nvnCommandBufferDrawElementsIndirect": (
        "nvn_command_buffer_rec_draw_indexed",
        ["arg1", "arg2", "0", "0", "0", "1", "arg3", "arg4"]),
    "nvnCommandBufferDrawElementsInstanced": (
        "nvn_command_buffer_rec_draw_indexed",
        ["arg1", "arg2", "arg3", "arg5", "arg6", "arg7", "arg4", "0"]),
    "nvnCommandBufferEndTransformFeedback": (
        "nvn_command_buffer_rec_transform_feedback_state", ["1", "arg1"]),
    "nvnCommandBufferFenceSync": ("nvn_command_buffer_rec_fence_sync", ["arg1"]),
    "nvnCommandBufferPauseTransformFeedback": (
        "nvn_command_buffer_rec_transform_feedback_state", ["2", "arg1"]),
    "nvnCommandBufferReportCounter": (
        "nvn_command_buffer_rec_report_counter", ["arg1", "arg2"]),
    "nvnCommandBufferReportValue": (
        "nvn_command_buffer_rec_report_value", ["arg1", "arg2"]),
    "nvnCommandBufferResetCounter": ("nvn_command_buffer_rec_reset_counter", ["arg1"]),
    "nvnCommandBufferResumeTransformFeedback": (
        "nvn_command_buffer_rec_transform_feedback_state", ["3", "arg1"]),
    "nvnCommandBufferSetBlendColor": ("nvn_command_buffer_rec_set_blend_color", ["arg1"]),
    "nvnCommandBufferSetDepthClamp": ("nvn_command_buffer_rec_set_depth_clamp", ["arg1"]),
    "nvnCommandBufferSetDepthRange": (
        "nvn_command_buffer_rec_set_depth_range", ["arg1", "arg2"]),
    "nvnCommandBufferSetDepthRanges": (
        "nvn_command_buffer_rec_set_depth_ranges", ["arg1", "arg2", "arg3"]),
    "nvnCommandBufferSetPolygonOffsetClamp": (
        "nvn_command_buffer_rec_set_polygon_offset", ["arg1", "arg2", "arg3"]),
    "nvnCommandBufferSetPrimitiveRestart": (
        "nvn_command_buffer_rec_set_primitive_restart", ["arg1", "arg2"]),
    "nvnCommandBufferSetRasterizerDiscard": (
        "nvn_command_buffer_rec_set_rasterizer_discard", ["arg1"]),
    "nvnCommandBufferSetRenderEnable": (
        "nvn_command_buffer_rec_set_render_enable", ["arg1"]),
    "nvnCommandBufferSetRenderEnableConditional": (
        "nvn_command_buffer_rec_set_render_enable_conditional", ["arg1", "arg2"]),
    "nvnCommandBufferSetRenderTargets": (
        "nvn_command_buffer_rec_set_render_targets",
        ["arg1", "(const void *const *)arg2", "NVN_CB_SIZE_NVNtexture",
         "(const void *const *)arg3", "NVN_CB_SIZE_NVNtextureView", "arg4", "arg5"]),
    "nvnCommandBufferSetSamplerPool": (
        "nvn_command_buffer_rec_set_sampler_pool", ["arg1"]),
    "nvnCommandBufferSetScissor": (
        "nvn_command_buffer_rec_set_scissor", ["arg1", "arg2", "arg3", "arg4"]),
    "nvnCommandBufferSetScissors": (
        "nvn_command_buffer_rec_set_scissors", ["arg1", "arg2", "arg3"]),
    "nvnCommandBufferSetStencilMask": (
        "nvn_command_buffer_rec_set_stencil", ["arg1", "0", "arg2", "0", "1"]),
    "nvnCommandBufferSetStencilRef": (
        "nvn_command_buffer_rec_set_stencil", ["arg1", "0", "0", "arg2", "2"]),
    "nvnCommandBufferSetStencilValueMask": (
        "nvn_command_buffer_rec_set_stencil", ["arg1", "arg2", "0", "0", "0"]),
    "nvnCommandBufferSetTexturePool": (
        "nvn_command_buffer_rec_set_texture_pool", ["arg1"]),
    "nvnCommandBufferSetViewport": (
        "nvn_command_buffer_rec_set_viewport", ["arg1", "arg2", "arg3", "arg4"]),
    "nvnCommandBufferSetViewportSwizzles": (
        "nvn_command_buffer_rec_set_viewport_swizzles", ["arg1", "arg2", "arg3"]),
    "nvnCommandBufferSetViewports": (
        "nvn_command_buffer_rec_set_viewports", ["arg1", "arg2", "arg3"]),
    "nvnCommandBufferSignalEvent": (
        "nvn_command_buffer_rec_signal_event", ["arg1", "arg2", "arg5"]),
    "nvnCommandBufferTiledDownsample": (
        "nvn_command_buffer_rec_copy_texture_to_texture",
        ["arg1", "0", "0", "arg2", "0", "0", "NVN_CB_COPY_RESOLVE"]),
    "nvnCommandBufferTiledDownsampleTextureView": (
        "nvn_command_buffer_rec_copy_texture_to_texture",
        ["arg1", "arg2", "0", "arg3", "arg4", "0", "NVN_CB_COPY_RESOLVE"]),
    "nvnCommandBufferUpdateUniformBuffer": (
        "nvn_command_buffer_rec_update_uniform_buffer", ["arg1", "arg3", "arg4", "arg5"]),
}

# The entry points that change the state of the recorder instead of, or as
# well as, recording a command.
STATE_CALLS = {
    "nvnCommandBufferAddCommandMemory": [
        "    nvn_command_buffer_add_command_memory(commandBuffer, (uint64_t)arg3);",
    ],
    "nvnCommandBufferAddControlMemory": [
        "    nvn_command_buffer_add_control_memory(commandBuffer, (uint64_t)arg2);",
    ],
    "nvnCommandBufferBeginRecording": [
        "    nvn_command_buffer_begin_recording(commandBuffer);",
    ],
    "nvnCommandBufferEndRecording": [],
    "nvnCommandBufferFinalize": [
        "    nvn_command_buffer_finalize(commandBuffer);",
    ],
    "nvnCommandBufferInitialize": [
        "    int initialized = nvn_command_buffer_initialize(commandBuffer);",
    ],
    "nvnCommandBufferSetCopyImageStride": [
        "    nvn_command_buffer_set_copy_image_stride(commandBuffer, (int64_t)arg1);",
    ],
    "nvnCommandBufferSetCopyRowStride": [
        "    nvn_command_buffer_set_copy_row_stride(commandBuffer, (int64_t)arg1);",
    ],
    "nvnCommandBufferSetMemoryCallback": [
        "    nvn_command_buffer_set_memory_callback(commandBuffer, (uint64_t)(uintptr_t)arg1);",
    ],
    "nvnCommandBufferSetMemoryCallbackData": [
        "    nvn_command_buffer_set_memory_callback_data(commandBuffer, (uint64_t)(uintptr_t)arg1);",
    ],
}

# The entry points that hand a value back to the guest.
RETURNS = {
    "nvnCommandBufferEndRecording": [
        "    return (NVNcommandHandle)nvn_command_buffer_end_recording(commandBuffer);",
    ],
    "nvnCommandBufferInitialize": [
        "    return (NVNboolean)(initialized ? 1 : 0);",
    ],
}

# The entry points that answer a question about the recorder.
QUERY_CALLS = {
    "nvnCommandBufferGetCommandMemoryFree": [
        "    return (size_t)nvn_command_buffer_get_command_memory_free(commandBuffer);",
    ],
    "nvnCommandBufferGetCommandMemorySize": [
        "    return (size_t)nvn_command_buffer_get_command_memory_size(commandBuffer);",
    ],
    "nvnCommandBufferGetCommandMemoryUsed": [
        "    return (size_t)nvn_command_buffer_get_command_memory_used(commandBuffer);",
    ],
    "nvnCommandBufferGetControlMemoryFree": [
        "    return (size_t)nvn_command_buffer_get_control_memory_free(commandBuffer);",
    ],
    "nvnCommandBufferGetControlMemorySize": [
        "    return (size_t)nvn_command_buffer_get_control_memory_size(commandBuffer);",
    ],
    "nvnCommandBufferGetControlMemoryUsed": [
        "    nvn_command_buffer_report_used_is_unknown(commandBuffer, \"control memory\");",
        "    return 0;",
    ],
    "nvnCommandBufferGetCopyImageStride": [
        "    return (ptrdiff_t)nvn_command_buffer_get_copy_image_stride(commandBuffer);",
    ],
    "nvnCommandBufferGetCopyRowStride": [
        "    return (ptrdiff_t)nvn_command_buffer_get_copy_row_stride(commandBuffer);",
    ],
    "nvnCommandBufferGetMemoryCallback": [
        "    return (PFNNVNCOMMANDBUFFERMEMORYCALLBACKPROC)",
        "        (uintptr_t)nvn_command_buffer_get_memory_callback(commandBuffer);",
    ],
    "nvnCommandBufferGetMemoryCallbackData": [
        "    /* The header this runtime was built against declares this entry point as",
        "     * returning nothing, but the value it names is the callback data the guest",
        "     * set, and the guest reads it from the result register. The value goes",
        "     * there directly, because a C function that returns nothing cannot hand",
        "     * it over otherwise. */",
        "    nvn_command_buffer_set_result(",
        "        nvn_command_buffer_get_memory_callback_data(commandBuffer));",
    ],
    "nvnCommandBufferIsRecording": [
        "    return (NVNboolean)(nvn_command_buffer_is_recording(commandBuffer) ? 1 : 0);",
    ],
}

FUNC_TYPEDEF = re.compile(
    r"^typedef\s+(?P<ret>.+?)\(\s*\*\s*(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\)\s*"
    r"\((?P<args>.*)\)\s*;$",
    re.S,
)
SIMPLE_TYPEDEF = re.compile(
    r"^typedef\s+(?P<base>[A-Za-z_][A-Za-z0-9_ ]*?\*?)\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*;$",
    re.S,
)
ENUM_TYPEDEF = re.compile(r"^typedef\s+enum\s*\{.*\}\s*(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*;$", re.S)
STRUCT_TYPEDEF = re.compile(r"^typedef\s+struct\s*\{.*\}\s*(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*;$", re.S)


def fail(message):
    sys.stderr.write("make_nvn_command_buffer_family: %s\n" % message)
    raise SystemExit(1)


def typedef_statements(text):
    """Every typedef statement of the header, one at a time.

    A typedef ends at the first semicolon that is not inside a bracket, so
    the statements are cut out of the header instead of being matched with a
    regular expression over the whole file, which would run one statement
    into the next."""
    index = 0
    while True:
        start = text.find("typedef", index)
        if start < 0:
            return
        depth = 0
        cursor = start
        while cursor < len(text):
            char = text[cursor]
            if char in "([{":
                depth += 1
            elif char in ")]}":
                depth -= 1
            elif char == ";" and depth == 0:
                break
            cursor += 1
        if cursor >= len(text):
            return
        yield text[start:cursor + 1]
        index = cursor + 1


def split_arguments(text):
    """The arguments of a parameter list, split at the commas that are not
    inside a bracket."""
    out = []
    depth = 0
    current = ""
    for char in text:
        if char == "," and depth == 0:
            out.append(current.strip())
            current = ""
            continue
        if char in "([{":
            depth += 1
        elif char in ")]}":
            depth -= 1
        current += char
    if current.strip():
        out.append(current.strip())
    return out


def base_type(argument):
    """The type of an argument without its qualifiers, and whether it is a
    pointer.

    A pointer to a pointer is written "const NVNtexture* const*", so every
    star and every const is peeled off until the name of the type is left."""
    text = argument.strip()
    pointer = "*" in text
    while True:
        stripped = text.rstrip()
        if stripped.endswith("*"):
            text = stripped[:-1].rstrip()
            continue
        if stripped.endswith("const"):
            text = stripped[:-len("const")].rstrip()
            continue
        text = stripped
        break
    text = re.sub(r"^const\s+", "", text).strip()
    return text, pointer


def read_header():
    """The typedefs of the header, by name.

    Each one is kept together with the kind it is, because the kind decides
    what the generator copies into the output: an enumeration and a handle
    struct are handed over by value and have to be complete, while an SDK
    object is only named by a pointer and stays incomplete."""
    text = HEADER_PATH.read_text()
    by_name = {}
    for statement in typedef_statements(text):
        match = FUNC_TYPEDEF.match(statement)
        if match:
            by_name[match.group("name")] = {
                "kind": "func",
                "text": statement,
                "ret": match.group("ret").strip(),
                "args": split_arguments(match.group("args")),
            }
            continue
        match = ENUM_TYPEDEF.match(statement)
        if match:
            by_name[match.group("name")] = {"kind": "enum", "text": statement, "args": []}
            continue
        match = STRUCT_TYPEDEF.match(statement)
        if match:
            by_name[match.group("name")] = {"kind": "struct", "text": statement, "args": []}
            continue
        match = SIMPLE_TYPEDEF.match(statement)
        if match:
            by_name[match.group("name")] = {
                "kind": "scalar",
                "text": statement,
                "args": [],
                "base": match.group("base").strip(),
            }
            continue
        fail("the header holds a typedef this generator does not understand:\n%s" % statement)
    return by_name


def read_family_names():
    """The names of the family, in the order the guest resolves them."""
    names = []
    for line in NAMES_PATH.read_text().splitlines():
        name = line.strip()
        if not name or name.startswith("#"):
            continue
        if name.startswith(FAMILY_PREFIX):
            names.append(name)
    return names


def select_signatures(names, typedefs):
    """The typedef of every name of the family, and the names that have none.

    The mapping is exact: the name of the guest without its family prefix,
    in upper case, is the middle of the typedef."""
    signed = {}
    unsigned = []
    for name in names:
        middle = name[len(FAMILY_PREFIX):].upper()
        typedef_name = TYPEDEF_PREFIX + middle + "PROC"
        if typedef_name == CALLBACK_TYPEDEF:
            continue
        if typedef_name in typedefs:
            signed[name] = typedefs[typedef_name]
        else:
            unsigned.append(name)
    return signed, unsigned


def needed_types(signed):
    """The types that have to be complete in the output.

    A type is needed when an entry point takes it by value, either as an
    argument or as what it returns. Everything else is only named by a pointer
    and stays an incomplete type, so that the output never carries the layout
    of an SDK object."""
    by_value = set()
    pointers = set()
    for typedef in signed.values():
        arguments = list(typedef["args"])
        if typedef["ret"] != "void":
            arguments.append(typedef["ret"])
        for argument in arguments:
            name, pointer = base_type(argument)
            if pointer:
                if name not in BUILTIN_TYPES:
                    pointers.add(name)
            else:
                by_value.add(name)
    return by_value, pointers


def collect_local_types(signed, typedefs):
    """The type definitions the output carries.

    The callback typedef names one more type by value, so the search follows
    it as well."""
    by_value, pointers = needed_types(signed)
    complete = set()
    pending = list(by_value)
    while pending:
        name = pending.pop()
        if name in complete or name in BUILTIN_TYPES:
            continue
        complete.add(name)
        if name not in typedefs:
            fail("the family uses the type %s, which the header does not define" % name)
        if typedefs[name]["kind"] == "func":
            for argument in typedefs[name]["args"]:
                inner, pointer = base_type(argument)
                if not pointer and inner not in complete:
                    pending.append(inner)
    return complete, pointers


def emit_local_types(complete, pointers, typedefs):
    """The type block of the output.

    The opaque types come first, because the callback typedef names one of
    them."""
    lines = []
    for name in sorted(pointers):
        if name in complete:
            continue
        lines.append("typedef struct nvn_cb_%s %s;" % (name.lower(), name))
    lines.append("")
    for name in sorted(complete):
        typedef = typedefs[name]
        if typedef["kind"] == "struct" and name not in BY_VALUE_STRUCTS:
            fail("the family takes the struct %s by value, and its layout would be copied" % name)
        if typedef["kind"] == "scalar" and name not in BY_VALUE_SCALARS:
            fail("the family takes the type %s by value, and it is not one of the known scalars" % name)
        if typedef["kind"] == "func" and name != CALLBACK_TYPEDEF:
            fail("the family takes the function pointer %s by value" % name)
        for line in typedef["text"].splitlines():
            lines.append(line)
        lines.append("")
    return lines


def parameter_declaration(argument, name):
    """The declaration of one parameter, with the type of the header."""
    text = argument.strip()
    if text.endswith("*"):
        return "%s *%s" % (text[:-1].rstrip(), name)
    return "%s %s" % (text, name)


def recorded_expression(argument, expression, typedefs):
    """The expression that puts one argument into a record.

    The value of a narrow argument is only its own width: the upper bits of
    the register are not part of it. A float is recorded as its bit pattern,
    because a float has no integer representation that survives the trip."""
    name, pointer = base_type(argument)
    if pointer:
        return "(uint64_t)(uintptr_t)%s" % expression
    if name in ("NVNboolean", "uint8_t"):
        return "(uint64_t)(uint8_t)%s" % expression
    if name in WIDE_TYPES:
        return "(uint64_t)%s" % expression
    if name == "float":
        return "nvn_command_buffer_float_bits(%s)" % expression
    if name in ("int", "uint32_t", "int32_t", "uint16_t", "int16_t"):
        return "(uint64_t)(uint32_t)%s" % expression
    if name in BY_VALUE_STRUCTS:
        return "(uint64_t)%s.value" % expression
    if name == CALLBACK_TYPEDEF:
        return "(uint64_t)(uintptr_t)%s" % expression
    if name in typedefs and typedefs[name]["kind"] == "enum":
        return "(uint64_t)(uint32_t)%s" % expression
    fail("the family takes the type %s by value, and this generator does not know how to record it"
         % name)


def format_signature(ret, name, parameters):
    """The definition of an entry point, wrapped the way the runtime wraps its
    definitions."""
    prefix = "%s %s(" % (ret, name)
    indent = " " * len(prefix)
    suffix = ") {"
    # A name with a long return type cannot even hold its first parameter, so
    # the return type goes on a line of its own.
    if parameters and len(prefix) + len(parameters[0]) + len(suffix) > 100:
        prefix = "%s(" % name
        indent = " " * len(prefix)
        head = [ret]
    else:
        head = []
    packed = []
    current = prefix
    for index, parameter in enumerate(parameters):
        last = index + 1 == len(parameters)
        piece = parameter + ("" if last else ",")
        limit = 100 - (len(suffix) if last else 0)
        candidate = current + piece if current == prefix else current + " " + piece
        if len(candidate) > limit:
            packed.append(current)
            current = indent + piece
        else:
            current = candidate
    packed.append(current + suffix)
    return head + packed


def format_comment(text, marker="/* "):
    """A comment, wrapped to the width the runtime uses."""
    words = text.split()
    lines = []
    current = marker.rstrip()
    for word in words:
        candidate = current + (" " if current.strip() else "") + word
        if len(candidate) > 100 and current.strip():
            lines.append(current)
            current = " * " + word
        else:
            current = candidate
    lines.append(current + " */")
    return lines


def classify(name, typedef):
    """The class of every argument of an entry point.

    The three classes are the ones the task card names: a fixed size object,
    an array whose element size is known and whose count comes from the
    argument next to it, and a pointer whose size is not known. The last one
    is never dereferenced."""
    classes = []
    arguments = typedef["args"]
    for index, argument in enumerate(arguments):
        if index == 0:
            classes.append("buffer")
            continue
        kind, pointer = base_type(argument)
        if not pointer:
            classes.append("scalar")
            continue
        if kind in UNKNOWN_POINTER_TYPES:
            classes.append("unknown")
            continue
        if kind in ARRAY_ELEMENT_SIZES:
            classes.append("array")
            continue
        if kind in NUMBER_ARRAY_ELEMENT_SIZES:
            if name in NO_COUNT:
                classes.append("unknown")
                continue
            # An array of numbers is only sized when the count is next to it.
            previous = None
            if index >= 1:
                previous, previous_pointer = base_type(arguments[index - 1])
                if previous_pointer:
                    previous = None
            if previous in ("int", "uint32_t", "int32_t", "size_t"):
                classes.append("array")
                continue
            classes.append("unknown")
            continue
        if kind in OBJECT_SIZES:
            classes.append("object")
            continue
        fail("%s takes the pointer %s, and this generator does not know how big it is" % (name, kind))
    return classes


def check_classification(name, typedef, classes):
    """The array of an entry point takes its count from the argument next to
    it, and every class of an object is a type the header declares."""
    arguments = typedef["args"]
    for index, kind in enumerate(classes):
        if kind != "array":
            continue
        if index < 2:
            fail("%s takes an array as its %d argument, and it has no count next to it" % (name, index))
        previous, _ = base_type(arguments[index - 1])
        if previous not in ("int", "uint32_t", "int32_t", "size_t"):
            fail("%s takes an array as its %d argument, and the argument next to it is a %s, which "
                 "is not a count" % (name, index, previous))
    return True


def unresolved_mask(name, typedef, classes):
    """The mask of the arguments that are pointers whose size is not known.

    Those are the arguments that the runtime does not follow: the record
    names the pointer and says that the data behind it is not in the record."""
    mask = []
    for kind in classes:
        mask.append(1 if kind == "unknown" else 0)
    return mask


def emit_raw_wrapper(name, index):
    """An entry point whose signature the header does not give.

    The arguments are taken as plain words and written down as they came.
    Nothing is dereferenced, so a signature with fewer or more arguments
    cannot do any harm here. The entry point is reported as diagnostic only:
    the guest calls it already, so it must not trap, but it must not be
    reported as implemented either."""
    lines = []
    lines.extend(format_comment(
        "The signature of %s is not in the header this runtime was built against, so the "
        "arguments are taken as plain words and written down as they came. Nothing is "
        "dereferenced, so a signature with fewer or more arguments cannot do any harm "
        "here. The call is recorded in the diagnostic channel only: the format of the game "
        "this port runs has no record for a command whose shape is not known." % name))
    parameters = ["NVNcommandBuffer *commandBuffer"]
    for index_of in range(1, 8):
        parameters.append("uint64_t arg%d" % index_of)
    lines.extend(format_signature("void", name, parameters))
    lines.append("    uint64_t args[8];")
    lines.append("    args[0] = (uint64_t)(uintptr_t)commandBuffer;")
    for index_of in range(1, 8):
        lines.append("    args[%d] = arg%d;" % (index_of, index_of))
    lines.append('    nvn_command_buffer_record_diagnostic(commandBuffer, "%s", args, 8);' % name)
    lines.append("    NVN_VOID_RETURN();")
    lines.append("}")
    return lines


def emit_unmapped_wrapper(name, typedef, classes):
    """An entry point that the format of the game this port runs has no
    record for.

    The call is written down in the diagnostic channel with the arguments as
    they came. A pointer whose size is not known is marked as one that was
    not followed: the runtime does not dereference it and does not guess its
    size."""
    arguments = typedef["args"]
    count = len(arguments)
    lines = []
    lines.extend(format_comment(
        "The format of the game this port runs has no record for %s, so the call is not "
        "recorded in the command stream. It is written down in the diagnostic channel with "
        "the arguments as they came; a pointer whose size is not known is marked as one "
        "that was not followed." % name))
    parameters = []
    for position, argument in enumerate(arguments):
        parameter_name = "commandBuffer" if position == 0 else "arg%d" % position
        parameters.append(parameter_declaration(argument, parameter_name))
    lines.extend(format_signature(typedef["ret"], name, parameters))
    lines.append("    uint64_t args[%d];" % count)
    lines.append("    uint8_t unresolved[%d];" % count)
    for position in range(count):
        expression = recorded_expression(arguments[position], "commandBuffer" if position == 0
                                         else "arg%d" % position, TYPEDEF_BY_NAME)
        lines.append("    args[%d] = %s;" % (position, expression))
    for position, kind in enumerate(classes):
        lines.append("    unresolved[%d] = %d;" % (position, 1 if kind == "unknown" else 0))
    lines.append('    nvn_command_buffer_record_unmapped(commandBuffer, "%s", args, %d, '
                 'unresolved);' % (name, count))
    if typedef["ret"] == "void":
        lines.append("    NVN_VOID_RETURN();")
    lines.append("}")
    return lines


ARGUMENT_REFERENCE = re.compile(r"\barg(\d+)\b")


def emit_typed_wrapper(name, typedef, index):
    """An entry point whose signature the header gives.

    The signature is the one of the header, so that the compiler lays the
    arguments out the way the NVN calling convention wants them."""
    arguments = typedef["args"]
    parameters = []
    for position, argument in enumerate(arguments):
        parameter_name = "commandBuffer" if position == 0 else "arg%d" % position
        parameters.append(parameter_declaration(argument, parameter_name))
    body = []

    state = STATE_CALLS.get(name)
    if state:
        for line in state:
            body.append(line)
    recorder = RECORDERS.get(name)
    if recorder:
        function, values = recorder
        body.append("    %s(commandBuffer%s);" % (
            function, "".join(", %s" % value for value in values)))
    query = QUERY_CALLS.get(name)
    if query:
        for line in query:
            body.append(line)
        return format_signature(typedef["ret"], name, parameters) + body + ["}"]
    returns = RETURNS.get(name)
    if returns:
        for line in returns:
            body.append(line)
    elif typedef["ret"] == "void":
        body.append("    NVN_VOID_RETURN();")

    # An argument that the recorder does not need is named as unused, so
    # that the entry point still takes the call exactly as the guest made
    # it.
    text = "\n".join(body)
    for position in range(1, len(arguments)):
        if not re.search(r"\barg%d\b" % position, text):
            body.insert(0, "    (void)arg%d;" % position)
    return format_signature(typedef["ret"], name, parameters) + body + ["}"]


def build(names, signed, unsigned, typedefs, complete, pointers):
    """The whole generated file."""
    out = []
    out.append("/* The NVN command buffer family of the FFRS RG477M native runtime.")
    out.append(" *")
    out.append(" * This file is generated. Do not edit it; edit the generator instead:")
    out.append(" *")
    out.append(" *     python3 scripts/runtime/make_nvn_command_buffer_family.py")
    out.append(" *")
    out.append(" * The generator reads the names the guest resolves from")
    out.append(" * evidence/nvn/main_nvn_ids.txt and the signatures from the vendored NVN")
    out.append(" * header in src/graphics/nvn/include/nvn/nvn.h, and writes this file. The")
    out.append(" * output depends on nothing else, so the same inputs always give the same")
    out.append(" * file.")
    out.append(" *")
    out.append(" * Every entry point that the format of the game this port runs has a record")
    out.append(" * for writes that record; every other one is written down in the diagnostic")
    out.append(" * channel. The replay of the recorded commands does not exist yet; nothing")
    out.append(" * here pretends that a command was executed.")
    out.append(" */")
    out.append('#include "nvn_command_buffer.h"')
    out.append("")
    out.append('#include "nvn_device.h"')
    out.append("")
    out.append("#include <stddef.h>")
    out.append("#include <stdint.h>")
    out.append("")
    out.append("/* The types of the NVN header that this family needs.")
    out.append(" *")
    out.append(" * The SDK objects are only ever named by a pointer, so they stay")
    out.append(" * incomplete here: the layout of an NVNobject is a property of the")
    out.append(" * Nintendo SDK, which this runtime does not have. The enumerations and the")
    out.append(" * handle wrappers are handed over by value, so they are complete. */")
    out.extend(emit_local_types(complete, pointers, typedefs))
    out.append("")
    out.append("/* The sizes of the SDK objects that the recorders copy at the moment of")
    out.append(" * the call, as the vendored header declares them. The recorder itself does")
    out.append(" * not have the header, so the sizes travel with the call. */")
    for name in sorted(OBJECT_SIZES):
        out.append("#define NVN_CB_SIZE_%s 0x%X" % (name, OBJECT_SIZES[name]))
    for name in sorted(ARRAY_ELEMENT_SIZES):
        out.append("#define NVN_CB_SIZE_%s %d" % (name, ARRAY_ELEMENT_SIZES[name]))
    out.append("")
    out.append("/* The flag of a resolve copy, as the reference names it. */")
    out.append("#define NVN_CB_COPY_RESOLVE 0x40000000")
    out.append("")
    out.append("/* The entry points. */")

    for index, name in enumerate(names):
        out.append("")
        if name in unsigned:
            out.extend(emit_raw_wrapper(name, index))
        elif name in RECORDERS or name in STATE_CALLS or name in QUERY_CALLS:
            out.extend(emit_typed_wrapper(name, signed[name], index))
        else:
            out.extend(emit_unmapped_wrapper(name, signed[name], classify(name, signed[name])))

    out.append("")
    out.append("/* ------------------------------------------------------------------ */")
    out.append("/* The names                                                           */")
    out.append("/* ------------------------------------------------------------------ */")
    out.append("")
    out.append("static const nvn_command_buffer_family_entry s_command_buffer_family[] = {")
    for name in names:
        status = ("NVN_CB_ENTRY_DIAGNOSTIC_ONLY" if name in unsigned
                  else "NVN_CB_ENTRY_SIGNATURE_CONFIRMED")
        out.append('    { "%s",' % name)
        out.append("      (void *)(uintptr_t)%s," % name)
        out.append("      %s }," % status)
    out.append("};")
    out.append("")
    out.append("size_t nvn_command_buffer_family_count(void) {")
    out.append("    return sizeof(s_command_buffer_family) / sizeof(s_command_buffer_family[0]);")
    out.append("}")
    out.append("")
    out.append("const nvn_command_buffer_family_entry *nvn_command_buffer_family_entry_at(size_t index) {")
    out.append("    if (index >= nvn_command_buffer_family_count()) return NULL;")
    out.append("    return &s_command_buffer_family[index];")
    out.append("}")
    out.append("")
    return "\n".join(out)


TYPEDEF_BY_NAME = {}


def main():
    global TYPEDEF_BY_NAME
    if not HEADER_PATH.exists():
        fail("the NVN header is not at %s" % HEADER_PATH)
    if not NAMES_PATH.exists():
        fail("the guest name list is not at %s" % NAMES_PATH)

    typedefs = read_header()
    TYPEDEF_BY_NAME = typedefs
    names = read_family_names()
    signed, unsigned = select_signatures(names, typedefs)

    if len(names) != 156:
        fail("the guest resolves %d names of the command buffer family, and this generator "
             "was written for 156" % len(names))
    if sorted(unsigned) != sorted(RAW_NAMES):
        fail("the names without a typedef are %s, and this generator expects %s"
             % (sorted(unsigned), sorted(RAW_NAMES)))
    for name in names:
        if name in signed:
            count = len(signed[name]["args"])
            if count > 8:
                fail("%s takes %d arguments, and a record holds 8" % (name, count))

    # Every name of the family is in exactly one of the three places: it has
    # a recorder, it is a state call or a query, or it is recorded in the
    # diagnostic channel because the format of the game has no record for it.
    for name in names:
        if name in unsigned:
            continue
        if name in RECORDERS or name in STATE_CALLS or name in QUERY_CALLS:
            continue
        classes = classify(name, signed[name])
        check_classification(name, signed[name], classes)
        unresolved_mask(name, signed[name], classes)

    # The recorders take the arguments they need, and every argument they
    # take is an argument of the call.
    for name, (_, values) in RECORDERS.items():
        typedef = signed[name]
        count = len(typedef["args"])
        for value in values:
            for reference in ARGUMENT_REFERENCE.findall(value):
                if int(reference) >= count:
                    fail("%s takes %d arguments, and the recorder is given %s"
                         % (name, count, value))

    complete, pointers = collect_local_types(signed, typedefs)
    text = build(names, signed, unsigned, typedefs, complete, pointers)
    OUTPUT_PATH.write_text(text)
    recorded = len([name for name in names if name in RECORDERS])
    unmapped = len([name for name in names if name not in unsigned and name not in RECORDERS
                    and name not in STATE_CALLS and name not in QUERY_CALLS])
    sys.stdout.write("wrote %s: %d names, %d with a record of the game, %d diagnostic only, "
                     "%d without a record\n"
                     % (OUTPUT_PATH.relative_to(ROOT), len(names), recorded, len(unsigned), unmapped))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
