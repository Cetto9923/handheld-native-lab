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

## Third checkpoint: real address-space reservation

The next HLE implementation is deliberately limited to the observed
AllocateAddressRegion/FreeAddressRegion interface. It does not implement
AllocateMemoryBlock, MapPhysicalMemory, heap size, or QueryMemoryInfo.

Current SDK byte evidence:

- AllocateAddressRegion +0x144800 delegates to VammManager +0x145da0 (0x178 bytes).
  Its parameters are output uint64 pointer and size_t; the result is in w0.
  The implementation finds and inserts an interval in its address-region tree;
  it contains no physical-page commit operation.
- Success returns 0; metadata-node exhaustion returns 0x1003 and lack of an
  address interval returns 0x1803. Requested sizes below 2 MiB use 64 KiB
  alignment; larger sizes use 2 MiB alignment. Candidate ranges retain at least
  a 4 KiB gap on each side.
- FreeAddressRegion +0x144860 delegates to +0x146680 (0x108 bytes). It takes the
  address, finds the recorded size, invokes SVC 0x2d, removes the interval, and
  returns a Result. An unregistered address returns 0xe03. No syscall name is
  inferred beyond the directly observed SVC number.
- Full static byte/disassembly evidence:
  .local/runtime/static-sdk-vamm-v1 and .local/runtime/static-sdk-free-v1.

The host implementation reserves mmap(PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS),
with MAP_NORESERVE where defined. A side table records the exact owned mapping,
including host-page guards and alignment padding, for matching munmap. On hosts
with pages larger than 4 KiB, inaccessible guards use the host page size. The
supported normal size contract is nonzero and a multiple of 4 KiB. Unsupported
contracts are diagnosed, not silently treated as success. No MAP_FIXED, writable
large mapping, page-touch loop, or physical-memory total is introduced.

The independent normal contract reserves four simultaneous ranges: 4 KiB,
64 KiB, 2 MiB and 0xf80000000 bytes (62 GiB / 66,571,993,088 bytes). The
macOS mach_vm_region API independently confirms PROT_NONE at both ends of every
requested interval. The returned addresses meet the observed alignment rules;
matching release succeeds for all four. No reserved byte is read or written.
getrusage maxrss is 1,671,168 before and after (native host units, observed only).
Build and run exit 0; exact logs:
.local/runtime/address-region-v1/{build,run}.

    clang -g -O2 -Wall -Wextra -Werror \
      src/runtime/hle/address_region.c tests/native/address_region_contract.c \
      -lpthread -o .local/runtime/address_region_contract
    python3 scripts/runtime/run_capture.py \
      --out .local/runtime/address-region-next --timeout 10 \
      -- .local/runtime/address_region_contract

With the optional HLE resolver, nnMain now performs the actual 62-GiB virtual
reservation, then reaches unresolved symbol 7:
nn::os::InitializeMutex(MutexType*,bool,int), caller +0x277d8, x1=1, x2=0.
The child exits 133 with the full register/frame diagnostic. The reservation
has PROT_NONE and no physical contents are fabricated; the process exit releases
its remaining virtual mappings. No mutex layout is guessed to advance further.
Final command set: .local/runtime/checkpoint-v4-region/manifest.json.
All builds and the 16,875-case leaf comparison exit 0. Structural info still
reports 651 imports, 4,612 RELA and 1,370,974 RELR targets (630 unresolved with
these optional HLE adapters). malloc, startup and constructors stop at their
existing QueryMemoryInfo, AllocateTlsSlot and __nnmusl_init_dso boundaries.

The third checkpoint changes only new HLE address-region sources, its exact-name
resolver registration, the native contract, the optional experiment build list,
and this report. The shared loader API does not change. Full T02, real Android
execution, SDK TLS/CRT, mutex/threading, game startup and graphics remain open.

## Fourth checkpoint: byte-evidenced Mutex family

Starting HEAD was rechecked clean at 0b98b5a. Current SDK function-call ABI:

| API | SDK offset | Return |
|---|---:|---|
| InitializeMutex(MutexType*, bool, int) | 0x13f4f0 | void |
| FinalizeMutex(MutexType*) | 0x13f540 | void |
| LockMutex(MutexType*) | 0x13f550 | void |
| TryLockMutex(MutexType*) | 0x13f5a0 | bool in w0 |
| UnlockMutex(MutexType*) | 0x13f610 | void |
| IsMutexLockedByCurrentThread(const MutexType*) | 0x13f630 | bool in w0 |

