# T02 runtime checkpoint — 2026-10-23

Branch: task/T02-codex-runtime. Starting commit: d26d3f683e3cf93fa857c1306e31e29912310a67.
Scope: native AArch64 loader and behavior experiments on macOS arm64.
This is an implementation self-test, not independent QA, Android execution,
game startup, graphics, or playability acceptance. T02 remains IN_PROGRESS.
The host mathematical comparison does not satisfy the acceptance rule requiring
Switch hardware or a trusted emulator reference. No acceptance gate was lowered.

## Observed result

An unmodified, private main NSO loads at a host-assigned address with 651 imports,
4,612 RELA entries and 1,370,974 RELR targets. The existing RELR algorithm was
retained and bounded; it was not replaced with pointer guessing.

The original instructions at module offset 0x694bd0 (44 bytes) execute natively.
A handwritten mathematical oracle checks 16,875 combinations of signed integer
inputs and flag bits, with zero mismatches. The routine computes the unsigned
bit length of (signed_max(a,b,(flags & 1) ? c : 1) | 1). Its engine-facing name
and original source declaration remain unknown.

| Inputs (a,b,c,flags) | Original native output | Independent oracle |
|---|---:|---:|
| (0,0,0,0) | 1 | 1 |
| (3,9,100,0) | 4 | 4 |
| (3,9,100,1) | 7 | 7 |
| (255,256,0,0) | 9 | 9 |
| (-8,-2,-1,1) | 32 | 32 |
| (7,8,1024,2) | 4 | 4 |

Module SHA-256: 6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5.
Build ID (first 16 bytes): 736837afd390442ed7c74a7de730e71b.
Leaf SHA-256: 8a6e3c4fbd3164c966e2d164e0b59f140a162be1a4cf260b60beefae9921c9c9.
The experiment verifies both hashes and checks the input hash again afterwards.
No target instructions or generated game code are committed.

## Why earlier entry failures were misleading

The old loader stored import_fn without invoking it. Unknown functions were
bound to a generic function with an incompatible signature that returned zero.
The caller therefore continued after unsupported OS/CRT operations.

| Experiment | Before (d26d3f6) | Strict import resolution |
|---|---|---|
| Structural --info | exit 0, imports incorrectly 652 | exit 0, imports 651 |
| malloc(4096) | exit 139, PC +0xb705c, fault address 0x30 | exit 133: QueryMemoryInfo, symbol 65, caller +0x1398c |
| nninitStartup | real exit 1, no result logged | exit 133: AllocateTlsSlot, symbol 59, caller +0x133a0 |
| nnMain | exit 139, PC +0x27928, fault address 0x10000 | exit 133: ReferSymbol, symbol 363, caller +0x352f178 |
| DT_INIT | exit 139, PC +0xb705c | exit 133: __nnmusl_init_dso, symbol 515, caller +0x5920a34 |

The first checkpoint bound 22 host imports, including three FILE globals whose
cross-libc structure layout was not established. Review removed stdin/stdout/
stderr object binding; these remain unresolved until an explicit CRT bridge.
The strict resolver now binds 19 imports (632 unresolved), with no broad C++
layout claim. Optional byte-evidenced ReferSymbol adds one (631 unresolved).
nvnBootstrapLoader remains an explicit optional dlsym allowlist entry.

## Implemented changes

- The import callback returns a typed target function/object address from
  (symbol_index, name). The four-argument nso_load_file interface is unchanged.
- Each unknown function gets an identifiable fatal BRK sentinel. Unknown object
  imports point into an inaccessible mapping; reads produce the symbol identity.
  No trap returns a fabricated successful result. Sentinels do not interpret or
  translate any target instructions.
- MOD0 and dynamic offsets resolve in the mapped virtual image, instead of
  assuming every segment shares a constant file/virtual delta.
- Segment, string/symbol, dynamic, RELA/RELR and initializer extents are checked.
  DT_HASH supplies the exact symbol count; GNU-hash-only NSOs use a matching exact
  NSO dynsym header extent. There is no estimated-symbol-count fallback.
- R_AARCH64_RELATIVE writes base + addend. The prior extra r_offset was wrong;
  a positive independent fixture exercises this (main has no RELATIVE RELA).
  Symbolic relocation addends are retained.
- Fault logging no longer reads past x28, and bounds frame traversal to the
  current thread stack. An alternate signal stack is provided. Bionic/Linux
  AArch64 uses uc_mcontext.pc/regs rather than nonexistent gregs/REG_PC fields.
