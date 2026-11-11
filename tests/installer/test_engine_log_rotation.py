"""The engine log rotates once and a fatal exit leaves a small file.

The test compiles the real log functions. It does not start the game.
"""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class EngineLogRotation(unittest.TestCase):
    def test_log_rotates_once_and_fatal_is_recorded(self):
        source = (ROOT / 'src/graphics/nvn_device/nvn_engine.c').read_text()
        start = source.index('static const char *engine_log_default_paths[]')
        body = source[start:source.index('\nvoid nvn_engine_set_enabled', start)]
        fixture = '''#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
''' + body + '''
int main(void) {
    const char *dir = getenv("FFRS_ENGINE_LOG_DIR");
    char path[512];
    snprintf(path, sizeof path, "%s/engine.log", dir);
    setenv("FFRS_ENGINE_LOG", path, 1);
    setenv("FFRS_ENGINE_LOG_MAX", "3000", 1);
    char line[180];
    memset(line, 'A', sizeof line - 1);
    line[sizeof line - 1] = 0;
    for (int i = 0; i < 80; i++) engine_log(NULL, line);
    char backup[520];
    snprintf(backup, sizeof backup, "%s.1", path);
    struct stat first, active;
    if (stat(backup, &first) != 0) return 2;
    if (stat(path, &active) != 0) return 3;
    off_t backup_size = first.st_size;
    off_t active_size = active.st_size;
    for (int i = 0; i < 40; i++) engine_log(NULL, line);
    if (stat(backup, &first) != 0 || first.st_size != backup_size) return 4;
    if (stat(path, &active) != 0 || active.st_size != active_size) return 5;
    pid_t child = fork();
    if (child == 0) engine_fatal(NULL, "boundary-test");
    int status = 0;
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) != 70) return 6;
    char fatal[520];
    snprintf(fatal, sizeof fatal, "%s.fatal", path);
    FILE *file = fopen(fatal, "rb");
    if (!file) return 7;
    char got[128] = {0};
    if (!fgets(got, sizeof got, file)) return 8;
    fclose(file);
    if (strcmp(got, "FATAL: boundary-test\\n") != 0) return 9;
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            code = Path(temp) / 'log.c'
            binary = Path(temp) / 'log'
            code.write_text(fixture)
            subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pthread',
                            str(code), '-o', str(binary)], check=True)
            env = os.environ.copy()
            env['FFRS_ENGINE_LOG_DIR'] = temp
            subprocess.run([str(binary)], check=True, env=env)


if __name__ == '__main__':
    unittest.main()