InitializeMutex's observed stores establish state byte +0, recursive byte +1,
signed level +4, recursion count u32 +8, owner pointer +0x10 and critical word
u32 +0x18. Its nested CriticalSection::Initialize +0x1498b4 writes one zero u32.
Padding at +2/+3, +0xc..+0xf and +0x1c..+0x1f is not written. FinalizeMutex
clears the state byte only. Lock/TryLock update depth and owner; Unlock clears
owner and releases the critical section only when depth reaches zero.

C aliases at SDK +0x141190/+0x1411a0/+0x1411b0/+0x1411c0/+0x1411d0 are each
a tail branch through an exact GOT relocation to Initialize/Finalize/Lock/Try/
Unlock respectively. Evidence: .local/runtime/mutex-aliases-v1.json.

The first main allocator constructor at +0x27780 passes its embedded mutex at
object +8 and directly clears these same metadata fields before InitializeMutex.
It is therefore incorrect to assume the guest struct is completely opaque.
The adapter mirrors the observed fields and preserves all padding.

The implementation uses POSIX normal/recursive mutexes in a side table keyed by
the guest object's address. It performs real blocking, recursion, trylock and
release, and destroys native resources at Finalize. Registry references prevent
destruction while operations are active. Native errors and unsupported object
lifecycle operations are fatal, not generic successful returns.

Owner identity uses a dedicated inaccessible mapping per host thread; it is not
a host pthread pointer or host TLS address, and is not advertised as a complete
Switch ThreadType. The synthetic critical-word handle reserves bit 30 for
contention, matching observed critical-section instructions. Direct guest
dereference of this opaque owner token cannot silently read host thread fields.
GetCurrentThread, condition-variable waits and other ThreadType services remain
unresolved. This is a compatibility boundary, not a claim that all inline SDK
operations can run.

A full 99,000,832-byte main-text scan identifies 14,496 direct B/BL sites to the
nine Mutex imports: 1,218 Initialize, 4,119 Try, 3,950 Lock, 4,096 Unlock, 1,089
Finalize, plus 24 C-alias calls. Two additional direct sites reference condition
variable waits and remain unsupported. The scan resolves bounded PLT ADRP/LDR
GOT references, not arbitrary indirect calls or whole-program field alias flow.
No MRS TPIDRRO_EL0 instruction appears in main, but this does not establish ELF
TLS compatibility or prove absence of every inline field access. Main's 1,489
TPIDR_EL0 reads remain a separate unresolved issue.

Reproduction:

    python3 scripts/runtime/inspect_mutex_abi.py \
      --out .local/runtime/mutex-abi-next
    clang -g -O2 -Wall -Wextra -Werror src/runtime/hle/mutex.c \
      tests/native/mutex_contract.c -lpthread -o .local/runtime/mutex_contract
    python3 scripts/runtime/run_capture.py --out .local/runtime/mutex-next \
      --timeout 15 -- .local/runtime/mutex_contract
    python3 scripts/runtime/run_experiments.py --host-hle \
      --out .local/runtime/repro-mutex-next

Local static evidence:
.local/runtime/static-sdk-mutex-v1,
.local/runtime/static-sdk-critical-init-v1,
.local/runtime/static-main-mutex-first-v1,
.local/runtime/mutex-abi-v2/mutex-abi.json.
The JSON includes exact input/code hashes, function addresses and all direct
call-site offsets. The first selected constructor's field stores are preserved
in the private disassembly. This is bounded static analysis, not a proof of
complete compatibility across every object and call path.

Normal native contract .local/runtime/mutex-v2/{build,run} exits 0:
eight simultaneous threads perform 80,000 protected increments, zero overlapping
critical sections, correct recursive depth three, trylock false for a current
nonrecursive owner and another owning thread, trylock true after release,
100 destroy/reinitialize cycles at the same address, unchanged object padding
and unchanged per-thread TPIDRRO_EL0. The adapter writes no thread-pointer
register and calls only host pthread interfaces for synchronization.

