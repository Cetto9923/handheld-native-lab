# Honor original-run full-draw capture matrix (implementation in progress)

Source checkpoint96e3b33; graphics baseline214fd79/ordinaryAPK2a4 remains intact. Sole writer Codex/root; deviceA42BBB5109000333 only. Existing native probe and original61abd GFXR layer/replay1d30 are reused. Normal game APK compilation is allowed; no exporter/tool rebuild or old large capture redownload. Default capture is off. This document records a plan, not completed evidence.

| Resource/state | Original-run acquisition | Same-frame replay check | Current status |
|---|---|---|---|
| Identity | native frame/submission/global draw, VS+FS exact keys; first pre and color per family | unique actual draw matched by modules/parameters/mesh and bound slices | PENDING |
| UBO/SSBO | record final bound VkBuffer+effective offset/range per stage/binding/array; GPU copy before draw, independent owner/fence; writable slices also after | existing exporter, per-binding slices; no pointer-only dedup | PENDING |
| Texel buffers | actual final view format/offset/range; GPU copy; pad only inside proven VkBuffer extent, store exact valid span; never expand shader view | actual SPIR-V sampled type vs view; every valid byte | PENDING |
| Vertex/index/indirect | actual prepared Vulkan buffers, whole accessible bound ranges; GPU indirect20B packet, no stale CPU mesh | original same-frame draw resources/actual indirect values | PENDING |
| Images | final actual view (including sampled proxy), all accessible mips/layers/aspects; raw block layout for BC1-7, explicit unsupported/undefined cases; GPU copies just before draw | all-subresource raw export; logical copied prefixes only, allocation tails excluded | PENDING |
| Color/depth/stencil | each actual attachment view before and after the selected draw, exact formats; both D/S aspects | compare defined storage bits/regions; D24 X bits excluded; state loads/stores checked | PENDING |
| Shader/pipeline | same original-frame API stream contains actual vkCreateShaderModule bytes and pipeline use, including cache restoration | actual draw stage IDs and full module SHA; no offline module substitute | existing capability, new same-frame match PENDING |
| Render state | original API stream plus native depth/stencil/blend/mask/viewport/scissor/raster/draw descriptors | pipeline/renderpass/dynamic offset/push-constant state | PENDING |
| Lifecycle/alias | native source serial, command slot generation, exact handle+range and copy point; original API create/bind/write/destroy stream | resource generation and producer/copy/consumer dependencies | PENDING |
| Completion | consumed explicit Honor marker D(dark)/E(Limit sphere), max2 selected draws, max256MiB, bounded subresources; fence+I/O status/END/list/hash | unchanged replay vs native original inputs/attachments; any missing/skip/ambiguous match keeps NO | PENDING |

Dark keys: db5f1e2dd1c9ef3d/c0adc062e4b8e8fa then41a5a80e5242a26c/b7dd0f48aea00196. Limit sphere:085afbc955a0744f/0066ad2ea935247d then632c207422d773c5/17ab021c45c63390. Record actual EQUAL/GEQUAL per draw; do not impose a shared cause or change depth. Distinct shards/fire-tail proofs remain separate.

Copies must be queued before draw without retiring the current descriptor/vertex arena. Readbacks use independent ownership and are flushed at the next safe draw boundary, after an actual GPU fence. Texture uploads/proxy refreshes must finish before queuing the captured inputs. Before/after attachment copies must not deduplicate just because content_version is unchanged. Full BC4 means reachable compressed blocks for every accessible mip, not sampling blocks as pixels. Do not claim a CPU mapped prepared buffer is a GPU readback.

Negative controls: marker0 executes no extra GPU copies; D/E clear all ALWAYS/cull/empty-FS/prepass diagnostic flags; FS/shader/depth behavior stays original. Capture an active effect, never a paused absent phase. Unknown resource/view/sample layouts and I/O or bounds failures are explicit missing rows. Limit and Dark are independently accepted. CAPTURE_COMPLETE currently NO, full VIEW_VALIDITY NOT_FIXED; existing GPU first-wrong/ordinary visual proofs remain valid within their recorded scope.

Acceptance must include coordination/prompts/_STANDING-audio-acceptance.md. A2DP subjective AUDIO=NOT_RUN by explicit user instruction; NONE(enum10),fence0,burst buffers,TIMEOUT retry and device-change reopen remain. No overall PASS or60 claim. Install-r only with recorded APK SHA and preserved data; restore ordinary2a4/PADhidden after capture.

Implementation checkpoint: native source integrated/build0; exact shader words and GPU final bound ranges/subresources queued; safe present-only readback; strict auditor implemented. APK769d1d28 with unchanged original61abd layer begins device validation. Every PENDING validation cell remains PENDING until raw evidence and replay comparison. Native copied files alone do not make CAPTURE_COMPLETE YES.

Device negative control: APK769d1d28/PID29937/fullscreen title, D consumed→0, native frame1942 has FULL_BEGIN/FULL_END/END, selected0/queued0/files0/missingpair1, no readback copies; strict auditor exits1 as expected. Evidence full-native-capture/negative-title/ and negative-control-result.json. This validates rejection/bounded completion, not active effects or full capture.

Runtime verified: Dark native8691 (190 readbacks/196 binaries/86895216B) and Limit native17938 (126/132/85982740B), both strict native_readback_audit PASS with complete END/no missing/IO. Exact source module words and GPU bound slices/mip/aspects/attachment before-after files now exist. Dark same-frame actual API1046335/1047825 matches all4module hashes and43 buffer offset/range/format rows; full trace SHA/EOF verified. Replay/defined-region agreement PENDING, Limit API same-frame verification PENDING until transfer. CAPTURE_COMPLETE remains NO.
