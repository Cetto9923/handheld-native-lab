# NVN execution boundary — Codex verification

Baseline: d26d3f6. This work changes the dynamic API boundary, not the renderer.

The inherited dispatcher did not compile (`NVNgenericFuncPtrFunc` is not defined
by the bundled header), returned success from an unimplemented DeviceInitialize,
and returned one incompatible generic stub for every unknown API. This could
silently advance the game through calls that performed no operation.

The dispatcher now uses the header's PFNNVNGENERICFUNCPTRPROC type. Backends
register actual typed implementations in an atomic batch. Names are copied,
duplicate/reserved entries are rejected, and function pointers stay stable for
the process lifetime. The two builtins are nvnBootstrapLoader and
nvnDeviceGetProcAddress. Missing APIs return NULL, report the symbol, and set
ENOSYS. There is no default GPU device or fictitious success callback.

Validation commands (run from the integration worktree):

```sh
clang -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
  -fsanitize=address,undefined -pthread -Isrc/graphics/nvn/include \
  src/graphics/nvn/src/nvn_dispatch.c tests/graphics/test_nvn_dispatch.c \
  -o .local/build/test_nvn_dispatch
.local/build/test_nvn_dispatch
clang -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -pthread \
  -Isrc/graphics/nvn/include src/graphics/nvn/src/nvn_dispatch.c \
  tests/graphics/nvn_coverage.c -o .local/build/nvn_coverage
.local/build/nvn_coverage evidence/nvn/main_nvn_ids.txt
```

Results: the ABI callback, all-or-nothing registration, preserved initialization,
missing APIs, reentrant diagnostics callback and four parallel lookup threads
pass ASan/UBSan. Coverage gate exits **3**: required=535, resolved=2,
complete=false, proves_rendering=false. Machine output and the true process
exit status are in `.local/artifacts/nvn/{coverage,run}.json`.

This is a correctness prerequisite for T04, not a title-screen or rendering
pass. A real backend must implement Vulkan resources, queues, commands,
presentation, format mapping and Switch shader conversion, with RG477M evidence.
No registered function alone demonstrates equivalent NVN semantics.
