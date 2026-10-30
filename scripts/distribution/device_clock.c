/* Diagnostic phase marker using the same clock as frame_trace_record.
 * No game input, configuration, system clock changes or file writes. */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
int main(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) { perror("clock_gettime"); return 1; }
    printf("%" PRIu64 "\n", (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec);
    return 0;
}
