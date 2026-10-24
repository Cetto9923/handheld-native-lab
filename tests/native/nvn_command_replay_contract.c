/* The contract of the NVN command replay.
 *
 * The replay is the consumer of the command packages: it reads a package
 * with the structures of the reference tree, it keeps the state that a
 * command stream carries from one command to the next, and it turns the
 * addresses the records carry into the host memory behind them.
 *
 * This case is in a translation unit of its own, with its own main, so that
 * two families can be worked on at the same time without writing into the
 * same file. The build script finds it by its name and runs it as a second
 * contract binary.
 *
 * What this case can prove on the host:
 *   - the replay reads a package that the recorder really produced, and it
 *     sees the same commands the recorder was given;
 *   - a handle that the registry does not know is refused and counted, and
 *     nothing is dereferenced;
 *   - an address that belongs to no pool of the guest is refused by name.
 *
 * What it cannot prove on the host: the resolution of an address against a
 * real pool. The host build has no Vulkan device, so the guest's memory pool
 * refuses to be created, and there is no pool to resolve against. That part
 * is proven on the device, where the pool is real.
 */
#define _GNU_SOURCE
#include "../../src/runtime/hle/guest_process.h"
#include "../../src/graphics/nvn_device/nvn_command_buffer.h"
#include "../../src/graphics/nvn_device/nvn_command_replay.h"
#include "../../src/graphics/nvn_device/nvn_memory_pool.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do {if(!(x)) {fprintf(stderr,"replay contract line=%d: %s\n",__LINE__,#x);return 1;}} while(0)

static unsigned errors;

static void expect_value(const char *tag,uint64_t actual,uint64_t expected) {
    if(actual!=expected) {
        errors++;
        fprintf(stderr,"[check-fail] %s actual=0x%" PRIx64 " expected=0x%" PRIx64 "\n",tag,actual,expected);
    }
}

static void expect_condition(const char *tag,int condition) {
    if(!condition) {errors++;fprintf(stderr,"[check-fail] %s\n",tag);}
}

typedef void *(*bootstrap_fn)(const char *);
typedef void *(*lookup_fn)(void *, const char *);
typedef uint64_t (*cb_initialize_fn)(void *, void *);
typedef void (*cb_add_command_memory_fn)(void *, void *, int64_t, uint64_t);
typedef void (*cb_begin_recording_fn)(void *);
typedef uint64_t (*cb_end_recording_fn)(void *);
typedef void (*cb_clear_fn)(void *, int, const float *, int);
typedef void (*cb_bind_vertex_buffer_fn)(void *, int, uint64_t, uint64_t);
typedef void (*cb_bind_texture_fn)(void *, int, int, uint64_t);