- Unsupported SVC handling remains fatal and cannot resume the same PC forever.
- CLI parsing rejects unknown options, malformed numbers and surplus arguments;
  debug reads from hard-coded game addresses were removed.
- The legacy static probe checks header/segment extents before reading them.
  No blocked malformed-input security-probe flow was resumed in this task.

## Initialization and TLS evidence

Static evidence comes from exact supplied binaries and existing clang/
llvm-objdump, not REA Evidence records. The REA MCP inventory was empty; the
installed launcher attempted npm package resolution and failed with ENOTFOUND.
No REA/client configuration, package, credential, or network setting was changed.

SDK SHA-256: 9dfd1cc6d3209d857dcafb191736265ac812401ddcc7b49dabf3bc0b114655a9.

- main nninitStartup +0x352f150 branches to +0x13370, not +0x2013370 as an older
  report claimed. Its first unsupported call allocates an nn OS TLS slot.
- SDK ReferSymbol +0x86670 and its C alias +0x86680 are each exactly one RET.
  A void link-anchor adapter can reproduce these observed specific semantics,
  but that would not establish game initialization success.
- SDK AllocateTlsSlot +0x143b60 returns zero on success and 0x1203 on exhaustion.
  It calls TlsManager::AllocateTlsSlot +0x144eb8, which enforces an application
  allocation count below 16 and writes a 32-bit slot value to the output pointer.
- SDK GetTlsValue +0x143c10 and SetTlsValue +0x143c30 read TPIDRRO_EL0, load the
  thread object at TLS +0x1f8, then access thread +0x80 + slot*8.
- main contains 1,489 MRS TPIDR_EL0 sites and no MRS TPIDRRO_EL0 sites. SDK contains
  124 MRS TPIDRRO_EL0, two MRS TPIDR_EL0, and three MSR TPIDR_EL0 instructions.
  Reading the same architectural register does not establish compatible layouts.
  No host thread pointer is written or replaced by this implementation.
- SDK QueryMemoryInfo implementation +0x14711c writes offsets +0,+8,+0x10,+0x18,
  +0x20,+0x28 and a 32-bit field at +0x30. It depends on GetInfo types 0x16/0x15
  and SDK memory-manager state. Output is not just two invented memory totals.
- SDK __nnmusl_init_dso +0x98e30 is 0x9d8 bytes, uses more than eight arguments,
  and maintains CRT/DSO state. No zero-return DSO initialization adapter is added.

## Reproduction and retained local evidence

Run from this worktree; every --out must be a new directory.

    python3 scripts/runtime/run_experiments.py --out .local/runtime/repro-next
    python3 tests/native/test_loader.py --out .local/runtime/contract-next
    python3 scripts/runtime/inspect_nso.py .local/uncompressed/main.bin \
      --out .local/runtime/static-main-next --slice 0x694bd0:44
    python3 scripts/runtime/inspect_nso.py .local/uncompressed/sdk.bin \
      --out .local/runtime/static-sdk-next --slice 0x143c10:52

- .local/runtime/baseline/{info,malloc,startup,main,init}: old executable and
  baseline commands, true return codes and separate stdout/stderr logs.
- .local/runtime/checkpoint-v1/manifest.json: compiler, host, input hashes,
  starting HEAD/dirty paths and complete commands/results.
- .local/runtime/checkpoint-v1/leaf-sample: exit 0, 16,875 cases, zero failures.
- .local/runtime/checkpoint-v1/{info,malloc,startup,main,constructors}: actual
  post-fix boundary runs; no timeout, exact outcomes in the table above.
- .local/runtime/contract-v1: positive synthetic NSO with distinct file/virtual
  segment offsets. Native imported sum(20,22)=42; resolver called exactly twice;
  RELATIVE plus direct/bitmap RELR checked. A separate ordinary unknown import
  produces its correct name and exit 133. The fixture is not game evidence.
- .local/runtime/static-{main,sdk}-v1/inspection.json: identities, dynamic tags,
  complete symbols, exact TLS opcode counts and requested slice hashes.
  Derived .S/.o/disassembly files stay local. Slice disassembly addresses begin
  at zero; add the recorded module_offset when interpreting them.

The first attempted strict build failed at the macOS ucontext include; strict/
and strict-v2/ preserve that intermediate history. Final checkpoint-v1 builds
pass -Wall -Wextra -Werror. Conclusions use checkpoint-v1 and contract-v1,
not failed intermediate launches.

## Remaining work