Real main now passes the observed mutex initialization calls and stops at
unresolved atexit, symbol 6, caller +0x13ed4, with callback +0x3680.
The actual exit is 133, not a timeout. Logs and build commands:
.local/runtime/checkpoint-v5-mutex/manifest.json.
The original 16,875-case leaf comparison and structural loader remain exit 0;
startup/malloc/constructors retain their previously identified boundaries.
There is no game startup, Android execution, full CRT or full ThreadType PASS.

## Fifth checkpoint: functional exit registration, not fabricated SDK DSO init

Local SDK evidence establishes the public atexit calling convention at
+0x56c400: the incoming void callback moves into x1, a thunk at +0x56c420
(which branches to x0) becomes the argument-taking callback in x0, and x2 is
zero. The module-local helper at +0x5c0 stores 24-byte callback/argument/DSO
tuples and returns 0 or -1. In the current static bytes that helper explicitly
rejects a null DSO field. Therefore the missing SDK module/DSO initialization
and route cannot be declared understood or reproduced. The helper at +0x650
walks matching DSO records in reverse and accounts for registration during
cleanup. None of these static observations execute the SDK CRT.

The new implementation is a functional HLE replacement for C exit registration
and the C++ ABI callback interface, not a precise reconstruction of that missing
SDK CRT module context. The locally installed cxxabi.h explicitly documents
int __cxa_atexit(void (*)(void *), void *, void *) and
void __cxa_finalize(void *) under Runtime API 3.3.5.3.
DSO values in this replacement are opaque comparison keys; no SDK DSO handle
layout or __nnmusl_init_dso result is invented. __cxa_thread_atexit remains
unresolved because thread-exit lifecycle and real guest TLS are not implemented.

Implementation and lifecycle:

- Registration allocates and inserts a real typed record, returning 0 only on
  acceptance and -1 on failure. Plain void callbacks and callbacks with a void
  argument have separate types; no generic six-argument callback is used.
- Finalization selects records in LIFO order, optionally matching a DSO key.
  Records are removed before invocation. The list lock is released around guest
  calls, while a separate recursive finalization mutex serializes drainers.
  Reentrant/repeated finalization does not repeat a consumed registration;
  callbacks registered by another callback are subsequently processed.
- Only a genuine call to the typed guest exit adapter or __cxa_finalize executes
  callbacks. Normal exit drains then calls host exit(status); _Exit/_exit use
  immediate host termination without draining. Probe startup/registration never
  triggers cleanup merely to move the entry point forward.
- The CLI resolves HLE before its host dlsym allowlist. Its prior exit allowlist
  can no longer bypass the typed guest exit path in HLE builds.
- One current guest-module exit scope is supported. A second simultaneous scope
  or a scope entered while unrelated callbacks are pending is explicitly rejected.
  The optional host unload observer is installed after loading the CLI module;
  nso_free invokes it before unmapping. It discards pending exit records without
  invoking guest code, preventing retained callbacks into that unloaded image.
- Module scope teardown requires joined/quiescent guest threads. General
  concurrent module unloading, multiple guest processes in one host process,
  dynamic SDK DSO initialization and arbitrary cross-module callback ownership
  are not implemented. A probe call returning is treated as an abandoned probe
  scope, not as proof of a normally completed game process.
- The unload observer cleans EXIT REGISTRATIONS ONLY. Mutex and AddressRegion
  side tables are not associated with modules or generally cleaned by this
  observer. The whole HLE is consequently not advertised as supporting repeated
  arbitrary module loading/unloading or multiple games. The APK does not bind
  these HLE functions.

Static evidence:
.local/runtime/static-sdk-exit-v1,
.local/runtime/static-sdk-crt-adapters-v1,
.local/runtime/static-sdk-finalize-v1,
.local/runtime/static-main-exit-v1.
The first real main callback +0x3680 restores an allocator vtable and tail-calls
the FinalizeMutex import for its embedded object. It is recorded, not executed
during these game entry experiments.

Normal functional verification:

    python3 tests/native/test_exit_registry.py \
      --out .local/runtime/exit-registry-next
    python3 tests/native/test_loader.py \
      --out .local/runtime/loader-unload-next
    python3 scripts/runtime/run_experiments.py --host-hle \
      --out .local/runtime/repro-exit-next

.local/runtime/exit-registry-v5 records zero errors for LIFO, argument delivery,
DSO filtering, repeated/reentrant finalization, newly registered callbacks during
cleanup, 4,000 registrations across eight threads, concurrent finalizers, and
scope discard without execution. Normal native exit returns the requested 23
and executes one callback; immediate exit returns the requested 24 and executes
none. These nonzero statuses are intentional successful contract outcomes.

