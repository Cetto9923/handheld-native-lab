/* The NVN command buffer recorder of the FFRS RG477M native runtime.
 *
 * An NVN command buffer is a recorder in the real SDK as well: the guest
 * records the commands it wants into the buffer's own memory, and the device
 * executes them later, when the buffer is submitted. So recording the calls
 * faithfully is the whole of this layer, and the replay that will read these
 * records does not exist yet.
 *
 * The state lives on the host and is keyed by the guest's command buffer
 * address. Nothing is written into the guest's command buffer object, because
 * the layout of an NVNcommandBuffer is a property of the Nintendo SDK, which
 * this runtime does not have.
 *
 * The answers that the API defines are given where the runtime can know
 * them: the size of the command memory is the size of the memory the guest
 * handed over, and the copy strides are the ones the guest set. How much of
 * the guest's command memory is already used cannot be known, because the
 * guest writes into that memory itself, so that answer is zero and the log
 * says so.
 */
#include "nvn_command_buffer.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* How many command buffers the runtime holds at once. The guest keeps a
 * handful, so this is a safety net rather than a limit the guest reaches. */
#define NVN_CB_MAX_BUFFERS 32

/* How many commands one buffer holds. A frame of the guest records a few
 * hundred, and the bound keeps a runaway guest from eating the host's
 * memory. */
#define NVN_CB_MAX_RECORDS 8192

/* The size a record list starts at. */
#define NVN_CB_INITIAL_RECORDS 64

typedef struct {
    void *object;         /* the address the guest gave for its buffer */
    int in_use;
    int finalized;
    int recording;
    int used_reported;    /* the honest "used" answer was already logged */
    uint64_t sequence;    /* the order the slots were taken in */
    size_t record_count;
    size_t record_capacity;
    nvn_recorded_command *records;
    size_t command_memory_size;
    size_t control_memory_size;
    int64_t copy_row_stride;
    int64_t copy_image_stride;
    uint64_t memory_callback;
    uint64_t memory_callback_data;
} nvn_cb_buffer;

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static nvn_cb_buffer s_buffers[NVN_CB_MAX_BUFFERS];
static nvn_command_buffer_stats s_stats;
static uint64_t s_sequence;
static uint64_t s_next_handle = 1;

/* The slot of one guest command buffer, taken fresh.
 *
 * The records of the slot that is given up are dropped with it: the slot
 * belongs to another buffer now, and the records of that one belong to it. */
static nvn_cb_buffer *buffer_reset(nvn_cb_buffer *buffer, const void *object) {
    free(buffer->records);
    memset(buffer, 0, sizeof(*buffer));
    buffer->object = (void *)(uintptr_t)object;
    buffer->in_use = 1;
    buffer->sequence = ++s_sequence;
    return buffer;
}

/* The slot of one guest command buffer.
 *
 * A slot of a finalized buffer is given up before a slot of a live one,
 * because the records of a finalized buffer have been read by whoever wanted
 * them. */
static nvn_cb_buffer *buffer_find(const void *object, int create) {
    size_t i;
    nvn_cb_buffer *free_slot = NULL;
    nvn_cb_buffer *oldest_finalized = NULL;
    nvn_cb_buffer *oldest = NULL;
    if (!object) return NULL;
    for (i = 0; i < NVN_CB_MAX_BUFFERS; i++) {
        nvn_cb_buffer *buffer = &s_buffers[i];
        if (!buffer->in_use) {
            if (!free_slot) free_slot = buffer;
            continue;
        }
        if (buffer->object == object) return buffer;
        if (!oldest || buffer->sequence < oldest->sequence) oldest = buffer;
        if (buffer->finalized &&
            (!oldest_finalized || buffer->sequence < oldest_finalized->sequence)) {
            oldest_finalized = buffer;
        }
    }
    if (!create) return NULL;
    if (free_slot) return buffer_reset(free_slot, object);
    if (oldest_finalized) return buffer_reset(oldest_finalized, object);
    (void)oldest;
    return NULL;
}

/* The slot of a buffer that the guest initializes.
 *
 * A buffer that is initialized again starts empty, because the commands of
 * the earlier run are not the commands of this one. */
static nvn_cb_buffer *buffer_prepare(const void *object) {
    nvn_cb_buffer *buffer = buffer_find(object, 0);
    if (buffer) {
        buffer->record_count = 0;
        buffer->finalized = 0;
        buffer->recording = 0;
        buffer->used_reported = 0;
        buffer->command_memory_size = 0;
        buffer->control_memory_size = 0;
        buffer->copy_row_stride = 0;
        buffer->copy_image_stride = 0;
        buffer->memory_callback = 0;
        buffer->memory_callback_data = 0;
        return buffer;
    }
    return buffer_find(object, 1);
}

/* The place of the next record of one buffer, growing the list if it is
 * full. Returns NULL when the bound of the list is reached. */
