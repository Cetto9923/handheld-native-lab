/* The contract of the command memory of the NVN command buffer.
 *
 * The guest records its commands into command memory that it hands over, and
 * when that memory is full the guest's own callback is asked for more. The
 * callback of the guest is guest code: it runs on the guest's own thread, it
 * is called through the guest thread boundary, and the address it lives at is
 * checked before it is called.
 *
 * This case is in a translation unit of its own, with its own main, so that
 * two families can be worked on at the same time without writing into the
 * same file. The build script finds it by its name and runs it as a second
 * contract binary.
 *
 * What this case proves on the host:
 *   - the recorder asks the guest for more command memory when the memory the
 *     guest handed over is full, and it hands the callback the four words the
 *     reference's callback takes;
 *   - a recording whose command memory ran out continues in a segment of the
 *     host's own memory and still ends in a handle, and the package of that
 *     recording holds the records in the order they were made;
 *   - a callback address that is not inside the text of the guest's module is
 *     refused by name and is not called;
 *   - an address that belongs to no registered memory pool is not turned into
 *     a host pointer, and a pool that cannot be created is refused by name.
 *
 * What it cannot prove on the host: the three checks of the real hook of the
 * boundary. The boundary is Android and AArch64 only, so on the host it
 * cannot be prepared and the real hook is never registered. The stand-in
 * below is the hook the boundary provides on the device: it applies the same
 * address check and it calls the guest's callback with the same four words.
 * The real hook is proven on the device, where the boundary exists. */
#define _GNU_SOURCE
#include "../../src/runtime/hle/guest_process.h"
#include "../../src/graphics/nvn_device/nvn_command_buffer.h"
#include "../../src/graphics/nvn_device/nvn_memory_pool.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do {if(!(x)) {fprintf(stderr,"command memory contract line=%d: %s\n",__LINE__,#x);return 1;}} while(0)

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

