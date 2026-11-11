/* Host durability contract for the save file path.
 * Proves flush and commit return only after the bytes are readable, and that
 * a concurrent reader is not stuck behind the sync. Not an Android test. */
#include "../../src/runtime/hle/nso_host.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int errors;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); errors++; } } while (0)
static nn_fs_file_handle file;
static int reader_done;
static void *reader(void *unused) {
    (void)unused;
    char buffer[16] = {0};
    size_t n = 0;
    int rc = nso_host_fs_read_file(&n, file, 0, buffer, sizeof buffer);
    if (rc != 0) errors++;
    reader_done = 1;
    return NULL;
}
int main(void) {
    char root[] = "/tmp/ffrs-save-commit-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    struct { uint64_t before; struct nso_local_user_handle h; uint64_t after; } handle = {0x1234, {{0}}, 0x5678};
    struct nso_local_uid uid = {{0}};
    CHECK(nso_host_account_initialize() == 0);
    CHECK(nso_host_account_try_open_preselected_user(&handle.h));
    CHECK(nso_host_account_get_user_id(&uid, &handle.h) == 0);
    CHECK(nso_host_fs_set_save_root(root) == 0);
    CHECK(nso_host_fs_ensure_save_data(&uid) == 0);
    CHECK(nso_host_fs_mount_save_data("save", &uid) == 0);
    CHECK(nso_host_fs_create_file("save:/value.bin", 16) == 0);
    CHECK(nso_host_fs_open_file(&file, "save:/value.bin", 3) == 0);
    const char value[] = "port-save";
    CHECK(nso_host_fs_write_file(file, 0, value, sizeof value, NULL) == 0);
    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, reader, NULL) == 0);
    CHECK(nso_host_fs_flush_file(file) == 0);
    CHECK(nso_host_fs_commit_save_data("save") == 0);
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(reader_done);
    char readback[sizeof value] = {0}; size_t read = 0;
    CHECK(nso_host_fs_read_file(&read, file, 0, readback, sizeof readback) == 0);
    CHECK(read == sizeof value && !memcmp(value, readback, sizeof value));
    nso_host_fs_close_file(file);
    if (errors) return 1;
    puts("save commit host contract passed");
    return 0;
}