static nvn_recorded_command *record_slot(nvn_cb_buffer *buffer) {
    if (buffer->record_count >= buffer->record_capacity) {
        size_t capacity = buffer->record_capacity ? buffer->record_capacity * 2
                                                 : NVN_CB_INITIAL_RECORDS;
        nvn_recorded_command *records;
        if (capacity > NVN_CB_MAX_RECORDS) capacity = NVN_CB_MAX_RECORDS;
        if (capacity <= buffer->record_capacity) return NULL;
        records = realloc(buffer->records, capacity * sizeof(nvn_recorded_command));
        if (!records) return NULL;
        buffer->records = records;
        buffer->record_capacity = capacity;
    }
    return &buffer->records[buffer->record_count];
}

void nvn_command_buffer_record(uint64_t op, const void *buffer, const uint64_t *args,
                                uint32_t arg_count) {
    pthread_mutex_lock(&s_lock);
    s_stats.calls++;
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 1);
        if (!slot) {
            s_stats.refused++;
        } else {
            nvn_recorded_command *record = record_slot(slot);
            if (!record) {
                s_stats.dropped++;
            } else {
                uint32_t count = arg_count > 8 ? 8 : arg_count;
                record->op = op;
                record->arg_count = count;
                /* The slots the call did not fill are cleared, so that a
                 * record never carries the words of an earlier one. */
                memset(record->args, 0, sizeof(record->args));
                if (args && count > 0) memcpy(record->args, args, count * sizeof(uint64_t));
                slot->record_count++;
                s_stats.recorded++;
                s_stats.recorded_args += count;
            }
        }
    }
    pthread_mutex_unlock(&s_lock);
}

void nvn_command_buffer_log_raw(const char *name, const void *buffer, const uint64_t *args,
                                 uint32_t arg_count) {
    uint32_t i;
    pthread_mutex_lock(&s_lock);
    s_stats.raw_calls++;
    pthread_mutex_unlock(&s_lock);
    /* The first word of the record is the command buffer itself, which the
     * line already names. */
    fprintf(stderr, "[nvn] %s: buffer=%p", name, buffer);
    for (i = 1; i < arg_count && i < 8; i++) fprintf(stderr, " %" PRIx64, args[i]);
    fprintf(stderr, "; the signature of this entry point is not in the header this runtime was "
                    "built against, so the arguments are recorded as they came\n");
}

uint64_t nvn_command_buffer_float_bits(float value) {
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return (uint64_t)bits;
}

void nvn_command_buffer_set_result(uint64_t value) {
    /* The result of a call is handed over in x0. The entry point that needs
     * this declares nothing as its return type, so the value cannot be
     * handed over through the C return. */
    __asm__ volatile("mov x0, %0" : : "r"(value) : "x0");
}

int nvn_command_buffer_initialize(void *buffer) {
    int ok = 0;
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_prepare(buffer);
        if (slot) {
            s_stats.initialized++;
            ok = 1;
        } else {
            s_stats.refused++;
        }
    }
    pthread_mutex_unlock(&s_lock);
    if (ok) {
        fprintf(stderr, "[nvn] nvnCommandBufferInitialize: buffer=%p; the commands of this buffer "
                        "are recorded on the host\n",
                buffer);
    } else {
        fprintf(stderr, "[nvn] nvnCommandBufferInitialize: buffer=%p; the command buffer table is "
                        "full, so the buffer was not initialized\n",
                buffer);
    }
    return ok;
}

void nvn_command_buffer_finalize(void *buffer) {
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 0);
        if (slot) {
            /* The records stay, so that whoever wants them can still read
             * them after the guest gave the buffer up. The slot itself is
             * given up when the table needs the room. */
            slot->finalized = 1;
            slot->recording = 0;
            s_stats.finalized++;
        }
    }
    pthread_mutex_unlock(&s_lock);
}

void nvn_command_buffer_begin_recording(void *buffer) {
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 1);
        if (slot) slot->recording = 1;
    }
    pthread_mutex_unlock(&s_lock);
}

uint64_t nvn_command_buffer_end_recording(void *buffer) {
    uint64_t handle;
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 0);
        if (slot) slot->recording = 0;
        handle = s_next_handle++;
        s_stats.handles++;
    }
    pthread_mutex_unlock(&s_lock);
    fprintf(stderr, "[nvn] nvnCommandBufferEndRecording: buffer=%p -> handle %" PRIu64
                    "; this is a handle of the runtime and not a command of the device, because "
                    "nothing was executed\n",
            buffer, handle);
    return handle;
}

void nvn_command_buffer_add_command_memory(void *buffer, uint64_t size) {
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 1);
        if (slot) slot->command_memory_size += (size_t)size;
    }
    pthread_mutex_unlock(&s_lock);
}

void nvn_command_buffer_add_control_memory(void *buffer, uint64_t size) {
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 1);
        if (slot) slot->control_memory_size += (size_t)size;
    }
    pthread_mutex_unlock(&s_lock);
}

