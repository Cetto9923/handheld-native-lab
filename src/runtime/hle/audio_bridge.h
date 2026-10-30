#ifndef FFRS_AUDIO_BRIDGE_H
#define FFRS_AUDIO_BRIDGE_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
uint64_t ffrs_audio_lookup(const char *name, unsigned *arguments, unsigned *returns);
void ffrs_audio_devices_changed(void);
struct ffrs_audio_stats {
    uint64_t appended_buffers, released_buffers, rejected_buffers;
    uint64_t submitted_frames, frames_written, nonzero_samples, samples_written;
    uint64_t write_calls, zero_writes;
    uint32_t peak, errors;
    int32_t sample_rate, channels, running, aaudio_state, device_id, xruns;
};
/* Actual source/AAudio-accepted PCM counters, not an assertion of audibility. */
int ffrs_audio_get_stats(const void *key,struct ffrs_audio_stats *out,size_t bytes);
/* Default off; existing private directory, at most 180 seconds per stream. */
int ffrs_audio_set_diagnostics(const char *directory);
/* Own app files directory. Experimental transport remains off unless its
 * explicit gfx-audio-paced-transport marker contains '1'. */
int ffrs_audio_set_platform_directory(const char *directory);
#ifdef __cplusplus
}
#endif
#endif
