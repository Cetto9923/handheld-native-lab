"""Exercise installed-content discovery and mounts using the production C code."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class AddOnContent(unittest.TestCase):
    def test_installed_content_and_legacy_absence(self):
        source = (ROOT / 'src/runtime/hle/nso_host.c').read_text()
        mounts = source[source.index('#define NSO_FS_MAX_MOUNTS'):
                        source.index('static char s_fs_save_root')]
        resolver = source[source.index('static int resolve_fs_path('):
                          source.index('int32_t nso_host_fs_get_free_space_size(')]
        content = source[source.index('static int aoc_data_root('):
                         source.index('/* nn::os::SystemEvent is a separate')]
        fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
''' + mounts + resolver + content + r'''
static void install(int index, int valid) {
    char name[64];
    snprintf(name, sizeof name, "dlc/%d", index);
    assert(!mkdir(name, 0700));
    snprintf(name, sizeof name, "dlc/%d/content", index);
    FILE *file = fopen(name, "wb"); assert(file);
    assert(fwrite("Download Content", 1, 16, file) == 16); assert(!fclose(file));
    snprintf(name, sizeof name, "dlc/%d.meta", index);
    file = fopen(name, "wb"); assert(file);
    fprintf(file, "FFRS-AOC-1\n0100b8a026f14000\n%016llx\n00000000\n",
            (unsigned long long)UINT64_C(0x0100b8a026f15000) + index + !valid);
    assert(!fclose(file));
}
int main(int argc, char **argv) {
    assert(argc == 2 && !chdir(argv[1]));
    assert(!unsetenv("NN_FS_ROM_PATH") && !unsetenv("ROMFS_PATH"));
    assert(nso_host_aoc_count_add_on_content() == 0);
    char rom[512]; assert(getcwd(rom, sizeof rom));
    strcat(rom, "/romfs/"); assert(!setenv("ROMFS_PATH", rom, 1));
    assert(!mkdir("dlc", 0700));
    assert(nso_host_aoc_count_add_on_content() == 0);
    install(12, 1); install(2, 1); install(3, 0);
    assert(!mkdir("dlc/02", 0700));
    assert(!symlink("2", "dlc/4"));
    assert(!symlink("2.meta", "dlc/4.meta"));
    assert(nso_host_aoc_count_add_on_content() == 2);
    int32_t indices[4] = {-1,-1,-1,-1};
    assert(nso_host_aoc_list_add_on_content(indices, 0, 4) == 2);
    assert(indices[0] == 2 && indices[1] == 12 && indices[2] == -1);
    assert(nso_host_aoc_list_add_on_content(indices, 1, 1) == 1 && indices[0] == 12);
    assert(!nso_host_aoc_list_add_on_content(indices, 99, 4));
    assert(!nso_host_aoc_list_add_on_content(indices, -1, 4));
    assert(!nso_host_aoc_list_add_on_content(NULL, 0, 4));
    assert(!nso_host_aoc_list_add_on_content(indices, 0, -1));
    size_t cache = 99;
    assert(!nso_host_fs_query_mount_add_on_content_cache_size(&cache, 2) && !cache);
    assert(nso_host_fs_query_mount_add_on_content_cache_size(NULL, 2));
    assert(nso_host_fs_mount_add_on_content("bad", -1, NULL, 0));
    assert(nso_host_fs_mount_add_on_content("bad", 4096, NULL, 0));
    assert(nso_host_fs_mount_add_on_content("bad", 3, NULL, 0));
    assert(nso_host_fs_mount_add_on_content("aoc:", 2, NULL, 0));
    assert(nso_host_fs_mount_add_on_content("a/b", 2, NULL, 0));
    assert(nso_host_fs_mount_add_on_content(NULL, 2, NULL, 0));
    assert(nso_host_fs_mount_add_on_content("", 2, NULL, 0));
    assert(nso_host_fs_mount_add_on_content("aoc", 2, NULL, 16));
    assert(!nso_host_fs_mount_add_on_content("aoc", 2, NULL, 0));
    assert(!nso_host_fs_mount_add_on_content("aoc", 2, NULL, 0));
    assert(nso_host_fs_mount_add_on_content("aoc", 12, NULL, 0));
    char resolved[512];
    assert(!resolve_fs_path("aoc:/content", resolved, sizeof resolved));
    FILE *file = fopen(resolved, "rb"); assert(file);
    char marker[17] = {0}; assert(fread(marker, 1, 16, file) == 16);
    assert(!strcmp(marker, "Download Content") && !fclose(file));
    assert(!fs_path_is_writable("aoc:/content"));
    assert(resolve_fs_path("aoc:/../save", resolved, sizeof resolved));
    for (int i = 1; i < NSO_FS_MAX_MOUNTS; ++i) {
        s_fs_mounts[i].in_use = 1; s_fs_mounts[i].writable = 1;
        snprintf(s_fs_mounts[i].name, sizeof s_fs_mounts[i].name, "save%d", i);
    }
    nso_fs_mount_entry before[NSO_FS_MAX_MOUNTS];
    memcpy(before, s_fs_mounts, sizeof before);
    assert(nso_host_fs_mount_add_on_content("save1", 2, NULL, 0));
    assert(nso_host_fs_mount_add_on_content("full", 12, NULL, 0));
    assert(!memcmp(before, s_fs_mounts, sizeof before));
    assert(!unlink("dlc/2/content"));
    assert(nso_host_aoc_count_add_on_content() == 1);
    assert(nso_host_fs_mount_add_on_content("missing", 2, NULL, 0));
    assert(!setenv("NN_FS_ROM_PATH", "/nonexistent/romfs", 1));
    assert(nso_host_aoc_count_add_on_content() == 0);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'test.c').write_text(fixture)
            subprocess.run(['clang', '-std=c11', '-D_DARWIN_C_SOURCE', '-Wall',
                            '-Wextra', '-Werror', '-fsanitize=address,undefined',
                            str(path / 'test.c'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test'), str(path)], check=True)


if __name__ == '__main__':
    unittest.main()