void nvn_command_buffer_set_copy_row_stride(void *buffer, int64_t stride) {
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 1);
        if (slot) slot->copy_row_stride = stride;
    }
    pthread_mutex_unlock(&s_lock);
}

void nvn_command_buffer_set_copy_image_stride(void *buffer, int64_t stride) {
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 1);
        if (slot) slot->copy_image_stride = stride;
    }
    pthread_mutex_unlock(&s_lock);
}

void nvn_command_buffer_set_memory_callback(void *buffer, uint64_t callback) {
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 1);
        if (slot) slot->memory_callback = callback;
    }
    pthread_mutex_unlock(&s_lock);
}

void nvn_command_buffer_set_memory_callback_data(void *buffer, uint64_t data) {
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 1);
        if (slot) slot->memory_callback_data = data;
    }
    pthread_mutex_unlock(&s_lock);
}

uint64_t nvn_command_buffer_get_command_memory_size(const void *buffer) {
    uint64_t size = 0;
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 0);
        if (slot) size = (uint64_t)slot->command_memory_size;
    }
    pthread_mutex_unlock(&s_lock);
    return size;
}

uint64_t nvn_command_buffer_get_command_memory_free(const void *buffer) {
    uint64_t size = 0;
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 0);
        if (slot) size = (uint64_t)slot->command_memory_size;
    }
    pthread_mutex_unlock(&s_lock);
    /* Nothing of the guest's command memory is known to be used, so all of
     * it is free as far as the runtime can tell. */
    return size;
}

uint64_t nvn_command_buffer_get_control_memory_size(const void *buffer) {
    uint64_t size = 0;
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 0);
        if (slot) size = (uint64_t)slot->control_memory_size;
    }
    pthread_mutex_unlock(&s_lock);
    return size;
}

uint64_t nvn_command_buffer_get_control_memory_free(const void *buffer) {
    uint64_t size = 0;
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 0);
        if (slot) size = (uint64_t)slot->control_memory_size;
    }
    pthread_mutex_unlock(&s_lock);
    return size;
}

int64_t nvn_command_buffer_get_copy_row_stride(const void *buffer) {
    int64_t stride = 0;
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 0);
        if (slot) stride = slot->copy_row_stride;
    }
    pthread_mutex_unlock(&s_lock);
    return stride;
}

int64_t nvn_command_buffer_get_copy_image_stride(const void *buffer) {
    int64_t stride = 0;
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 0);
        if (slot) stride = slot->copy_image_stride;
    }
    pthread_mutex_unlock(&s_lock);
    return stride;
}

uint64_t nvn_command_buffer_get_memory_callback(const void *buffer) {
    uint64_t callback = 0;
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 0);
        if (slot) callback = slot->memory_callback;
    }
    pthread_mutex_unlock(&s_lock);
    return callback;
}

uint64_t nvn_command_buffer_get_memory_callback_data(const void *buffer) {
    uint64_t data = 0;
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 0);
        if (slot) data = slot->memory_callback_data;
    }
    pthread_mutex_unlock(&s_lock);
    return data;
}

int nvn_command_buffer_is_recording(const void *buffer) {
    int recording = 0;
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 0);
        if (slot) recording = slot->recording;
    }
    pthread_mutex_unlock(&s_lock);
    return recording;
}

void nvn_command_buffer_report_used_is_unknown(const void *buffer, const char *what) {
    int report = 0;
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 0);
        if (slot && !slot->used_reported) {
            slot->used_reported = 1;
            report = 1;
            s_stats.honest_answers++;
        }
    }
    pthread_mutex_unlock(&s_lock);
    if (report) {
        fprintf(stderr, "[nvn] the guest asked how much of its %s it has already used; the guest "
                        "writes into that memory itself and the runtime cannot see how much of it "
                        "is written, so the answer is 0 and it is not a measurement\n",
                what);
    }
}

int nvn_command_buffer_get_records(void *buffer, const nvn_recorded_command **out, size_t *count) {
    int found = 0;
    if (out) *out = NULL;
    if (count) *count = 0;
    if (!buffer || !out || !count) return 0;
    pthread_mutex_lock(&s_lock);
    {
        nvn_cb_buffer *slot = buffer_find(buffer, 0);
        if (slot && slot->records && slot->record_count > 0) {
            *out = slot->records;
            *count = slot->record_count;
            found = 1;
        }
    }
    pthread_mutex_unlock(&s_lock);
    return found;
}

void nvn_command_buffer_get_stats(nvn_command_buffer_stats *out) {
    size_t i;
    if (!out) return;
    pthread_mutex_lock(&s_lock);
    *out = s_stats;
    out->live_buffers = 0;
    out->records = 0;
    for (i = 0; i < NVN_CB_MAX_BUFFERS; i++) {
        if (!s_buffers[i].in_use) continue;
        if (!s_buffers[i].finalized) out->live_buffers++;
        out->records += (int)s_buffers[i].record_count;
    }
    pthread_mutex_unlock(&s_lock);
}