A well-formed synthetic AArch64 NSO additionally runs through the actual CLI
resolver and unload observer, not a duplicate resolver in the test. Its typed
guest exit returns 23 with exactly one native callback; _Exit returns 24 without
a callback; a returning register-only probe unloads with zero callbacks executed.
The test explicitly checks that resolving exit yields the guest adapter rather
than the host dlsym result. This fixture is infrastructure evidence, not game
code or game-startup acceptance. The earlier exit-registry-v2 launch did not run
its contract because its executable path collided with its log directory;
the runner was corrected, with the failed directory retained.

.local/runtime/contract-v6-exit validates the positive loader fixture, exact-once
host unload observation while mapped data is still readable, alternate-stack
restoration and named unknown imports. The ordinary contract exits 0; the
intentional unsupported import exits 133.

Final real-module evidence:
.local/runtime/checkpoint-v7-exit-final/manifest.json.
nnMain logs a successful real atexit registration (one pending callback, deferred)
and next stops at QueryMemoryInfo, symbol 65, caller +0x27a50, exit 133.
No registered game callback runs at this boundary. The module remains byte-
identical; its 16,875-case leaf and structural load both exit 0, with imports
651 / unresolved 620 / RELA 4,612 / RELR 1,370,974.

Current independent frontiers:

| Probe | First remaining unsupported call | Caller offset | Exit |
|---|---|---:|---:|
| nnMain | QueryMemoryInfo | 0x27a50 | 133 |
| malloc(4096) | QueryMemoryInfo | 0x1398c | 133 |
| nninitStartup | AllocateTlsSlot | 0x133a0 | 133 |
| DT_INIT | __nnmusl_init_dso | 0x5920a34 | 133 |

QueryMemoryInfo still requires a coherent guest-owned memory model and accounting
backed by real allocations, not invented total/free RAM. Guest ELF TLS, SDK thread
state and DSO/CRT initialization remain separate unresolved layouts/lifecycles.
There is no full game startup, Android execution or complete T02 PASS.


## Sixth checkpoint: real CRT gate and explicit initializer ownership

This unit starts at 5e165d08a22d6b19826467ef0ed861ce168bb5fb. It does not add
__nnmusl_init_dso to the HLE resolver. The true main initialization still stops
at import 515, caller +0x5920a34, exit 133. A separate, explicitly terminating
diagnostic establishes what the original SDK would do at its first gate.

### Actual machine arguments and original SDK result

Main's DT_INIT +0x5920950 prepares 21 fixed GP words: x0..x7 and thirteen
8-byte stack arguments at sp+0 through sp+96. Its BL at +0x5920a30 calls PLT
+0x5e699d0, whose relocation identifies __nnmusl_init_dso. The test's fixed-word
C signature is a diagnostic for that observed machine ABI, not a recovered
source prototype or permission to use a six-argument generic wrapper here.

All 21 input GOT slots are DT_RELR targets. Every received argument is B plus
the corresponding original GOT word, where B is the main mapping base. This
includes the words 0 and 8: their actual arguments are B and B+8, not null or
a literal scalar alignment value. The native observer checks each actual
argument against its actual relocated GOT slot.

| Argument | Register / stack byte offset | GOT slot | Value minus B |
|---:|---|---:|---:|
| 0 | x0 | 0x776f120 | 0 |
| 1 | x1 | 0x776f128 | 0x5e67aa4 |
| 2 | x2 | 0x776f130 | 0x7767108 |
| 3 | x3 | 0x776f138 | 0x7767328 |
| 4 | x4 | 0x776f140 | 8 |
| 5 | x5 | 0x776f148 | 0x7767110 |
| 6 | x6 | 0x776f158 | 0x7767328 |
| 7 | x7 | 0x776f160 | 0x7768121 |
| 8 | sp+0 | 0x776f170 | 8 |
| 9 | sp+8 | 0x776f168 | 0x7767330 |
| 10 | sp+16 | 0x776f118 | 0x7771fe8 |
| 11 | sp+24 | 0x776f150 | 0x7773398 |
| 12 | sp+32 | 0x776f110 | 0x5e6b098 |
| 13 | sp+40 | 0x776f108 | 0x5e82630 |
| 14 | sp+48 | 0x776f100 | 0 |
| 15 | sp+56 | 0x776f0f8 | 0 |
| 16 | sp+64 | 0x776f0f0 | 0x5e82630 |
| 17 | sp+72 | 0x776f0e8 | 0x5e860f8 |
| 18 | sp+80 | 0x776f0e0 | 0 |
| 19 | sp+88 | 0x776f0d8 | 0 |
| 20 | sp+96 | 0x776f0d0 | 0x7767328 |

