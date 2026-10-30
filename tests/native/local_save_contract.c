#include "../../src/runtime/hle/nso_host.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int errors;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); errors++; } } while (0)
int main(void) {
    char root[] = "/data/local/tmp/ffrs-local-save-contract-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    struct { uint64_t before; struct nso_local_user_handle h; uint64_t after; } handle = {0x1234, {{0}}, 0x5678};
    struct nso_local_uid uid = {{0}}, unchanged = {{9, 9}};
    CHECK(!nso_host_account_try_open_preselected_user(&handle.h));
    CHECK(nso_host_account_initialize() == 0);
    CHECK(nso_host_account_try_open_preselected_user(&handle.h));
    CHECK(handle.before == 0x1234 && handle.after == 0x5678);
    CHECK(nso_host_account_get_user_id(&uid, &handle.h) == 0);
    struct nso_local_user_handle bad = {{0}};
    CHECK(nso_host_account_get_user_id(&unchanged, &bad) != 0 && unchanged.words[0] == 9);
    CHECK(nso_host_fs_set_save_root(root) == 0);
    CHECK(nso_host_fs_ensure_save_data(&uid) == 0);
    CHECK(nso_host_fs_is_save_data_existing(UINT64_C(0x0100b8a026f14000), &uid));
    CHECK(!nso_host_fs_is_save_data_existing(UINT64_C(0x010068b027e88000), &uid));
    CHECK(!nso_host_fs_is_save_data_existing(UINT64_C(0x0100b8a026f14000), &unchanged));
    CHECK(nso_host_fs_mount_save_data("save", &uid) == 0);
    CHECK(nso_host_fs_create_directory("save:/test") == 0);
    CHECK(nso_host_fs_create_file("save:/test/value.bin", 16) == 0);
    CHECK(nso_host_fs_create_file("save:/test/value.bin", 16) == 0x402);
    nn_fs_file_handle file = {0};
    CHECK(nso_host_fs_open_file(&file, "save:/test/value.bin", 3) == 0);
    const char value[] = "port-save"; char readback[sizeof(value)] = {0}; size_t read = 0;
    CHECK(nso_host_fs_write_file(file, 0, value, sizeof(value), NULL) == 0);
    CHECK(nso_host_fs_flush_file(file) == 0);
    CHECK(nso_host_fs_read_file(&read, file, 0, readback, sizeof(readback)) == 0);
    CHECK(read == sizeof(value) && !memcmp(value, readback, sizeof(value)));
    CHECK(nso_host_fs_commit_save_data("save") == 0);
    nso_host_fs_close_file(file);
    CHECK(nso_host_fs_create_file("save:/../escape.bin", 1) != 0);
    setenv("NN_FS_ROM_PATH", root, 1);
    unsigned char cache[65536];
    CHECK(nso_host_fs_mount_rom("rom", cache, sizeof(cache)) == 0);
    CHECK(nso_host_fs_open_file(&file, "rom:/test/value.bin", 2) != 0);
    CHECK(nso_host_fs_create_file("rom:/new.bin", 1) != 0);
    CHECK(nso_host_fs_create_directory("rom:/newdir") != 0);
    printf("local_save_contract errors=%d fixture=%s\n", errors, root);
    return errors ? 1 : 0;
}