Game CRT/DSO registration, guest TLS context, memory manager, threading, graphics
and other OS surfaces are incomplete. Native initialization and nnMain stop at
the named unsupported calls above. Full game startup is BLOCKED, not passed.
RG477M execution/performance and independent QA were not run by this agent.

## Second checkpoint: narrowly evidenced HLE and portable leaf artifact

The optional NSO_WITH_HOST_HLE build resolves only ReferSymbol and its C alias to
a correctly typed void adapter. It logs the link reference; the supplied SDK's
single RET established the API has no OS initialization work in this build.
An exact-name extension resolver hook permits later typed graphics integration
without depending on graphics headers.

With this adapter, nnMain advances to nn::os::AllocateAddressRegion, symbol 68,
caller +0x13e70, x1=0xf80000000 (virtual reservation size), then exits 133.
No memory allocation success, physical memory budget or page contents are
fabricated. nninitStartup, malloc and DSO constructors remain at their previous
explicit boundaries. Command:

    python3 scripts/runtime/run_experiments.py --host-hle       --out .local/runtime/repro-hle-next

Guest TLS storage is a separate PoC, not bound to AllocateTlsSlot or any game
TLS access. Eight simultaneous native threads each own 16 slots; 128,000 value
checks pass, main-thread storage stays zero, an unrelated pthread key retains
its identity, and TPIDRRO_EL0 remains unchanged. No code writes a thread-pointer
register. This proves host-owned storage isolation only, not the guest ELF TLS
layout, SDK ThreadType layout, destructor policy, or a register-preserving
instruction adaptation.

An initial test incorrectly assumed TPIDR_EL0 also stays constant across host
scheduling: seven threads failed that assertion. A host-only control, which
does not call the guest storage APIs, independently observed seven changes.
The final test records TPIDR_EL0 changes as observations (four in the guest run)
and asserts actual slot isolation, host pthread-key identity and TPIDRRO_EL0.
The failed experiment is retained at .local/runtime/tls-poc-v1; the control and
corrected checks are .local/runtime/tls-poc-v2/{control,run}, both exit 0.
No claim of TPIDR_EL0 invariance is made.

Fault context, frame bounds and alternate-stack storage are now thread-local.
Freeing the installing thread's current module clears its diagnostic pointer
and restores its prior alternate-stack state. A positive contract test found
macOS rejects restoring its initial disabled zero-size stack with ENOMEM;
restoration now retains SS_DISABLE while supplying MINSIGSTKSZ, and passes.
Final restoration/named-import evidence: .local/runtime/contract-v5 (ordinary
functional contract exit 0; intentional missing platform call exit 133).
Earlier contract-v2/v3/v4 failures remain on disk.

Signal dispositions are still process-wide, and the fatal experiment handler
still uses stdio, which is not async-signal-safe. Installation is opt-in;
a guest must join its threads before freeing shared modules. This facility
does not implement recoverable production game exceptions or arbitrary
cross-thread module unloading.

### Android-target shared library

    python3 scripts/runtime/build_leaf_library.py       --out .local/runtime/leaf-libraries-next

The generator validates full input and leaf SHA-256, emits the original 44
bytes only into the private output, then builds two exports:
ffrs_leaf_694bd0 (original instructions) and ffrs_leaf_reference (60-byte compiled
mathematical oracle). A corresponding macOS dylib compares original code,
compiled C and Python mathematics over the same 16,875 cases, with zero mismatch.

Final artifact: .local/runtime/leaf-libraries-v3/libffrs_leaf.so.
Size 2,176 bytes; SHA-256:
8008f3722db61346eabc02969ff5a37aab9fc405409ed891f65036d7649704d8.
Format ELF64 little-endian, ET_DYN=3, Machine AArch64=183, no NEEDED entries.
The exported original function's size is 44 and its code SHA-256 remains
8a6e3c4fbd3164c966e2d164e0b59f140a162be1a4cf260b60beefae9921c9c9.
Full headers, symbols, native disassembly and build return codes are retained
beside manifest.json. This artifact contains no interpreter/JIT or stream
client; its two native code bodies are visible in disassembly.

Toolchain: existing Apple clang targeting aarch64-linux-android34 plus existing
rust-lld. The first linker invocation failed to find its bundled libLLVM; the
working command supplies the existing toolchain lib directory in a child-only
DYLD_LIBRARY_PATH, without changing installation or global configuration.
The failed link is retained in leaf-libraries-v1. This is a genuine freestanding
Android-target cross-build, not official NDK compiler acceptance or Android
execution. NSPRE-2/NSPRE-3/device gates and full T02 remain unmet.