Arguments 3, 6 and 20 coincide with this main's verified DT_DYNAMIC address.
The callee field writes provide stronger semantic evidence than names guessed
from adjacent data. At SDK +0x98e30 relative offsets +0xb4..+0xd4 it computes
(a5-a2)=8, a4-(a5-a2)=B, and (a9-a6)=8. At +0x1f4..+0x218 it writes a2,
(a3-a2)=0x220, (a7-a2)=0x1019 and maximum alignment 8 into the first four
64-bit fields of its allocated 0x148-byte record. Interpreting these as a TLS
template pointer, initialized size 544, aggregate size 4,121 and alignment 8 is
an inference from field use; the original parameter/structure declarations
remain unavailable. The dynamic-table parser adds the derived B to table
pointer values. These findings do not establish a complete guest thread layout.

The original SDK's first dependency is
nn::os::MemoryAllocatorForThreadLocalInitialized() at +0x144620, reached through
its own PLT. The getter checks an SDK-owned allocator function pointer. The
setter +0x1445e0 accepts non-null allocate/free pointers, rejects reinitialization,
and stores both; no setter is invoked by this experiment. With a fresh mapped
SDK, the getter actually returns 0. Calling the original, unmodified
__nnmusl_init_dso with all 21 captured main arguments then actually returns 2
through its early-return branch. SDK DT_INIT, SDK constructors, DSO allocation,
TLS setup and callbacks have not executed.

The observer exits 86 immediately after logging this result, before main
consumes it or enters ProtectRelro/constructors. This nonzero status is an
intentional diagnostic stop, not a game or CRT success result. Both capture-only
and SDK-gate modes pass their observations with exactly 21 matching arguments;
both return 86, with no timeout. The normal strict resolver continues to leave
__nnmusl_init_dso unsupported.

Going further by merely setting the allocator would be unsafe and semantically
incomplete. The subsequent path uses SdkMutexType, allocates DSO tables/records,
counts TLS relocations and computes thread storage. Its internal helper at SDK
+0x99930 contains MRS TPIDRRO_EL0 at +0x99b4c, a store to [TPIDRRO_EL0+0x1f0] at
+0x99b50 and MSR TPIDR_EL0 at +0x99b54. These instructions were inspected
statically only. They are not executed on the host. No host TP register or host
TLS memory is replaced, and gate=false is never bound as fabricated CRT success.

### Constructor ownership is now explicit

This main's DT_INIT wrapper already walks its DT_INIT_ARRAY: after the DSO call
it skips its remaining work when w0==1; otherwise it calls ProtectRelro, then
walks from the GOT-derived array start 0x67d5000 to end 0x67d8b10. These exactly
match dynamic array offset 0x67d5000 and size 0x3b10 (1,890 entries). The SDK's
own DT_INIT +0x400 uses the same wrapper pattern. Running a generic ELF
DT_INIT-then-array walk after either wrapper can therefore duplicate constructor
execution once the current earlier platform boundaries are resolved.

The loader now exposes nso_run_init_policy with NSO_INIT_ELF and
NSO_INIT_DT_INIT_OWNS_ARRAY. The latter runs DT_INIT and leaves its array to the
verified wrapper. With no DT_INIT, either strategy explicitly walks the array
once. Legacy nso_run_init remains documented as the generic ELF strategy; it is
not used for this main. No strategy skips CRT initialization or invents its
state.

The CLI requires the caller to choose --init-wrapper or --init-elf. Ambiguous
--init is rejected with exit 2; it does not infer a strategy for an unidentified
NSO. The reviewed main experiment and CRT diagnostic explicitly select the
wrapper strategy. The stale nso_enable_svc_trap API was removed: its old
implementation only printed a message and installed no SVC compatibility, while
its header misleadingly claimed interception. Fatal SVC behavior is unchanged.

