/* The contract of the queue submission seam.
 *
 * The seam is the place where the guest hands the commands it recorded over
 * to the runtime. The reference tree runs the command package of each handle
 * at this point (.local/refs/nxnative/dm/gfx/nvn/hle_nvn.c L1132, where the
 * handle h[i] is what is executed). This runtime's replay reads the package
 * instead of executing it, and this contract is what proves that the two
 * halves are actually joined.
 *
 * This case is in a translation unit of its own, with its own main, so that
 * two families can be worked on at the same time without writing into the
 * same file. The build script finds it by its name and runs it as a second
 * contract binary. The switch is derived from the file's name:
 * nvn_queue_submit_contract.c answers to --nvn-queue-submit.
 *
 * What this case can prove on the host:
 *   - the five names of this card resolve to one entry each, and the same
 *     entry every time they are resolved;
 *   - the seam is really joined: after nvnQueueSubmitCommands the replay
 *     holds one more package and more records than before. The proof is the
 *     replay's own counters, not a stub counter;
 *   - a handle that names no package is refused by name and counted;
 *   - the guest's queue object, its command buffer object and the array of
 *     handles are not written;
 *   - the wait on a sync without a fence answers 1 and is logged once;
 *   - the present does not report a success.
 *
 * What it cannot prove on the host: that a command reaches the GPU. The
 * replay reads the package and stops there; nothing is submitted to Vulkan,
 * so this contract says that the package was read and nothing more.
 */
#define _GNU_SOURCE
#include "../../src/runtime/hle/guest_process.h"
#include "../../src/graphics/nvn_device/nvn_command_buffer.h"
#include "../../src/graphics/nvn_device/nvn_command_replay.h"
#include "../../src/graphics/nvn_device/nvn_queue.h"
#include "../../src/graphics/nvn_device/nvn_texture_pool.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do {if(!(x)) {fprintf(stderr,"queue submit contract line=%d: %s\n",__LINE__,#x);return 1;}} while(0)

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
typedef void (*submit_fn)(void *, int, const uint64_t *);
typedef void (*present_fn)(void *, void *, int);
typedef void (*fence_fn)(void *, void *, int, int);
typedef int (*wait_fn)(void *, const void *);
typedef void *(*builder_get_memory_fn)(const void *);
typedef void *(*builder_set_queue_memory_fn)(void *, void *, uint64_t);
typedef uint64_t (*cb_initialize_fn)(void *, void *);
typedef void (*cb_add_command_memory_fn)(void *, void *, int64_t, uint64_t);
typedef void (*cb_begin_recording_fn)(void *);
typedef uint64_t (*cb_end_recording_fn)(void *);
typedef void (*cb_clear_fn)(void *, int, const float *, int);
typedef void (*cb_bind_vertex_buffer_fn)(void *, int, uint64_t, uint64_t);
typedef void (*cb_set_texture_pool_fn)(void *, const void *);
typedef void (*cb_bind_texture_fn)(void *, int, int, uint64_t);
typedef int (*tp_initialize_fn)(void *, const void *, long, int);

/* How many times the two threads submit at the same time. The overlap is a
 * matter of timing, so the check is repeated: on this host the overlap is
 * seen in about a tenth of the rounds without the submission lock, and in
 * none of them with it. */
#define QUEUE_SUBMIT_CONCURRENT_ROUNDS 200
/* How many textures each package binds after its pool. The more of them
 * there are, the longer the two runs overlap, and the overlap is what the
 * race needs. */
#define QUEUE_SUBMIT_BINDS_PER_PACKAGE 100

/* The five names this card owns. */
static const char *const s_names[5] = {
    "nvnQueueSubmitCommands",
    "nvnQueuePresentTexture",
    "nvnQueueFenceSync",
    "nvnQueueWaitSync",
    "nvnQueueBuilderGetMemory",
};

/* One submission, made from another thread. */
typedef struct submit_job {
    void *queue;
    uint64_t handle;
} submit_job;

static submit_fn s_submit;

static void *submit_thread(void *argument) {
    submit_job *job = (submit_job *)argument;
    uint64_t handles[1];
    handles[0] = job->handle;
    s_submit(job->queue, 1, handles);
    return NULL;
}

