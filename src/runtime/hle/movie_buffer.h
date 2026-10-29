#ifndef FFRS_MOVIE_BUFFER_H
#define FFRS_MOVIE_BUFFER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Exact imported Buffer ABI, reconstructed from subsdk0 build ab62fe8e.
 * This is buffer bookkeeping, not a video decoder or a successful movie. */
uint64_t ffrs_movie_buffer_lookup(const char *, unsigned *, unsigned *);
uint64_t ffrs_movie_core_mask(void);
int ffrs_movie_thread_priority(void);
#ifdef __cplusplus
}
#endif
#endif
