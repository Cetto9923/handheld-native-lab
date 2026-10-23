# RG477M startup capacity correction and original-entry gate

Source freeze: `d4ded908096feef6fff2876ab2ffdaa9e48303b5`; integrated as `c5bbddbe2487e27e16a05627580607c0b956351a`. This report records coordinator device execution, not reviewer device execution. The prior `15e498c` normal contract exit1 and diagnostic exit1 remain preserved.

On the same RG477M Android14 ARM64/4KiB device, the first native stack report remains 64752B for a 65536B raw RW range; callback is withheld. After joining, actual unmap/refund and checked page-rounded administrative capacity rebuild, attempt2 has raw69632B, native68848B, administrative4096B, exact logical65536B. Callback SP/local are inside the logical window. Both outer guards are checked using raw bounds. Query continues to charge logical65536B only. This does not hardcode the observed 784B deficit or claim a general Bionic private layout.

| Actual device command | Exit | Scope |
|---|---:|---|
| standalone Context contract | 0 | verified raw/native/logical relations, guards, SP, joined cleanup, quota competition |
| startup profile normal contract | 0 | two callbacks, seven fields/padding, VA/return value/altstack/disposition recovery, errors0/extension0 |
| original module profile info | 0 | image162869248B, imports651/unresolved619; CRT/TLS explicitly uninitialized |
| original nnMain +0x352f160 | 133 | crossed Query caller+0x27a50, stopped strict unresolved AllocateMemoryPages sym71/caller+0x27f90 with address in owned VA and bytes0x10000 |
| original malloc(4096) +0x132d0 | 133 | crossed Query+0x1398c/+0x27a50, stopped strict GetVirtualAddressMemoryResourceUsage sym66/caller+0x144b8 |

No timeout; no tail pipeline. A zero contract/info exit is not a successful game entry. No constructors/complete CRT/TLS, title, rendered game frame, playable scene, game input/audio/save or performance has passed. No APK update, data clearing, firmware operation or permission request occurred. Raw self-built ELF only in the existing project-owned shell directory. The source main was not patched; SHA256 before and after is `6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5`. The 1GiB quota is explicit diagnostic port policy, not measured Horizon physical RAM. The 62GiB address reservation remains virtual only.

## Reproduction and evidence

Coordinator checkout `.local/device/rg477m-session1/capacity-v1-plan.json` records source hashes, build-manifest hashes, artifact paths/full SHA256, existing material identities and explicit policy; `capacity-v1-stage.json` records exact ADB argv/readback; `capacity-v1-execution.json` records actual commands, return codes, elapsed time and stdout/stderr hashes. The same prefix's per-case stdout/stderr are retained. `run_capacity_v1.py` is the executed script. Do not replay its staging blindly: fresh hash-qualified paths already exist. First inspect stage/execution; verify/reuse only explicitly. Each command has device timeout15s and host timeout22s.

- `context`: 78432B, SHA256 `6d7d46bae9a0b97f8a25221b6d1213f02ec748f8d17c92e6144ee198b761dda5`; local `/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-runtime/.local/guest-context-capacity-v2/guest_context_contract.android`.
- `contract`: 675792B, SHA256 `aa82fc592bb4db849375aa54efdc0cfb17b703cde29f3d5f4456aad77abb2d86`; local `/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-runtime/.local/owned-startup-capacity-v1/guest_process_contract.android`.
- `cli`: 655104B, SHA256 `1e5953a8144855d65f93310c5d68fce215abe097d1ff94f8e6fed9cd8d6a6d69`; local `/Users/yuyan9923/Documents/Codex/2026-10-23/task/ffrs-runtime/.local/owned-startup-capacity-v1/nso_owned_startup.android`.

Independent review of the new frozen Context/profile is in progress at `../functional-review/startup-capacity/`, not yet PASS. The earlier review in [codex_guest_process_review.md](codex_guest_process_review.md) applies to15e and records the old macro-off P2. New code directly references typed guards and rejects an untyped host profile build; review will verify cross-TU behavior.

Next production gate is a single owner for real VA and mapped-page tickets, integrated into Query only after successful real map/commit, retained on ambiguous failures, exact free and whole-region refund only after actual release. No guessed SDK error mapping or success stubs.
