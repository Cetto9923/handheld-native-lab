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

Header coverage check: 59 of the 535 inventoried names have no corresponding
PFN typedef in the bundled header. Its own comment describes NVN 53.105 and
contains incomplete opaque types; it is not proof of SDK 20.5 ABI coverage.
Do not infer missing function signatures or texture-format equivalence to make
the game advance. The new dispatcher provides no such inferred implementation.

## LAS MIT API transfer reuse: actual RG evidence81b1917

Frozen source81b1917adb261f80ce1780e2c16d8d31c536a693 in ../ffrs-las-transfer,11 changed files clean. Existing driver now consumes typed NVN command entry→LAS80B CmdRec→borrowed production VkBuffer owner→real vkCmdCopyBuffer; no CPU memmove fallback. LAS Beta4 fe66f31a6dbcaee1faa1e6718805cb2dbcc67a93 ownMIT attribution/provenance retained. This is an extracted production API subset, not complete original-game NVN binding or a full upstream renderer transplant.

Root actual sameRG execution: exit0/100.1925ms/nohost orremote timeout,stderr empty. Mali-G615 MC6; actual queue/fence/readback,64KiB buffer and256B RGBA clear eachzero mismatch;31 actualVkResults0;12destroy/3unmap. Originalvalidator0 and explicit newfields typed_record_path=true/backend_copy_count1/backend_copy_bytes65536/gpu_completion_retired=true. opaque_layout_written=false/shader_or_present=false/game_rendering=false. Independent newcomponent QA running; root device is not reviewer device PASS.

ELF288744B SHA2568ac793ff4bc978ecad1e1d837195e102ec9a7f28118b97fed38f38694430ef26;12listedsourcebundle21e4c62f30c481211ff09e8df8f89557215322f610552824e64dbd82079eed70. Inputs/source/ELF all checked against authorfreeze/build-evidence before fresh staging,remote SHA readback matched,chmod700. Raw .local/device/rg477m-session1/las-transfer-v1-{plan,stage,execution,result,validator}.json plusstdout/stderr; exact executed run_las_transfer_v1.py. No newAPK/permissions/gamebytes/data clearing. Actual source integration awaits independentreview; oldrootdriver alone is not claimed to contain newadapter.

Remaining path: accepted typed targets must be prebound per owned primary while native scopeIDLE; existing cross-thread/global thunk routing cannot be blindly reused. Unknown gameopaque layout remains unresolved. No shader translation/surface/present/title/gameplay proof; do not add fakeQueueSubmitCommands or guessed offsets.
