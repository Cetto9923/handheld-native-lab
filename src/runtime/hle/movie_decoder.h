#ifndef FFRS_MOVIE_DECODER_H
#define FFRS_MOVIE_DECODER_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct ffrs_movie_virtual { void (*target)(void); unsigned arguments, returns; };
#if defined(__ANDROID__)
const struct ffrs_movie_virtual *ffrs_movie_decoder_virtuals(size_t *count);
int ffrs_movie_decoder_publish_virtuals(void *const *gates, size_t count);
uint64_t ffrs_movie_decoder_lookup(const char *, unsigned *, unsigned *);
/* Default off. Existing private directory; at most four chunks / 8 MiB. */
int ffrs_movie_decoder_set_diagnostics(const char *directory);
#else
static inline const struct ffrs_movie_virtual *ffrs_movie_decoder_virtuals(size_t *count) { if(count)*count=0;return NULL; }
static inline int ffrs_movie_decoder_publish_virtuals(void *const *gates,size_t count) { (void)gates;return count?-1:0; }
static inline uint64_t ffrs_movie_decoder_lookup(const char *name,unsigned *args,unsigned *returns) { (void)name;(void)args;(void)returns;return 0; }
static inline int ffrs_movie_decoder_set_diagnostics(const char *directory) { return directory?-1:0; }
#endif
#ifdef __cplusplus
}
#endif
#endif
