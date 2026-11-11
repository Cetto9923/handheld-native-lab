/* Ownership ledger for pointers that the guest can see. See heap_owner.h. */
#include "heap_owner.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct heap_owner_entry {
    const void *ptr;
    size_t bytes;
    enum heap_owner_kind kind;
};

static pthread_mutex_t heap_owner_lock = PTHREAD_MUTEX_INITIALIZER;
static struct heap_owner_entry *heap_owner_table;
static size_t heap_owner_count;
static size_t heap_owner_capacity;

/* The table is kept sorted by address so a lookup is a binary search. */
static size_t heap_owner_lower_bound(const void *ptr) {
    size_t lo = 0, hi = heap_owner_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (heap_owner_table[mid].ptr < ptr) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

void heap_owner_record(void *ptr, size_t bytes, enum heap_owner_kind kind) {
    if (!ptr || kind == HEAP_OWNER_UNKNOWN) {
        return;
    }
    pthread_mutex_lock(&heap_owner_lock);
    size_t at = heap_owner_lower_bound(ptr);
    if (at < heap_owner_count && heap_owner_table[at].ptr == ptr) {
        heap_owner_table[at].bytes = bytes;
        heap_owner_table[at].kind = kind;
        pthread_mutex_unlock(&heap_owner_lock);
        return;
    }
    if (heap_owner_count == heap_owner_capacity) {
        size_t capacity = heap_owner_capacity ? heap_owner_capacity * 2 : 64;
        struct heap_owner_entry *grown =
            realloc(heap_owner_table, capacity * sizeof(*grown));
        if (!grown) {
            /* The block stays unowned. Releases keep the previous behaviour
             * for it; the ledger never changes what a release does. */
            pthread_mutex_unlock(&heap_owner_lock);
            return;
        }
        heap_owner_table = grown;
        heap_owner_capacity = capacity;
    }
    memmove(&heap_owner_table[at + 1], &heap_owner_table[at],
            (heap_owner_count - at) * sizeof(*heap_owner_table));
    heap_owner_table[at].ptr = ptr;
    heap_owner_table[at].bytes = bytes;
    heap_owner_table[at].kind = kind;
    heap_owner_count++;
    pthread_mutex_unlock(&heap_owner_lock);
}

enum heap_owner_kind heap_owner_lookup(const void *ptr, size_t *bytes_out) {
    if (!ptr) {
        return HEAP_OWNER_UNKNOWN;
    }
    pthread_mutex_lock(&heap_owner_lock);
    size_t at = heap_owner_lower_bound(ptr);
    enum heap_owner_kind kind = HEAP_OWNER_UNKNOWN;
    if (at < heap_owner_count && heap_owner_table[at].ptr == ptr) {
        kind = heap_owner_table[at].kind;
        if (bytes_out) {
            *bytes_out = heap_owner_table[at].bytes;
        }
    }
    pthread_mutex_unlock(&heap_owner_lock);
    return kind;
}

void heap_owner_forget(const void *ptr) {
    if (!ptr) {
        return;
    }
    pthread_mutex_lock(&heap_owner_lock);
    size_t at = heap_owner_lower_bound(ptr);
    if (at < heap_owner_count && heap_owner_table[at].ptr == ptr) {
        memmove(&heap_owner_table[at], &heap_owner_table[at + 1],
                (heap_owner_count - at - 1) * sizeof(*heap_owner_table));
        heap_owner_count--;
    }
    pthread_mutex_unlock(&heap_owner_lock);
}