A valid synthetic native fixture has a DT_INIT wrapper report event 1 and call
its array function (event 2). Wrapper policy reports [1,2], count 2; explicit
ELF and legacy generic policies report [1,2,2], count 3. Array-only fixtures
report [2], count 1 for both strategies. These are deliberately ordinary
lifecycle examples, not game bytes or a malformed-input/security QA flow.

### Reproduction, evidence and next feasible host work

    python3 scripts/runtime/probe_crt_init.py \
      --out .local/runtime/crt-init-next
    python3 tests/native/test_init_policy.py \
      --out .local/runtime/init-policy-next
    python3 scripts/runtime/run_experiments.py --host-hle \
      --out .local/runtime/entry-policy-next

Evidence directories:

- .local/runtime/crt-init-v3/manifest.json: input identities, code-range hashes,
  all argument/GOT values, build exit 0, capture exit 86 and SDK-gate exit 86.
- .local/runtime/init-policy-v1: both native policy counts, legacy semantics,
  array-only fallback, strict CLI rejection. Functional cases/builds exit 0;
  ambiguous --init exits the expected 2; no timeouts.
- .local/runtime/static-main-dso-v1: exact caller +0x5920900 instruction slice.
- .local/runtime/static-sdk-dso-v1: entire exported +0x98e30 implementation.
- .local/runtime/static-sdk-dso-gate-v1: getter/setter and SDK DT_INIT wrapper.
- .local/runtime/static-sdk-dso-mutex-v1: SdkMutexType calls and the internal
  helper containing the guest TP write sites, inspected without execution.
- .local/runtime/checkpoint-v8-init-policy: new host builds and the actual
  --init-wrapper path. Leaf 16,875 comparisons and --info exit 0; all four game
  entry boundary probes still exit 133. No boundary is recategorized as startup.

The next meaningful host step is a separately owned, bounded per-thread guest
TLS image: confirm template/zero-fill/alignment and all thread-relative offsets,
then test copy/zero-fill/isolation and host TLS preservation without executing
game accesses. Connecting that storage to original code additionally requires
an evidenced native adaptation for every relevant TP read/write site, preserving
registers and return state. The SDK helper above must not execute its host-TP
writes. SDK object/DSO ownership, private mutex/allocator dependencies and
startup order also require an explicit process context. Mapping the SDK and
letting its native CRT continue is not presently safe or compatible.

This bounded TLS/context research is feasible without a device. Full game CRT
startup has not become feasible merely from the gate observation; it still
needs substantive platform adaptation. No Android execution, game initialization,
RG477M performance, independent acceptance or overall T02 PASS is claimed.


## Seventh checkpoint: independent module TLS template storage

Starting HEAD: 92c7b9bb5408e8c01060813c1b9ec05040288db9. The new
src/runtime/hle/module_tls.c/h implements reusable owned storage, not a guest
thread runtime or an SDK initialization adapter. The existing 16-slot guest_tls
PoC and nso_host_resolve are unchanged. No allocator gate is set to true.

A descriptor supplies an already-relocated initialized-data pointer, initialized
length, total length, payload alignment and reserved prefix length. Synchronous
nso_module_tls_create allocates a real image, zeroes its storage and copies
exactly the initialized bytes. It returns 0 only on successful allocation/copy,
or a POSIX error value, with explicit parameter/size-overflow checks. No template,
module or current-thread pointer is retained. The caller must keep the source
readable and its module alive until create returns. Afterwards the image owns
its copy independently of the source.

The opaque instance exposes a const metadata view with mutable anchor/payload
storage. The payload obeys the requested power-of-two alignment; host allocation
alignment is at least pointer alignment. Extra allocation padding may precede
the anchor when the requested prefix is not a multiple of that alignment. The
reported storage_size is the rounded anchor-to-end span, not those preceding
host allocation bytes. Free releases the instance and allocation without running
callbacks. Callers must quiesce image users first; sharing an image requires
ordinary caller synchronization. There are no global image pointers, pthread
keys, automatic destructors, register writes or instruction rewrites in this
component.

For the evidenced main descriptor, the prefix is 16, initialized size 0x220,
total size 0x1019 and alignment 8. The resulting storage span is 0x1030:

