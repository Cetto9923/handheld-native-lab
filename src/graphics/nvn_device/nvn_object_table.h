/*
 * nvn_object_table.h - the table of the host side objects of one NVN family.
 *
 * A family of NVN objects (a sampler, a texture, a blend state, a render
 * state, a program) keeps its records on the host, keyed by the address the
 * guest gave, because the layout of the guest's objects belongs to the
 * Nintendo SDK and this runtime does not have it.
 *
 * The table used to be a fixed array of a few dozen slots, which was enough
 * for the contract and nothing else. A real game does not work that way: in
 * the device run of p-mm-43 the guest asked for more than sixteen hundred
 * blend states and more than four thousand render states within the first
 * seconds, and the families refused the ones that did not fit
 * ("no room for another blend state", "the render state table is full"). A
 * refused state is a state the guest draws with that this runtime does not
 * know about, so the refusal is a defect, not a limit.
 *
 * The table therefore grows with the number of objects that are live. The
 * entries are the family's own records, so the family keeps its own layout
 * and this owns only the memory: the array, the number of slots it holds and
 * the number of slots that are in use.
 *
 * The cap is the number of objects the device allows, so the table cannot
 * grow past what the guest is allowed to create. It is a limit and not a
 * guess: a table that cannot grow says so.
 */
#ifndef FFRS_NVN_OBJECT_TABLE_H
#define FFRS_NVN_OBJECT_TABLE_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* The table itself. A family puts one of these in its own file and holds the
 * entries through it. */
typedef struct {
    void *slots;       /* the entries, as one block */
    size_t capacity;   /* the slots the block holds */
    size_t count;      /* the slots that are in use */
    size_t entry_size; /* the size of one entry */
} nvn_object_table;

/* An empty table. The entries are allocated when the first object arrives. */
static inline void nvn_object_table_init(nvn_object_table *table, size_t entry_size) {
    table->slots = NULL;
    table->capacity = 0;
    table->count = 0;
    table->entry_size = entry_size;
}

/* The entry at an index, or NULL when the table has no such index. */
static inline void *nvn_object_table_at(nvn_object_table *table, size_t index) {
    if (!table->slots || index >= table->count) return NULL;
    return (char *)table->slots + index * table->entry_size;
}

/* An entry that is not in use yet, zeroed and ready to be filled in.
 *
 * The table grows by doubling, so the cost of the growth is of the same
 * order as the size of the table. The entry that is handed out is the one
 * after the last one in use, so the entries of a family keep the order in
 * which the guest created them.
 *
 * NULL means the table could not grow: either the cap is reached or the
 * memory is gone. The caller has to say which, so it is told by the cap
 * being the reason: the caller knows the cap it passed in. */
static inline void *nvn_object_table_new_entry(nvn_object_table *table, size_t max_entries) {
    if (!table || !table->entry_size || table->count >= max_entries) return NULL;
    if (table->count == table->capacity) {
        size_t capacity=table->capacity?table->capacity*2:64;
        if(capacity<table->capacity || capacity>max_entries)capacity=max_entries;
        if(capacity<=table->count || capacity>(size_t)-1/table->entry_size)return NULL;
        void *grown=realloc(table->slots,capacity*table->entry_size);
        if(!grown)return NULL;
        table->slots=grown;table->capacity=capacity;
    }
    void *entry=(char*)table->slots+table->count*table->entry_size;
    memset(entry,0,table->entry_size);table->count++;
    return entry;
}

/* Give the memory of the table back. The family does this when it is torn
 * down; a table that is torn down with objects in it is a leak the family
 * has to know about, so this is only called when the family knows that every
 * entry is gone. */
static inline void nvn_object_table_free(nvn_object_table *table) {
    free(table->slots);
    table->slots = NULL;
    table->capacity = 0;
    table->count = 0;
}

#endif /* FFRS_NVN_OBJECT_TABLE_H */