int main(int argc,char **argv) {
    if(argc!=2 || strcmp(argv[1],"--nvn-queue-submit")) {
        fprintf(stderr,"usage: %s --nvn-queue-submit\n",argv[0]);
        return 2;
    }
    bootstrap_fn bootstrap = (bootstrap_fn)(uintptr_t)guest_process_resolve(0,"nvnBootstrapLoader");
    CHECK(bootstrap);
    lookup_fn lookup = (lookup_fn)(uintptr_t)bootstrap("nvnDeviceGetProcAddress");
    CHECK(lookup);

    /* ---------------------------------------------------------------- */
    /* 1. The five names resolve, to one entry each, and the same entry   */
    /*    every time.                                                    */
    /* ---------------------------------------------------------------- */
    {
        void *entries[5];
        size_t i;
        size_t count;
        for(i=0;i<5;i++) {
            entries[i]=lookup(NULL,s_names[i]);
            expect_condition("queue-submit-the-name-resolves",entries[i]!=NULL);
            /* Resolving it a second time gives the same entry: the name
             * table of the family has one line for it and not two. */
            expect_condition("queue-submit-the-name-resolves-to-the-same-entry",
                             lookup(NULL,s_names[i])==entries[i]);
        }
        /* The family table holds every name exactly once. A name that is in
         * the table twice would resolve to the same entry as well, so the
         * table itself is walked. */
        count=nvn_queue_family_count();
        for(i=0;i<5;i++) {
            size_t j;
            size_t hits=0;
            for(j=0;j<count;j++) {
                const nvn_queue_family_entry *entry=nvn_queue_family_entry_at(j);
                if(entry&&entry->name&&!strcmp(entry->name,s_names[i])) hits++;
            }
            expect_value("queue-submit-the-name-is-in-the-family-table-once",hits,1);
        }
        /* And the entries the guest resolved are the ones the table holds:
         * the dispatch and the table cannot drift apart. */
        for(i=0;i<5;i++) {
            size_t j;
            int found=0;
            for(j=0;j<count;j++) {
                const nvn_queue_family_entry *entry=nvn_queue_family_entry_at(j);
                if(entry&&entry->name&&!strcmp(entry->name,s_names[i])&&entry->fn==entries[i]) found=1;
            }
            expect_condition("queue-submit-the-resolved-entry-is-the-table-entry",found);
        }
    }

    submit_fn submit = (submit_fn)(uintptr_t)lookup(NULL,"nvnQueueSubmitCommands");
    present_fn present = (present_fn)(uintptr_t)lookup(NULL,"nvnQueuePresentTexture");
    fence_fn fence = (fence_fn)(uintptr_t)lookup(NULL,"nvnQueueFenceSync");
    wait_fn wait_sync = (wait_fn)(uintptr_t)lookup(NULL,"nvnQueueWaitSync");
    builder_get_memory_fn builder_get_memory =
        (builder_get_memory_fn)(uintptr_t)lookup(NULL,"nvnQueueBuilderGetMemory");
    CHECK(submit&&present&&fence&&wait_sync&&builder_get_memory);
    s_submit = submit;

    /* The guest's own objects. They are filled with the 0xAA sentinel and
     * are checked for that sentinel at the end: nothing is written into the
     * guest's memory. */
    unsigned char queue_object[64];
    unsigned char buffer_object[64];
    unsigned char command_memory[16384];
    uint64_t handles[4];
    uint64_t unknown_handles[1];
    unsigned char handles_before[sizeof(handles)];
    memset(queue_object,0xaa,sizeof(queue_object));
    memset(buffer_object,0xaa,sizeof(buffer_object));
    memset(command_memory,0,sizeof(command_memory));

    /* ---------------------------------------------------------------- */
    /* 2. The seam. A real recording, its handle, and a submission.       */
    /* ---------------------------------------------------------------- */
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
    CHECK(initialize&&add_command_memory&&begin_recording&&end_recording&&clear&&bind_vertex_buffer);

    {
        float colour[4]={0.25f,0.5f,0.75f,1.0f};
        uint64_t handle;
        nvn_replay_stats before;
        nvn_replay_stats after;
        nvn_queue_stats queue_before;
        nvn_queue_stats queue_after;

        expect_value("queue-submit-the-buffer-initializes",(uint64_t)(uint32_t)initialize(buffer_object,NULL),1);
        add_command_memory(buffer_object,command_memory,0,sizeof(command_memory));
        begin_recording(buffer_object);
        clear(buffer_object,0,colour,0);
        bind_vertex_buffer(buffer_object,0,0x1000u,64u);
        handle=end_recording(buffer_object);
        expect_condition("queue-submit-the-recording-hands-out-a-handle",handle!=0);

        /* The counters of the replay before the submission. */
        nvn_command_replay_reset();
        nvn_command_replay_get_stats(&before);
        nvn_queue_get_stats(&queue_before);

        /* The guest's submission: the handle of the recording. */
        memset(handles,0,sizeof(handles));
        handles[0]=handle;
        memcpy(handles_before,handles,sizeof(handles));
        submit(queue_object,1,handles);

        nvn_command_replay_get_stats(&after);
        nvn_queue_get_stats(&queue_after);

        /* The proof of the seam: the replay holds one more package and more
         * records than it held before the submission. These are the
         * replay's own counters, not a counter of this contract. */
        expect_value("queue-submit-the-replay-read-one-more-package",
                     after.packages-before.packages,1);
        expect_condition("queue-submit-the-replay-read-more-records",
                         after.records>before.records);
        expect_condition("queue-submit-the-replay-read-the-clear",
                         after.clears>=1);
        expect_value("queue-submit-the-queue-handed-one-package-to-the-replay",
                     queue_after.packages_handed_to_replay-queue_before.packages_handed_to_replay,1);
        expect_condition("queue-submit-the-queue-counted-the-records",
                         queue_after.records_read>queue_before.records_read);
        expect_value("queue-submit-the-queue-refused-nothing",
                     queue_after.packages_refused-queue_before.packages_refused,0);
        /* The seam does not submit anything to the GPU: the present path is
         * not connected and the replay issues no Vulkan command. */
        expect_value("queue-submit-nothing-was-presented",
                     (uint64_t)(uint32_t)queue_after.presents_performed,0);
    }

    /* ---------------------------------------------------------------- */
    /* 3. A handle that names no package is refused and counted.          */
    /* ---------------------------------------------------------------- */
    {
        nvn_queue_stats before;
        nvn_queue_stats after;
        nvn_replay_stats replay_before;
        nvn_replay_stats replay_after;
        uint64_t unknown=0x5eed5eed5eed5eedULL;

        nvn_queue_get_stats(&before);
        nvn_command_replay_get_stats(&replay_before);

        memset(unknown_handles,0,sizeof(unknown_handles));
        unknown_handles[0]=unknown;
        submit(queue_object,1,unknown_handles);

        nvn_queue_get_stats(&after);
        nvn_command_replay_get_stats(&replay_after);

        /* It is refused and counted, not skipped silently. */
        expect_value("queue-submit-the-unknown-handle-is-refused-and-counted",
                     after.packages_refused-before.packages_refused,1);
        /* And it is not counted as submitted. */
        expect_value("queue-submit-the-unknown-handle-was-not-handed-to-the-replay",
                     after.packages_handed_to_replay-before.packages_handed_to_replay,0);
        /* The replay was not asked to read a package that is not there. */
        expect_value("queue-submit-the-replay-read-no-package-for-the-unknown-handle",
                     replay_after.packages-replay_before.packages,0);
        expect_value("queue-submit-the-replay-read-no-record-for-the-unknown-handle",
                     replay_after.records-replay_before.records,0);
    }

    /* ---------------------------------------------------------------- */
    /* 4. The 0xAA sentinel.                                              */
    /* ---------------------------------------------------------------- */
    {
        size_t i;
        int queue_clean=1;
        int buffer_clean=1;
        for(i=0;i<sizeof(queue_object);i++) if(queue_object[i]!=0xaa) queue_clean=0;
        for(i=0;i<sizeof(buffer_object);i++) if(buffer_object[i]!=0xaa) buffer_clean=0;
        expect_condition("queue-submit-the-queue-object-was-not-written",queue_clean);
        expect_condition("queue-submit-the-command-buffer-object-was-not-written",buffer_clean);
        /* The array of handles is the guest's too. */
        expect_condition("queue-submit-the-handle-array-was-not-written",
                         memcmp(handles_before,handles,sizeof(handles))==0);
    }

    /* ---------------------------------------------------------------- */
    /* 5. The sync objects.                                              */
    /* ---------------------------------------------------------------- */
    {
        unsigned char sync_object[64];
        unsigned char sync_object_2[64];
        nvn_queue_stats before;
        nvn_queue_stats after;

        memset(sync_object,0xaa,sizeof(sync_object));
        memset(sync_object_2,0xaa,sizeof(sync_object_2));

        /* A sync that was never fenced: the wait answers 1 all the same,
         * and it is logged once for the whole run. */
        nvn_queue_get_stats(&before);
        expect_value("queue-submit-the-wait-on-a-sync-without-a-fence-answers-one",
                     (uint64_t)(uint32_t)wait_sync(queue_object,sync_object),1);
        nvn_queue_get_stats(&after);
        expect_value("queue-submit-the-wait-was-counted",
                     after.waits-before.waits,1);
        expect_value("queue-submit-the-wait-on-a-sync-without-a-fence-was-counted",
                     after.waits_without_fence-before.waits_without_fence,1);
        {
            uint64_t logged=after.waits_without_fence_logged-before.waits_without_fence_logged;
            /* The log of a missing fence happens once for the whole run. The
             * first wait of this run may be the one that logs it, so the
             * number that was logged is at most one. */
            expect_condition("queue-submit-the-missing-fence-is-logged-at-most-once",logged<=1);
            expect_condition("queue-submit-the-missing-fence-was-logged",logged>=1);
        }
        /* A second wait on the same sync, and a wait on another sync that
         * was never fenced: neither of them logs again. */
        {
            uint64_t logged_before;
            nvn_queue_get_stats(&before);
            logged_before=before.waits_without_fence_logged;
            expect_value("queue-submit-the-second-wait-answers-one-too",
                         (uint64_t)(uint32_t)wait_sync(queue_object,sync_object),1);
            expect_value("queue-submit-the-wait-on-another-sync-without-a-fence-answers-one",
                         (uint64_t)(uint32_t)wait_sync(queue_object,sync_object_2),1);
            nvn_queue_get_stats(&after);
            expect_value("queue-submit-the-missing-fence-is-not-logged-again",
                         after.waits_without_fence_logged-logged_before,0);
        }

        /* A sync that was fenced: the wait answers 1, and it is not counted
         * as a wait without a fence. */
        nvn_queue_get_stats(&before);
        fence(queue_object,sync_object,0,0);
        nvn_queue_get_stats(&after);
        expect_value("queue-submit-the-fence-was-counted",
                     after.fences_taken-before.fences_taken,1);
        nvn_queue_get_stats(&before);
        expect_value("queue-submit-the-wait-on-a-fenced-sync-answers-one",
                     (uint64_t)(uint32_t)wait_sync(queue_object,sync_object),1);
        nvn_queue_get_stats(&after);
        expect_value("queue-submit-the-wait-on-a-fenced-sync-is-not-a-wait-without-a-fence",
                     after.waits_without_fence-before.waits_without_fence,0);
        /* The guest's sync object is not written. */
        {
            size_t i;
            int clean=1;
            for(i=0;i<sizeof(sync_object);i++) if(sync_object[i]!=0xaa) clean=0;
            expect_condition("queue-submit-the-sync-object-was-not-written",clean);
        }
    }

    /* ---------------------------------------------------------------- */
    /* 6. The present.                                                   */
    /* ---------------------------------------------------------------- */
    {
        nvn_queue_stats before;
        nvn_queue_stats after;
        unsigned char window_object[64];
        memset(window_object,0xaa,sizeof(window_object));

        nvn_queue_get_stats(&before);
        present(queue_object,window_object,0);
        nvn_queue_get_stats(&after);
        /* The request is recorded. */
        expect_value("queue-submit-the-present-request-was-recorded",
                     after.presents_requested-before.presents_requested,1);
        /* And nothing was presented: the present path is not connected yet,
         * so no success is reported. */
        expect_value("queue-submit-nothing-was-presented",
                     (uint64_t)(uint32_t)after.presents_performed,0);
        {
            size_t i;
            int clean=1;
            for(i=0;i<sizeof(window_object);i++) if(window_object[i]!=0xaa) clean=0;
            expect_condition("queue-submit-the-window-object-was-not-written",clean);
        }
    }

    /* ---------------------------------------------------------------- */
    /* 7. The builder memory.                                            */
    /* ---------------------------------------------------------------- */
    {
        unsigned char builder_object[64];
        unsigned char memory[256];
        builder_set_queue_memory_fn set_queue_memory =
            (builder_set_queue_memory_fn)(uintptr_t)lookup(NULL,"nvnQueueBuilderSetQueueMemory");
        CHECK(set_queue_memory);
        memset(builder_object,0xaa,sizeof(builder_object));
        memset(memory,0,sizeof(memory));
        set_queue_memory(builder_object,memory,sizeof(memory));
        expect_condition("queue-submit-the-builder-gives-back-the-memory-it-was-given",
                         builder_get_memory(builder_object)==memory);
        /* A builder that was never prepared has no memory, and NULL is said
         * rather than an address made up. */
        expect_condition("queue-submit-an-unknown-builder-has-no-memory",
                         builder_get_memory(NULL)==NULL);
        {
            size_t i;
            int clean=1;
            for(i=0;i<sizeof(builder_object);i++) if(builder_object[i]!=0xaa) clean=0;
            expect_condition("queue-submit-the-builder-object-was-not-written",clean);
        }
    }

/* ------------------------------------------------------------------ */
/* 5b. Two submissions at the same time.                                */
/* ------------------------------------------------------------------ */
/* The reference tree serializes the submissions for the same reason:
 * .local/refs/nxnative/dm/gfx/nvn/hle_nvn.c L1129, where the host stopped
 * with "submit re-entered" when a second thread submitted while the first
 * was still being replayed. The replay keeps one state for the stream that
 * is running - the pools it bound, the program it bound - and it clears that
 * state when a run begins. Two submissions that overlap clear each other's
 * state.
 *
 * Two threads submit two packages here, and each package binds its own
 * texture pool and then its own texture. When the two runs overlap, one of
 * them clears the other's pool binding, and the other's texture is then
 * refused as a handle of a pool that is not bound: the replay counts that
 * as a refusal of a foreign pool.
 *
 * So the number of foreign pool refusals is the thing this case checks. It
 * is zero when the submissions are serialized, and it is not zero when they
 * are not. The check is repeated, because the overlap is a matter of
 * timing and one run of it is not enough to see it. */
{
    unsigned char pool_a[64];
    unsigned char pool_b[64];
    unsigned char second_buffer[64];
    unsigned char second_memory[16384];
    uint64_t second_handle;
    pthread_t threads[2];
    submit_job jobs[2];
    nvn_queue_stats queue_before;
    nvn_queue_stats queue_after;
    tp_initialize_fn texture_pool_initialize =
        (tp_initialize_fn)(uintptr_t)lookup(NULL,"nvnTexturePoolInitialize");
    cb_set_texture_pool_fn set_texture_pool =
        (cb_set_texture_pool_fn)(uintptr_t)lookup(NULL,"nvnCommandBufferSetTexturePool");
    cb_bind_texture_fn bind_texture =
        (cb_bind_texture_fn)(uintptr_t)lookup(NULL,"nvnCommandBufferBindTexture");
    int round;
    int bind;

    CHECK(texture_pool_initialize&&set_texture_pool&&bind_texture);

    memset(pool_a,0xaa,sizeof(pool_a));
    memset(pool_b,0xaa,sizeof(pool_b));
    memset(second_buffer,0xaa,sizeof(second_buffer));
    memset(second_memory,0,sizeof(second_memory));

    /* Two pools with a numbering of their own, so that the texture of one
     * is not the texture of the other. */
    expect_value("queue-submit-the-first-pool-takes-the-numbering-from-zero",
                 (uint64_t)(uint32_t)texture_pool_initialize(pool_a,(const void *)pool_a,0,300),1);
    expect_value("queue-submit-the-second-pool-gets-a-numbering-of-its-own",
                 (uint64_t)(uint32_t)texture_pool_initialize(pool_b,(const void *)pool_a,0,10),1);

    /* The first package binds the first pool and then a texture of it. */
    expect_value("queue-submit-the-first-pool-binding-records",
                 (uint64_t)(uint32_t)initialize(buffer_object,NULL),1);
    add_command_memory(buffer_object,command_memory,0,sizeof(command_memory));
    begin_recording(buffer_object);
    set_texture_pool(buffer_object,pool_a);
    for (bind = 0; bind < QUEUE_SUBMIT_BINDS_PER_PACKAGE; bind++)
        bind_texture(buffer_object,0,0,5u);
    handles[0]=end_recording(buffer_object);
    expect_condition("queue-submit-the-first-recording-hands-out-a-handle",handles[0]!=0);

    /* The second package binds the second pool and then a texture of it. */
    expect_value("queue-submit-the-second-buffer-initializes",
                 (uint64_t)(uint32_t)initialize(second_buffer,NULL),1);
    add_command_memory(second_buffer,second_memory,0,sizeof(second_memory));
    begin_recording(second_buffer);
    set_texture_pool(second_buffer,pool_b);
    for (bind = 0; bind < QUEUE_SUBMIT_BINDS_PER_PACKAGE; bind++)
        bind_texture(second_buffer,0,0,5u);
    second_handle=end_recording(second_buffer);
    expect_condition("queue-submit-the-second-recording-hands-out-a-handle",second_handle!=0);

    /* The check, repeated. */
    for (round = 0; round < QUEUE_SUBMIT_CONCURRENT_ROUNDS; round++) {
        nvn_replay_stats stats;
        nvn_command_replay_reset();
        nvn_queue_get_stats(&queue_before);

        jobs[0].queue=queue_object;
        jobs[0].handle=handles[0];
        jobs[1].queue=queue_object;
        jobs[1].handle=second_handle;

        pthread_create(&threads[0],NULL,submit_thread,&jobs[0]);
        pthread_create(&threads[1],NULL,submit_thread,&jobs[1]);
        pthread_join(threads[0],NULL);
        pthread_join(threads[1],NULL);

        nvn_command_replay_get_stats(&stats);
        nvn_queue_get_stats(&queue_after);

        /* Both packages were read. A submission that is lost would be a
         * submission the guest made and the runtime dropped. */
        expect_value("queue-submit-the-replay-read-both-packages-of-two-threads",
                     stats.packages,2);
        expect_value("queue-submit-the-queue-handed-both-packages-to-the-replay",
                     queue_after.packages_handed_to_replay-queue_before.packages_handed_to_replay,2);
        expect_value("queue-submit-the-queue-refused-nothing-of-two-threads",
                     queue_after.packages_refused-queue_before.packages_refused,0);
        /* And the two runs did not clear each other's pool binding: no
         * texture was refused as a handle of a pool that is not bound. This
         * is the check that has teeth: without the submission lock this
         * number is not zero. */
        expect_value("queue-submit-the-two-threads-did-not-clear-each-others-pool",
                     stats.refused_foreign_pool,0);
    }
}

    {
        nvn_queue_stats final_stats;
        nvn_queue_get_stats(&final_stats);
        printf("{\"nvn_queue_submit_errors\":%u,\"submit_calls\":%llu,\"packages_handed_to_replay\":%llu,"
               "\"packages_refused\":%llu,\"records_read\":%llu,\"presents_requested\":%llu,"
               "\"presents_performed\":%llu,\"fences_taken\":%llu,\"waits\":%llu,"
               "\"waits_without_fence\":%llu,\"waits_without_fence_logged\":%llu}\n",
               errors,
               (unsigned long long)final_stats.submit_calls,
               (unsigned long long)final_stats.packages_handed_to_replay,
               (unsigned long long)final_stats.packages_refused,
               (unsigned long long)final_stats.records_read,
               (unsigned long long)final_stats.presents_requested,
               (unsigned long long)final_stats.presents_performed,
               (unsigned long long)final_stats.fences_taken,
               (unsigned long long)final_stats.waits,
               (unsigned long long)final_stats.waits_without_fence,
               (unsigned long long)final_stats.waits_without_fence_logged);
    }
    CHECK(!errors);
    return 0;
}
