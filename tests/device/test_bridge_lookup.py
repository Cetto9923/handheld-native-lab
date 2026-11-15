"""Exercise the production bridge index across growth, collisions and failures."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class BridgeLookup(unittest.TestCase):
    def test_lookup_indices_and_object_lifetime(self):
        source = (ROOT / 'src/graphics/nvn_device/nvn_bridge.c').read_text()
        definitions = source[source.index('typedef struct bridge_snapshot {'):
                             source.index('static pthread_mutex_t s_lock')]
        start = source.index('static bridge_entry *entry_find(')
        function = source[start:source.index('/* Put an object', start)]
        fixture = r'''
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static int fail_growth;
static void *grow(void *memory, size_t bytes) {
    if (fail_growth) { fail_growth = 0; return NULL; }
    return realloc(memory, bytes);
}
#define realloc grow
''' + definitions + function + r'''
#undef realloc
static bridge_table table;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static uintptr_t identities[2400];
static unsigned values[2400];
static void *reader(void *unused) {
    (void)unused;
    for (unsigned pass = 0; pass < 3; ++pass) {
        for (unsigned i = 0; i < 2400; ++i) {
            pthread_mutex_lock(&lock);
            bridge_entry *entry = entry_find(&table, &identities[i], 0);
            assert(entry && entry->object == &values[i]);
            assert(entry->serial == i + 1);
            pthread_mutex_unlock(&lock);
        }
    }
    return NULL;
}
int main(void) {
    assert(!entry_find(&table, &identities[0], 0) && table.count == 0);
    fail_growth = 1;
    assert(!entry_find(&table, &identities[0], 1));
    assert(table.count == 0 && !table.entries);
    for (unsigned i = 0; i < 2400; ++i) {
        if (table.count == table.capacity && table.count) {
            size_t count = table.count;
            fail_growth = 1;
            assert(!entry_find(&table, &identities[i], 1));
            assert(table.count == count);
            assert(entry_find(&table, &identities[0], 0)->object == &values[0]);
        }
        bridge_entry *entry = entry_find(&table, &identities[i], 1);
        assert(entry && table.count == i + 1);
        values[i] = i;
        entry->serial = i + 1;
        entry->object = &values[i];
        entry->size = sizeof(values[i]);
    }
    // More addresses than cache slots force collisions. Every result still
    // belongs to the full guest address and the current backing allocation.
    for (unsigned pass = 0; pass < 3; ++pass) {
        for (unsigned i = 0; i < 2400; ++i) {
            bridge_entry *entry = entry_find(&table, &identities[i], 0);
            assert(entry && entry->guest == &identities[i]);
            assert(entry->object == &values[i] && entry->serial == i + 1);
        }
    }
    uintptr_t missing = 0;
    assert(!entry_find(&table, &missing, 0) && table.count == 2400);
    bridge_entry *entry = entry_find(&table, &identities[0], 1);
    entry->serial = 9999;
    entry->object = &values[1];
    assert(entry_find(&table, &identities[0], 0)->serial == 9999);
    assert(entry_find(&table, &identities[0], 0)->object == &values[1]);
    entry->serial = 1;
    entry->object = &values[0];
    pthread_t threads[4];
    for (unsigned i = 0; i < 4; ++i) assert(!pthread_create(&threads[i], NULL, reader, NULL));
    for (unsigned i = 0; i < 4; ++i) assert(!pthread_join(threads[i], NULL));
    free(table.entries);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'test.c').write_text(fixture)
            subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-pthread',
                            str(path / 'test.c'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