int main(int argc,char **argv) {
    (void)argc;(void)argv;
    bootstrap_fn bootstrap = (bootstrap_fn)(uintptr_t)guest_process_resolve(0,"nvnBootstrapLoader");
    CHECK(bootstrap);
    lookup_fn lookup = (lookup_fn)(uintptr_t)bootstrap("nvnDeviceGetProcAddress");
    CHECK(lookup);

    cb_initialize_fn initialize = (cb_initialize_fn)(uintptr_t)lookup(NULL,"nvnCommandBufferInitialize");
    cb_add_command_memory_fn add_command_memory =
        (cb_add_command_memory_fn)(uintptr_t)lookup(NULL,"nvnCommandBufferAddCommandMemory");
    cb_begin_recording_fn begin_recording =
        (cb_begin_recording_fn)(uintptr_t)lookup(NULL,"nvnCommandBufferBeginRecording");
    cb_end_recording_fn end_recording =
        (cb_end_recording_fn)(uintptr_t)lookup(NULL,"nvnCommandBufferEndRecording");
    cb_clear_fn clear = (cb_clear_fn)(uintptr_t)lookup(NULL,"nvnCommandBufferClearColor");
    cb_bind_vertex_buffer_fn bind_vertex_buffer =
        (cb_bind_vertex_buffer_fn)(uintptr_t)lookup(NULL,"nvnCommandBufferBindVertexBuffer");
    cb_bind_texture_fn bind_texture =
        (cb_bind_texture_fn)(uintptr_t)lookup(NULL,"nvnCommandBufferBindTexture");
    nvn_replay_stats final_stats;
    CHECK(initialize&&begin_recording&&end_recording&&clear&&bind_vertex_buffer&&bind_texture);

    /* 1. A recording that the guest makes. The commands are the ones the
     *    guest would make: a clear with a colour of its own, a vertex buffer
     *    at an address, and a texture handle. */
    unsigned char buffer_object[64];
    unsigned char command_memory[4096];
    float colour[4] = {0.25f,0.5f,0.75f,1.0f};
    uint64_t handle;
    memset(buffer_object,0xaa,sizeof(buffer_object));
    memset(command_memory,0,sizeof(command_memory));
    expect_value("replay-initialize-succeeds",initialize(buffer_object,NULL),1);
    /* The guest hands over the command memory it wants the recording in.
     * Without it the recorder has no room and records nothing. */
    add_command_memory(buffer_object,command_memory,0,sizeof(command_memory));
    begin_recording(buffer_object);
    clear(buffer_object,0,colour,0);
    bind_vertex_buffer(buffer_object,0,0x1000u,64u);
    /* A texture handle is not a pointer: it is the texture's number in the
     * low twenty bits. The stream has no texture pool bound, so the replay
     * has to refuse it by name. */
    bind_texture(buffer_object,0,0,0x1234u);
    handle = end_recording(buffer_object);
    expect_condition("replay-end-recording-hands-out-a-handle",handle!=0);

    /* 2. The replay reads the package. */
    nvn_command_replay_reset();
    expect_value("replay-reads-the-package",(uint64_t)(uint32_t)nvn_command_replay_run(handle),1);
    {
        nvn_replay_stats stats;
        nvn_replay_state state;
        nvn_command_replay_get_stats(&stats);
        nvn_command_replay_get_state(&state);
        expect_condition("replay-read-at-least-one-record",stats.records>=1);
        expect_condition("replay-counted-the-clear",stats.clears>=1);
        expect_condition("replay-counted-the-package",stats.packages>=1);
        expect_value("replay-refused-no-unknown-handle",stats.refused_unknown_handle,0);
        expect_condition("replay-refused-the-address-of-no-pool",stats.refused_no_pool>=1);
        expect_condition("replay-refused-the-texture-of-no-pool",stats.refused_foreign_pool>=1);
        expect_condition("replay-bound-no-texture-without-a-pool",stats.textures_bound==0);
        expect_value("replay-keeps-no-program",state.program,0);
        expect_value("replay-keeps-no-render-target",state.render_targets,0);
    }

    /* 3. A handle the registry does not know is refused, and nothing is
     *    dereferenced. */
    {
        nvn_replay_stats before;
        nvn_replay_stats after;
        nvn_command_replay_get_stats(&before);
        expect_value("replay-refuses-a-handle-it-does-not-know",
                     (uint64_t)(uint32_t)nvn_command_replay_run(0x5eed5eed5eed5eedULL),0);
        nvn_command_replay_get_stats(&after);
        expect_value("replay-counted-the-unknown-handle",after.refused_unknown_handle,
                     before.refused_unknown_handle+1);
        expect_value("replay-read-no-record-for-the-unknown-handle",after.records,before.records);
    }

    /* 4. The replay does not walk into the guest's memory. The handle is the
     *    only thing it is given, and the address in a record is the guest's
     *    buffer address, which is not a host pointer. */
    {
        nvn_replay_stats stats;
        nvn_command_replay_get_stats(&stats);
        expect_condition("replay-resolved-no-address-on-the-host",stats.resolved_addresses==0);
    }

    nvn_command_replay_get_stats(&final_stats);

    printf("{\"nvn_replay_errors\":%u,\"packages\":%llu,\"records\":%llu,\"clears\":%llu,"
           "\"refused_unknown_handle\":%llu,\"refused_no_pool\":%llu,\"refused_foreign_pool\":%llu}\n",
           errors,(unsigned long long)final_stats.packages,(unsigned long long)final_stats.records,
           (unsigned long long)final_stats.clears,
           (unsigned long long)final_stats.refused_unknown_handle,
           (unsigned long long)final_stats.refused_no_pool,
           (unsigned long long)final_stats.refused_foreign_pool);
    return errors?1:0;
}
