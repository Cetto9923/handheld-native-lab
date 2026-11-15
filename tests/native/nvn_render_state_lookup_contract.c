#include "../../src/graphics/nvn_device/nvn_render_state.h"
#include "../../src/graphics/nvn_device/nvn_packed_state.h"

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

void nvn_render_state_color_set_defaults(void *object);
void nvn_render_state_color_set_logic_op(void *object, int value);
int nvn_render_state_color_get_logic_op(const void *object);
void nvn_render_state_polygon_set_defaults(void *object);
void nvn_render_state_polygon_set_cull_face(void *object, int value);
int nvn_render_state_polygon_get_cull_face(const void *object);

static _Alignas(64) unsigned char objects[4096][64];
static _Alignas(64) unsigned char copied[64];
static _Alignas(64) unsigned char threaded[4][64];

static void *exercise_thread(void *opaque) {
    size_t index = (size_t)(uintptr_t)opaque;
    unsigned char *object = threaded[index];
    for (int round = 0; round < 2000; ++round) {
        int value = ((int)index + round) & 15;
        nvn_render_state_color_set_logic_op(object, value);
        assert(nvn_render_state_color_get_logic_op(object) == value);
    }
    return NULL;
}

static void check_lookup(void) {
    /* Growing the backing table must preserve earlier lookup indices. */
    for (size_t i = 0; i < 4096; ++i) {
        nvn_render_state_color_set_logic_op(objects[i], (int)i & 15);
        assert(nvn_render_state_color_get_logic_op(objects[i]) == ((int)i & 15));
        assert(nvn_render_state_color_get_logic_op(objects[0]) == 0);
    }
    /* More objects than hints force collisions and the full-search fallback. */
    for (size_t round = 0; round < 3; ++round) {
        for (size_t i = 0; i < 4096; ++i) {
            size_t index = (i * 3019) & 4095;
            assert(nvn_render_state_color_get_logic_op(objects[index]) == ((int)index & 15));
        }
    }

    memcpy(copied, objects[4095], sizeof(copied));
    assert(nvn_render_state_color_get_logic_op(copied) == 15);
    ffrs_color_st replacement = {0, 7, 8};
    memcpy(copied, &replacement, sizeof(replacement));
    assert(nvn_render_state_color_get_logic_op(copied) == 7);
    /* Reuse an already cached address through a normal guest memcpy. */
    memcpy(objects[0], objects[4095], sizeof(objects[0]));
    assert(nvn_render_state_color_get_logic_op(objects[0]) == 15);
    nvn_render_state_color_set_defaults(objects[0]);
    assert(nvn_render_state_color_get_logic_op(objects[0]) == 3);

    /* One guest address may be viewed through different state families. */
    nvn_render_state_polygon_set_defaults(copied);
    nvn_render_state_polygon_set_cull_face(copied, 2);
    assert(nvn_render_state_polygon_get_cull_face(copied) == 2);
    ffrs_color_st color;
    memcpy(&color, copied, sizeof(color));
    assert(nvn_render_state_color_get_logic_op(copied) == color.logic_op);
    assert(nvn_render_state_polygon_get_cull_face(copied) == 2);
    assert(nvn_render_state_color_get_logic_op(NULL) == 3);

    pthread_t threads[4];
    for (size_t i = 0; i < 4; ++i)
        assert(pthread_create(&threads[i], NULL, exercise_thread, (void *)(uintptr_t)i) == 0);
    for (size_t i = 0; i < 4; ++i)
        assert(pthread_join(threads[i], NULL) == 0);
    nvn_render_state_stats stats;
    nvn_render_state_get_stats(&stats);
    assert(stats.live_states == 4102);
}

int main(int argc, char **argv) {
    check_lookup();
    if (argc == 2 && strcmp(argv[1], "--benchmark") == 0) {
        struct timespec begin, end;
        volatile uint64_t sum = 0;
        clock_gettime(CLOCK_MONOTONIC, &begin);
        for (size_t i = 0; i < 100000; ++i)
            sum += (unsigned)nvn_render_state_color_get_logic_op(objects[4095]);
        clock_gettime(CLOCK_MONOTONIC, &end);
        double ms = (end.tv_sec - begin.tv_sec) * 1000.0 +
                    (end.tv_nsec - begin.tv_nsec) / 1000000.0;
        assert(sum == UINT64_C(1500000));
        printf("{\"lookup_ms\":%.3f,\"iterations\":100000,\"sum\":%llu}\n",
               ms, (unsigned long long)sum);
    }
    puts("render-state lookup contract passed");
    return 0;
}
