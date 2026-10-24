/* The NVN command buffer family of the FFRS RG477M native runtime.
 *
 * An NVN command buffer is a recorder in the real SDK as well: the guest
 * records the commands it wants into the buffer's own memory, and the device
 * executes them later, when the buffer is submitted. Recording the calls
 * faithfully is therefore the whole of this layer; the replay comes later.
 *
 * As with the queue, the window, the memory pool and the two state families,
 * the state lives on the host and is keyed by the guest's command buffer
 * address. Nothing is written into the guest's command buffer object, because
 * the layout of an NVNcommandBuffer is a property of the Nintendo SDK, which
 * this runtime does not have.
 */
#ifndef FFRS_NVN_COMMAND_BUFFER_H
#define FFRS_NVN_COMMAND_BUFFER_H

#include <stddef.h>
#include <stdint.h>

typedef struct nvn_command_buffer_family_entry {
    const char *name;
    void *fn;
} nvn_command_buffer_family_entry;

size_t nvn_command_buffer_family_count(void);
const nvn_command_buffer_family_entry *nvn_command_buffer_family_entry_at(size_t index);

/* One recorded command.
 *
 * op is the index of the entry point in the family table, so the name of a
 * recorded command is nvn_command_buffer_family_entry_at(op)->name. args holds
 * the arguments of the call as they were passed, one word each, with the
 * command buffer itself as the first one. A float is recorded as its bit
 * pattern, and a narrow argument as its own width only. */
typedef struct nvn_recorded_command {
    uint64_t op;
    uint32_t arg_count;
    uint64_t args[8];
} nvn_recorded_command;

/* The records of one command buffer.
 *
 * The list belongs to the runtime and stays valid until the next command is
 * recorded into the same buffer, so a caller reads it before it records more.
 * Returns 1 when the buffer has records. */
int nvn_command_buffer_get_records(void *buffer, const nvn_recorded_command **out, size_t *count);

/* Statistics for the contract. */
typedef struct nvn_command_buffer_stats {
    uint64_t calls;          /* calls into the family */
    uint64_t recorded;       /* commands that made it into a record */
    uint64_t recorded_args;  /* argument words recorded in total */
    uint64_t dropped;        /* commands that did not fit into a record */
    uint64_t refused;        /* calls the runtime could not serve at all */
    uint64_t initialized;
    uint64_t finalized;
    uint64_t raw_calls;      /* calls to the entry points of unknown signature */
    uint64_t honest_answers; /* answers that are a stated approximation */
    uint64_t handles;        /* the command handles the runtime handed out */
    int live_buffers;
    int records; /* the records the runtime holds right now */
} nvn_command_buffer_stats;

void nvn_command_buffer_get_stats(nvn_command_buffer_stats *out);

/* The recorder core. The generated wrappers use these. */
void nvn_command_buffer_record(uint64_t op, const void *buffer, const uint64_t *args,
                                uint32_t arg_count);
void nvn_command_buffer_log_raw(const char *name, const void *buffer, const uint64_t *args,
                                 uint32_t arg_count);
uint64_t nvn_command_buffer_float_bits(float value);
void nvn_command_buffer_set_result(uint64_t value);

int nvn_command_buffer_initialize(void *buffer);
void nvn_command_buffer_finalize(void *buffer);
void nvn_command_buffer_begin_recording(void *buffer);
uint64_t nvn_command_buffer_end_recording(void *buffer);
void nvn_command_buffer_add_command_memory(void *buffer, uint64_t size);
void nvn_command_buffer_add_control_memory(void *buffer, uint64_t size);
void nvn_command_buffer_set_copy_row_stride(void *buffer, int64_t stride);
void nvn_command_buffer_set_copy_image_stride(void *buffer, int64_t stride);
void nvn_command_buffer_set_memory_callback(void *buffer, uint64_t callback);
void nvn_command_buffer_set_memory_callback_data(void *buffer, uint64_t data);

uint64_t nvn_command_buffer_get_command_memory_size(const void *buffer);
uint64_t nvn_command_buffer_get_command_memory_free(const void *buffer);
uint64_t nvn_command_buffer_get_control_memory_size(const void *buffer);
uint64_t nvn_command_buffer_get_control_memory_free(const void *buffer);
int64_t nvn_command_buffer_get_copy_row_stride(const void *buffer);
int64_t nvn_command_buffer_get_copy_image_stride(const void *buffer);
uint64_t nvn_command_buffer_get_memory_callback(const void *buffer);
uint64_t nvn_command_buffer_get_memory_callback_data(const void *buffer);
int nvn_command_buffer_is_recording(const void *buffer);

/* Say that the guest asked how much of its own memory it has already used.
 *
 * The guest writes into the command memory it handed over, and the runtime
 * cannot see how much of it is written, so there is no measurement to
 * report. The answer is zero and this says so, once per buffer, so that the
 * log shows the limit instead of hiding it. */
void nvn_command_buffer_report_used_is_unknown(const void *buffer, const char *what);

#endif /* FFRS_NVN_COMMAND_BUFFER_H */