static int all_bytes(const void *memory,size_t size,unsigned char value) {
    const unsigned char *at=(const unsigned char *)memory;
    size_t i;
    for(i=0;i<size;i++)if(at[i]!=value)return 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* The stand-in of the guest thread boundary                           */
/* ------------------------------------------------------------------ */

/* The guest's callback of the command memory. The reference's callback takes
 * the command buffer, the event, the number of bytes needed and the callback
 * data the guest set. */
static uint64_t s_callback_calls;
static uint64_t s_callback_words[4];

static void guest_memory_callback(uint64_t buffer,uint64_t event,uint64_t size,uint64_t data) {
    s_callback_calls++;
    s_callback_words[0]=buffer;
    s_callback_words[1]=event;
    s_callback_words[2]=size;
    s_callback_words[3]=data;
}

/* The entry of the guest's module the stand-in knows. An address that is not
 * this one is not inside the text of the guest's module. */
static uint64_t s_guest_entry;
static unsigned s_callback_refused;

/* The hook of the guest thread boundary, as the boundary provides it on the
 * device. The address is checked before the guest is called, and a refusal is
 * named. */
static int stand_in_guest_call(void *entry,const uint64_t *x,uint64_t *out_x) {
    if((uint64_t)(uintptr_t)entry!=s_guest_entry) {
        s_callback_refused++;
        fprintf(stderr,"[nvn-callback] refused: the address 0x%" PRIx64 " of the memory callback is "
                "not inside the text of the guest's module; the memory callback of the guest was not "
                "called\n",(uint64_t)(uintptr_t)entry);
        return NVN_CB_GUEST_CALL_BAD_ADDRESS;
    }
    guest_memory_callback(x[0],x[1],x[2],x[3]);
    if(out_x)*out_x=0;
    return NVN_CB_GUEST_CALL_OK;
}

/* ------------------------------------------------------------------ */
/* The case                                                            */
/* ------------------------------------------------------------------ */

typedef void *(*bootstrap_fn)(const char *);
typedef void *(*lookup_fn)(void *,const char *);
typedef uint64_t (*cb_initialize_fn)(void *,void *);
typedef void (*cb_finalize_fn)(void *);
typedef void (*cb_begin_recording_fn)(void *);
typedef uint64_t (*cb_end_recording_fn)(void *);
typedef uint64_t (*cb_add_command_memory_fn)(void *,void *,int64_t,uint64_t);
typedef void (*cb_set_memory_callback_fn)(void *,uint64_t);
typedef void (*cb_set_memory_callback_data_fn)(void *,uint64_t);
typedef void (*cb_clear_fn)(void *,int,const float *,int);
typedef void (*cb_set_viewport_fn)(void *,int,int,int,int);
typedef void (*pool_builder_set_defaults_fn)(void *);
typedef void (*pool_builder_set_storage_fn)(void *,void *,uint64_t);
typedef uint64_t (*pool_initialize_fn)(void *,void *);

static int nvn_command_memory_contract(void) {
    nvn_command_buffer_stats stats;
    nvn_command_buffer_stats before;
    size_t i;

    /* 1. The entry points, resolved the way the guest resolves them: through
     *    the bootstrap loader and the lookup it hands out. */
    bootstrap_fn bootstrap = (bootstrap_fn)(uintptr_t)guest_process_resolve(0,"nvnBootstrapLoader");
    CHECK(bootstrap);
    lookup_fn lookup = (lookup_fn)(uintptr_t)bootstrap("nvnDeviceGetProcAddress");
    CHECK(lookup);
    cb_initialize_fn initialize = (cb_initialize_fn)(uintptr_t)lookup(NULL,"nvnCommandBufferInitialize");
    cb_finalize_fn finalize = (cb_finalize_fn)(uintptr_t)lookup(NULL,"nvnCommandBufferFinalize");
    cb_begin_recording_fn begin_recording =
        (cb_begin_recording_fn)(uintptr_t)lookup(NULL,"nvnCommandBufferBeginRecording");
    cb_end_recording_fn end_recording =
        (cb_end_recording_fn)(uintptr_t)lookup(NULL,"nvnCommandBufferEndRecording");
    cb_add_command_memory_fn add_command_memory =
        (cb_add_command_memory_fn)(uintptr_t)lookup(NULL,"nvnCommandBufferAddCommandMemory");
    cb_set_memory_callback_fn set_memory_callback =
        (cb_set_memory_callback_fn)(uintptr_t)lookup(NULL,"nvnCommandBufferSetMemoryCallback");
    cb_set_memory_callback_data_fn set_memory_callback_data =
        (cb_set_memory_callback_data_fn)(uintptr_t)lookup(NULL,"nvnCommandBufferSetMemoryCallbackData");
    cb_clear_fn clear = (cb_clear_fn)(uintptr_t)lookup(NULL,"nvnCommandBufferClearColor");
    cb_set_viewport_fn set_viewport =
        (cb_set_viewport_fn)(uintptr_t)lookup(NULL,"nvnCommandBufferSetViewport");
    CHECK(initialize&&finalize&&begin_recording&&end_recording&&add_command_memory);
    CHECK(set_memory_callback&&set_memory_callback_data&&clear&&set_viewport);

    /* The stand-in of the boundary takes the place of the hook the boundary
     * registers on the device. */
    s_guest_entry = (uint64_t)(uintptr_t)&guest_memory_callback;
    nvn_command_buffer_set_guest_call(stand_in_guest_call);

    /* 2. A command buffer of the guest, with a command memory that is far too
     *    small for the records of the runtime: the records of this runtime
     *    are larger than the commands of a real device, so the budget the
     *    guest reserved runs out early. That is the case the reference
     *    catches with a segment of the host's own memory. */
    unsigned char buffer_object[64];
    memset(buffer_object,0xaa,sizeof(buffer_object));
    expect_value("command-memory-initialize-succeeds",initialize(buffer_object,NULL),1);
    expect_condition("command-memory-object-not-written",
                     all_bytes(buffer_object,sizeof(buffer_object),0xaa));

    static unsigned char command_memory[64];
    memset(command_memory,0x11,sizeof(command_memory));
    add_command_memory(buffer_object,command_memory,0,sizeof(command_memory));

    /* The guest's callback and the data it set. The callback data is a word
     * of the guest's own, and the recorder has to hand it back unchanged. */
    const uint64_t callback_data = 0x1234567890abcdefULL;
    set_memory_callback(buffer_object,s_guest_entry);
    set_memory_callback_data(buffer_object,callback_data);

    begin_recording(buffer_object);

    /* How many records there are before the commands below. The number is
     * not written down here, so that the check afterwards does not depend on
     * how many commands the family records on its own. */
    const nvn_recorded_command *records = NULL;
    size_t record_count = 0;
    expect_value("command-memory-has-records",
                 (uint64_t)(uint32_t)nvn_command_buffer_get_records(buffer_object,&records,&record_count),1);
    const size_t records_before = record_count;

    /* 3. The commands. The colour lives in the guest's own memory, and the
     *    guest may write over it the moment the call returns. */
    float clear_color[4] = {0.25f,0.5f,0.75f,1.0f};
    clear(buffer_object,0,clear_color,0);
    clear(buffer_object,1,clear_color,0);
    set_viewport(buffer_object,0,0,1280,720);

    records = NULL;
    record_count = 0;
    expect_value("command-memory-has-records-again",
                 (uint64_t)(uint32_t)nvn_command_buffer_get_records(buffer_object,&records,&record_count),1);
    expect_value("command-memory-the-commands-were-recorded",
                 (uint64_t)record_count,(uint64_t)(records_before+3));

    /* 4. The guest's callback was called with the four words the reference's
     *    callback takes: the command buffer, the event, the number of bytes
     *    needed and the callback data the guest set. */
    expect_condition("command-memory-the-guest-callback-was-called",s_callback_calls>0);
    expect_value("command-memory-the-callback-got-the-command-buffer",
                 s_callback_words[0],(uint64_t)(uintptr_t)buffer_object);
    expect_value("command-memory-the-callback-got-the-out-of-memory-event", s_callback_words[1], 0);
    expect_condition("command-memory-the-callback-got-a-size",s_callback_words[2]>0);
    expect_value("command-memory-the-callback-got-the-callback-data",
                 s_callback_words[3],callback_data);

    /* 5. The recording still ends in a handle: a recording is never void
     *    because of memory. */
    uint64_t handle = end_recording(buffer_object);
    expect_condition("command-memory-end-recording-hands-out-a-handle",handle!=0);

    nvn_command_package package;
    memset(&package,0,sizeof(package));
    expect_value("command-memory-handle-names-a-package",
                 (uint64_t)(uint32_t)nvn_command_buffer_get_package(handle,&package),1);
    expect_condition("command-memory-the-package-has-records",package.record_count>0);
    expect_condition("command-memory-the-package-has-bytes",package.bytes>0);

    /* 6. The package holds the records in the order they were made, and it
     *    says which memory every part of it is in. The records are read with
     *    the structures of the reference tree, so a record written here is
     *    read with the reference's own structures. */
    expect_condition("command-memory-the-package-has-several-segments",package.segment_count>1);
    {
        size_t segment_records = 0;
        size_t bytes = 0;
        int host_records = 0;
        for(i=0;i<package.segment_count;i++) {
            expect_condition("command-memory-a-segment-is-inside-the-package",
                             package.segments[i].offset+package.segments[i].bytes<=package.bytes);
            segment_records += package.segments[i].record_count;
            bytes += package.segments[i].bytes;
            if(package.segments[i].host)host_records++;
        }
        expect_value("command-memory-the-segments-hold-all-the-records",
                     (uint64_t)segment_records,(uint64_t)package.record_count);
        expect_value("command-memory-the-segments-hold-all-the-bytes",
                     (uint64_t)bytes,(uint64_t)package.bytes);
        expect_condition("command-memory-a-host-segment-was-used",host_records>0);
    }
    {
        /* The order of the records: the opcodes the guest produced, in the
         * order it produced them, and the end of the recording last. */
        uint32_t expected[4];
        size_t expected_count = 0;
        expected[expected_count++] = NVN_CB_OP_CLEAR_COLOR;
        expected[expected_count++] = NVN_CB_OP_CLEAR_COLOR;
        expected[expected_count++] = NVN_CB_OP_VIEWPORTS;
        expected[expected_count++] = NVN_CB_OP_END;
        const uint8_t *at = package.records;
        size_t left = package.bytes;
        size_t seen = 0;
        while(left>=sizeof(nvn_cb_op_hdr)) {
            nvn_cb_op_hdr header;
            memcpy(&header,at,sizeof(header));
            if(seen<expected_count) {
                expect_value("command-memory-the-records-are-in-order",
                             (uint64_t)header.op,(uint64_t)expected[seen]);
            }
            if(header.size<sizeof(nvn_cb_op_hdr)||header.size>left)break;
            at += header.size;
            left -= header.size;
            seen++;
        }
        expect_value("command-memory-the-package-holds-every-record",
                     (uint64_t)seen,(uint64_t)package.record_count);
    }

    /* 7. A record knows which memory it is in. The first records are in the
     *    command memory the guest handed over, the later ones are in a
     *    segment of the host's own memory. */
    {
        /* The reference's recording ends with one end record, and it is a
         * record like any other. */
        const size_t end_record = 1;
        const nvn_recorded_command *again = NULL;
        size_t again_count = 0;
        expect_value("command-memory-the-records-are-still-there",
                     (uint64_t)(uint32_t)nvn_command_buffer_get_records(buffer_object,&again,&again_count),1);
        expect_value("command-memory-the-record-count-is-unchanged",
                     (uint64_t)again_count,(uint64_t)(records_before+3+end_record));
        if(again&&again_count>=3) {
            expect_value("command-memory-the-first-record-is-in-the-guest-memory",
                         (uint64_t)again[0].segment,0);
            expect_condition("command-memory-the-last-record-is-in-a-host-segment",
                             again[again_count-1].segment>0);
        }
    }

    /* 8. A callback address that is not inside the text of the guest's module
     *    is refused by name and is not called, and the recording still ends
     *    in a handle. */
    unsigned char second_buffer[64];
    memset(second_buffer,0xaa,sizeof(second_buffer));
    expect_value("command-memory-second-initialize-succeeds",initialize(second_buffer,NULL),1);
    add_command_memory(second_buffer,command_memory,0,sizeof(command_memory));
    const uint64_t bogus_entry = 0x7ffff00000000000ULL;
    set_memory_callback(second_buffer,bogus_entry);
    set_memory_callback_data(second_buffer,callback_data);

    nvn_command_buffer_get_stats(&before);
    const uint64_t calls_before = s_callback_calls;
    const unsigned refused_before = s_callback_refused;
    const uint64_t released_before = before.host_segments_released;
    begin_recording(second_buffer);
    clear(second_buffer,0,clear_color,0);
    clear(second_buffer,1,clear_color,0);
    /* The guest hands out command memory again while the recording runs in a
     * segment of the host's memory: the segment that took over the range of
     * the guest's command memory is given back. */
    add_command_memory(second_buffer,command_memory,0,8);
    uint64_t second_handle = end_recording(second_buffer);
    nvn_command_buffer_get_stats(&stats);

    expect_condition("command-memory-the-bogus-callback-was-refused",
                     s_callback_refused>refused_before);
    expect_value("command-memory-the-bogus-callback-was-not-called",
                 s_callback_calls,calls_before);
    expect_condition("command-memory-the-bogus-address-was-counted",
                     stats.callback_refused_address>before.callback_refused_address);
    expect_condition("command-memory-a-refused-callback-still-ends-in-a-handle",
                     second_handle!=0);
    expect_condition("command-memory-the-segment-was-given-back",
                     stats.host_segments_released>released_before);

    /* 9. An address that belongs to no registered memory pool is not turned
     *    into a host pointer, and a pool that cannot be created is refused by
     *    name. */
    {
        pool_builder_set_defaults_fn pool_set_defaults =
            (pool_builder_set_defaults_fn)(uintptr_t)lookup(NULL,"nvnMemoryPoolBuilderSetDefaults");
        pool_builder_set_storage_fn pool_set_storage =
            (pool_builder_set_storage_fn)(uintptr_t)lookup(NULL,"nvnMemoryPoolBuilderSetStorage");
        pool_initialize_fn pool_initialize =
            (pool_initialize_fn)(uintptr_t)lookup(NULL,"nvnMemoryPoolInitialize");
        CHECK(pool_set_defaults&&pool_set_storage&&pool_initialize);
        nvn_memory_pool_stats pool_stats;
        unsigned char pool_builder[64];
        unsigned char pool_object[64];
        memset(pool_builder,0xaa,sizeof(pool_builder));
        memset(pool_object,0xaa,sizeof(pool_object));
        pool_set_defaults(pool_builder);
        pool_set_storage(pool_builder,command_memory,sizeof(command_memory));
        nvn_memory_pool_get_stats(&pool_stats);
        /* A pool without a device is memory that no graphics device can use,
         * so it is refused and the refusal is named. */
        expect_value("command-memory-a-pool-without-a-device-is-refused",
                     (uint64_t)(uint32_t)pool_initialize(pool_object,pool_builder),0);
        nvn_memory_pool_get_stats(&pool_stats);
        expect_condition("command-memory-the-refused-pool-was-counted",
                         pool_stats.refused>0);
        /* An address that belongs to no registered pool has no host address:
         * the runtime does not turn a number the guest made up into a
         * pointer. */
        {
            size_t available = 12345;
            void *resolved = nvn_memory_pool_resolve_address(0x1234567890abcdefULL,&available);
            expect_condition("command-memory-an-address-of-no-pool-is-not-resolved",resolved==NULL);
            expect_value("command-memory-an-address-of-no-pool-has-no-bytes",
                         (uint64_t)available,0);
        }
    }

    /* 10. The counters. */
    nvn_command_buffer_get_stats(&stats);
    expect_condition("command-memory-the-stats-count-the-callbacks",stats.callback_calls>0);
    expect_condition("command-memory-the-stats-count-the-host-segments",stats.host_segments>0);
    expect_condition("command-memory-the-stats-count-the-packages",stats.packages>0);
    expect_condition("command-memory-the-stats-count-the-handles",stats.handles>0);
    expect_value("command-memory-no-recording-is-void",stats.void_recordings,0);
    expect_condition("command-memory-the-host-segments-are-bounded",
                     stats.host_segment_bound>0&&stats.host_segment_bytes<=stats.host_segment_bound);

    finalize(buffer_object);
    finalize(second_buffer);

    /* 11. The bound of the memory the host lends. The recording goes on until
     *     the recorder refuses, and the refusal is named: the reason of a
     *     void recording is about a command that does not fit, and not about
     *     memory that ran out. This is the last check of the case: the pool
     *     of the host is empty while it runs. */
    {
        unsigned char third_buffer[64];
        memset(third_buffer,0xaa,sizeof(third_buffer));
        expect_value("command-memory-third-initialize-succeeds",initialize(third_buffer,NULL),1);
        add_command_memory(third_buffer,command_memory,0,sizeof(command_memory));
        set_memory_callback(third_buffer,s_guest_entry);
        set_memory_callback_data(third_buffer,callback_data);
        begin_recording(third_buffer);
        nvn_command_buffer_get_stats(&before);
        size_t written = 0;
        while(written<200000u) {
            set_viewport(third_buffer,0,0,1280,720);
            written++;
            nvn_command_buffer_get_stats(&stats);
            if(stats.host_segment_refused>before.host_segment_refused)break;
        }
        uint64_t third_handle = end_recording(third_buffer);
        nvn_command_buffer_get_stats(&stats);
        expect_condition("command-memory-the-bound-of-the-lent-memory-is-named",
                         stats.host_segment_refused>before.host_segment_refused);
        expect_condition("command-memory-the-bound-makes-the-recording-void",
                         stats.void_recordings>before.void_recordings);
        expect_condition("command-memory-the-reason-is-about-a-command-that-does-not-fit",
                         stats.void_reason!=NULL&&strstr(stats.void_reason,"does not fit")!=NULL);
        expect_value("command-memory-a-void-recording-hands-out-no-handle",third_handle,0);
        finalize(third_buffer);
    }

    /* The final gate. Without it the contract could exit 0 while the
     * expectations had failed. */
    if(errors) {
        fprintf(stderr,"[check-fail] the command memory contract found %u failed expectations\n",errors);
        return 1;
    }
    printf("{\"nvn_command_memory_errors\":%u,\"packages\":%llu,\"callback_calls\":%llu,"
           "\"host_segments\":%llu,\"host_segment_bytes\":%llu,\"host_segment_bound\":%llu,"
           "\"records\":%llu,\"refused_address\":%llu}\n",
           errors,(unsigned long long)stats.packages,(unsigned long long)stats.callback_calls,
           (unsigned long long)stats.host_segments,(unsigned long long)stats.host_segment_bytes,
           (unsigned long long)stats.host_segment_bound,(unsigned long long)stats.recorded,
           (unsigned long long)stats.callback_refused_address);
    return 0;
}

int main(int argc,char **argv) {
    if(argc==1||(argc==2&&!strcmp(argv[1],"--nvn-command-memory"))) {
        return nvn_command_memory_contract();
    }
    fprintf(stderr,"usage: %s --nvn-command-memory\n",argc>0?argv[0]:"nvn_command_memory_contract");
    return 2;
}