| Anchor-relative bytes | Contents | Length |
|---|---|---:|
| [0,0x10) | Zero-filled reserved prefix; TCB/header semantics unmodeled | 16 |
| [0x10,0x230) | Copy of initialized module template | 544 |
| [0x230,0x1029) | Zero-filled module TLS/BSS tail | 3,577 |
| [0x1029,0x1030) | Zero-filled allocation padding | 7 |

The anchor models only the candidate address relation payload=anchor+0x10.
Neither it nor its first 16 bytes is a complete SDK ThreadPointer/TCB. No host
TP register is directed at this image, and no original game instruction reads
or writes through it during these experiments.

### Normal functional and lifetime evidence

    python3 tests/native/test_module_tls.py \
      --main .local/uncompressed/main.bin \
      --out .local/runtime/module-tls-next

The ordinary C contract uses a nontrivial synthetic initialized template and
valid empty/all-initialized/zero-initialized templates, including alignments
1, 8, 64 and 256. Prefix-to-payload distance and payload alignment are checked;
copying remains valid after the source buffer is overwritten and freed.

Eight simultaneous threads create separate main-sized images. While all images
are live, their anchor-to-end ranges are checked for non-overlap. Each verifies
the template, reserved prefix, zero tail and allocation padding, then writes
its own values at payload offsets 0, 0x21f, 0x220 and 0x1018. The last is
anchor+0x1028, the highest directly evidenced main TLS byte. Repeated payload
checks total 32,968,000 expected byte values, with zero errors. The shared
source remains unchanged. Unrelated pthread-key values and compiler TLS canaries
remain correct; TPIDRRO_EL0 is unchanged for all eight threads. Seven observed
TPIDR_EL0 changes are recorded, consistent with the previously retained host-only
scheduling control; that register is not written by this component or assumed
constant across scheduling.

The private-source harness loads main with the native loader, so relocation has
already completed, and copies the actual mapped [0x7767108,0x7767328) template.
It does not call main, DT_INIT, constructors or any original game instruction.
Two images are created while the module is alive; the module is then freed.
Both copied images remain correct, one image's mutation does not affect the
other, and the second remains valid after the first is freed. The Python byte
oracle independently checks the complete 4,144-byte image against zero prefix,
exact source template, zero tail and zero padding. It never copies the module's
ordinary dynamic/GOT bytes after 0x7767328 as TLS data.

Final local evidence: .local/runtime/module-tls-v2/manifest.json. Both host
builds, synthetic threaded contract, Android-target object compilation and
mapped-main copy/lifetime test exit 0, with no timeout. Source main SHA-256 before
and after is unchanged at
6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5.
The 544-byte template contains 11 nonzero bytes and has SHA-256
2236311146eecaa733a64cda6108fece94e6693159ba1d3add9bb3f0899834bf.

Private artifacts, retained only under that experiment directory:

- private/loaded-template.bin: 544 bytes, template hash above.
- private/copied-image.bin: 4,144 bytes, SHA-256
  0516b0ba6d9225c86451c81d5b7ca2bffc493b3a7a711fe5ef76180d4995b111.
- module_tls.android.o: 2,056 bytes, SHA-256
  25dee6da71f41b6d0d916482510a0cb21ccba0e045ffd9fbb06741e6a76bd805.
  This is ELF64 little-endian AArch64 ET_REL, produced by existing AppleClang 21
  targeting aarch64-linux-android34 with the previously supplied official NDK
  sysroot headers. The driver accepts that existing sysroot through
  --android-sysroot. This unit does not link a new NDK runtime or execute Android.

### Remaining integration boundary

The first 16 header bytes, full ThreadType/TCB structure, SDK DSO tables and
module/thread ownership are still unmodeled. Main's actual TLS constructor at
+0x34a0 reaches __cxa_thread_atexit at +0x34e8; storage alone does not implement
that destructor lifecycle. This component registers no thread-exit callback and
does not reuse ordinary process atexit as a substitute.

The 1,489 main TPIDR_EL0 reads and SDK TP read/write sites still require an
accurate native adaptation. The separate caller research resolved constant
patterns at 1,440 sites; 49 non-adjacent uses remain without complete data-flow
verification. The unsafe original SDK helper identified in the sixth checkpoint
is not executed. Existing CRT/nnMain boundaries remain unresolved; this storage
component is not bound into the HLE resolver, does not enable __nnmusl_init_dso,
and does not change APK behavior or any acceptance gate. T02 remains IN_PROGRESS.
