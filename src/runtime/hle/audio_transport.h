#ifndef FFRS_AUDIO_TRANSPORT_H
#define FFRS_AUDIO_TRANSPORT_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* All queue operations require the caller's output mutex. The reader copies a
 * stable prefix before dropping that lock; it consumes only frames accepted by
 * the backend. Guest buffer reuse cannot change these owned bytes. */
typedef struct {
    int16_t *pcm;
    uint32_t capacity,channels;
    uint64_t read,write;
} ffrs_audio_fifo;
static inline uint32_t ffrs_audio_fifo_size(const ffrs_audio_fifo *q) {
    return (uint32_t)(q->write-q->read);
}
static inline uint32_t ffrs_audio_fifo_space(const ffrs_audio_fifo *q) {
    return q->capacity-ffrs_audio_fifo_size(q);
}
static inline int ffrs_audio_fifo_push(ffrs_audio_fifo *q,const int16_t *pcm,uint32_t frames) {
    if(frames>ffrs_audio_fifo_space(q))return 0;
    uint32_t pos=(uint32_t)(q->write%q->capacity),first=q->capacity-pos;
    if(first>frames)first=frames;
    size_t frame_bytes=(size_t)q->channels*sizeof(int16_t);
    memcpy(q->pcm+(size_t)pos*q->channels,pcm,(size_t)first*frame_bytes);
    memcpy(q->pcm,pcm+(size_t)first*q->channels,(size_t)(frames-first)*frame_bytes);
    q->write+=frames;return 1;
}
static inline uint32_t ffrs_audio_fifo_peek(const ffrs_audio_fifo *q,int16_t *pcm,uint32_t frames) {
    uint32_t have=ffrs_audio_fifo_size(q);if(frames>have)frames=have;
    uint32_t pos=(uint32_t)(q->read%q->capacity),first=q->capacity-pos;
    if(first>frames)first=frames;
    size_t frame_bytes=(size_t)q->channels*sizeof(int16_t);
    memcpy(pcm,q->pcm+(size_t)pos*q->channels,(size_t)first*frame_bytes);
    memcpy(pcm+(size_t)first*q->channels,q->pcm,(size_t)(frames-first)*frame_bytes);
    return frames;
}
static inline int ffrs_audio_fifo_consume(ffrs_audio_fifo *q,uint32_t frames) {
    if(frames>ffrs_audio_fifo_size(q))return 0;
    q->read+=frames;return 1;
}
typedef struct {
    uint64_t deadline_ns,remainder;
    int started;
} ffrs_audio_sample_clock;
/* available_ns is when this PCM was actually enqueued (clamped to Start), not
 * when a delayed worker happened to wake. Already queued PCM keeps its absolute
 * sampling interval; a real gap with no data begins a new interval. Zero for
 * later chunks of the same buffer preserves fractional nanoseconds and phase. */
static inline uint64_t ffrs_audio_clock_reserve(ffrs_audio_sample_clock *c,
        uint64_t available_ns,uint32_t frames,uint32_t rate) {
    uint64_t scaled=(uint64_t)frames*1000000000ull+c->remainder;
    uint64_t duration=scaled/rate;
    if(!c->started || available_ns>c->deadline_ns) {
        c->deadline_ns=available_ns;c->remainder=0;c->started=1;
        scaled=(uint64_t)frames*1000000000ull;duration=scaled/rate;
    }
    c->deadline_ns+=duration;c->remainder=scaled%rate;
    return c->deadline_ns;
}
#endif
