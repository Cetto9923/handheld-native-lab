# Honor 同 draw 捕获离线复验脚本（2026-11-08）

只读输入位于本树 `.local/honor-device-profiles/flame-20261104/dark-shader-retranslate-20261107/postbaseline-20261108/full-native-capture/`。本目录不含游戏、shader、trace 或 GPU 数据。

复验时将本目录脚本复制回上述同名位置（包括 dark-api/limit-api 子目录），再从该位置执行；脚本以自身目录寻找私有输入。依赖本机现有 Python3、numpy 和 `/opt/homebrew/lib/liblz4.dylib`，不重建/下载工具。

- `dark-api/index_trace.py`、`limit-api/index_trace.py`：对应原始 trace 的 API/实际 module/pipeline 索引。已执行并保存精确 EOF/帧号。两份只含对应输入和目标索引差别。
- `compare_dark_native.py`：保留联合 pre+color 导出的负控制。190 项中 176 相同，14 项 color 前后附件不同；不能当原运行场景。
- `compare_dark_color_only.py`：同原 trace 只请求 color，112/112 相同。pre 另外 78 项在负控制中已全部相同；Dark 共 190 份独立 GPU 读回闭合。
- `compare_limit_pre_only.py`、`compare_limit_color_only.py`：独立单目标导出 22/22、115/115 相同；含 11 个别名引用，实际独立读回为 126 份。
- `audit_R32_ranges.py`：实际 vkCreateBufferView 范围审计。
- `audit_invalid_view_use.py`：沿 descriptor 更新/绑定、最新 CB 录制、实际 QueueSubmit 检查非法 view 的活跃引用。Dark 135 条命令引用，Limit 0；不能把恢复快照中的创建全当活跃 draw。

所有 TBO 比较按 actual same-draw VkBuffer ID + offset + range 单独切片；同一 buffer 的不同 binding 不按指针去重。现有导出器 JSON 漏 TBO descriptor，但整 VkBuffer GPU 输出文件存在。D24 仅比较定义的 24 bit，文件 allocation 尾部不算像素；VB 实际可达前缀和额外 bound 尾部明确区分。

原始 trace 从未修改。VK_NOT_READY vs 原运行 SUCCESS 的日志仍保留；实际输入/附件一致性独立比较，不能称所有返回码差异已消失。捕获输入输出完整性与全场景 view 合规、视觉/音频/性能验收分别报告。

`audit_half_sources.py ARCHIVE OUT_JSON [LOOSE_DIR]` audits the eight actual
source XGS descriptors and SPIR-V types. Prefer loose overrides exactly as the
runtime does. Source slots are individually 129, 129, 130, 132, 132, 131, 131,
132; do not assume every half attribute is slot129. The device archive index
and selected entry ranges / loose override were independently hashed without
downloading shaders or rehashing the whole 6GB archive. All eight selected
source files match the audited sources.

The post-fix native frame23325 has 190/190 GPU readbacks, no missing/IO errors,
and all eight corrected half descriptors observed as native76/scalar0/range2.
Its four actual target modules are byte-identical to frame8691's modules and
pass spirv-val. This is a separate new frame: never splice its inputs into the
old trace. A fresh unmodified full API replay for this post-fix build has not
been run. Historical traces retain their invalid background views.

Host regression: compile `tests/device/half_numeric_fact.vert` twice with
`glslc --target-env=vulkan1.1 -DBASE=12` / `-DBASE=18`, compile
`tests/device/test_half_numeric_facts.cpp` with C++17, and pass the two SPIR-V
paths and an output directory. Old facts fail exit4; corrected facts preserve
all eight float half fetches and other raw uint bindings. Validate all eight
outputs with `spirv-val --target-env vulkan1.1`. The existing on-device
`test-dark-half` executable also passed all16 numeric half/raw cases again.

`stream-control.json` records the actual existing61abd layer FIFO/ZSTD title
capture and unmodified existing1d30 replay proof. `audit_zstd_blocks.py` is an
owned read-only format audit: copy it to the private stream-control directory
with title-stream.gfxr and run Python3 using existing libzstd. It checks exact
EOF, unique complete ZSTD frames ending at each compressed block boundary,
and the declared original size for API packets. Metadata image-init headers
are variable length, not limited to128bytes. A title control is not effect
input/output equality and does not change CAPTURE_COMPLETE for Dark/Limit.
