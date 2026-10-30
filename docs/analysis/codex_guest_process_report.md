# Owned startup profile: native boundary integration

## SDK thread-local HOST consumers

This unit admits exactly the reviewed C exports nnosAllocateMemoryForThreadLocal
(one64-bit size -> pointer) and nnosFreeMemoryForThreadLocal(pointer,size -> void).
SDK144640 supplies fixed align16 to the registered allocate pair;144650 skips
null pointers before touching the pair. Actual callback NULL is forwarded as
NULL; invalid/busy/domain failures terminate, never a guessed SDK success.

Consumers now use the old nb_call path from IDLE and the already validated
nb_call_from_host only from actual-primary HOST phase2. No scope/import binding
is created there. The HOST packet explicitly seeds actual active_guest_x18;
the boundary restores its outer scope, and callback x18 output never overwrites
the Process cache. That cache is updated only by the original IDLE call path.
No Process mutex spans guest callbacks. A preallocated uncertain identity is
published before invocation so even in-flight owners are reachable; busy
recursion is rejected before further side effects. Completed exact frees remove
records, while failure/unknown results retain code/owner. Backing remains counted
by the existing sole page owner, with no second physical/suballocation charge.

The self-authored fixture covers guest->HOST allocate->guest pair->HOST Query,
zero-byte allocation, real NULL return, null free inside an active callback,
exact free sizes and multiple pointers. Its outer guest x18 A=41 differs from
the Process's prior cached55; allocator/free change x18 to B=66/C=77 and outer A
must survive. The separate SCS/testing binary probes busy rejection and live
in-flight records from Query. Uninitialized and recursive HOST import cases
are explicit expected-fatal contracts. Mac native execution remains unsupported;
incremental hashes/raw codes and new Android ELFs are in the private checkpoint.
These functional hooks do not install ELF templates/TCBs/DSO destructors, invoke
SDK TP-writing initialization, or establish complete CRT/startup/main support.

Checkpoint tls-host-consumer-v1 recompiles only changed Process/contract objects,
reusing source/hash-matched f60/585 inputs. Its25 captured steps all meet their
expected codes without timeout: ordinary host regressions and null-free pass,
inactive allocate terminates with raw SIGABRT(-6), and Mac native fixture calls
return explicit unsupported. Three Android ELFs link successfully; normal/CLI
exclude test hooks, while the recovery binary instruments real host SCS and
busy/metadata probes. Android execution is NOT_RUN here and belongs to the
coordinator. The private manifest/freeze and device-argv.json preserve all
commands, hashes and expected-fatal cases.

## Native primary identity subset

The585 combined RG run printed its entry result before the original OS-slot
destructor18c60 stopped at GetCurrentThread106. That is post-entry cleanup,
not a new call in startup's body; the undefined x0 of void startup is not a
success result. Hash-matched SDK140bd0 loads ThreadType* from TPIDRRO+1f8;
140d70 GetThreadId loads a full64-bit x0 from ThreadType+1b8. Main18d64/18d68
only passes that pointer between these APIs, then stores low32 bits of the ID.

This provider implements an explicit opaque native API subset: the actual
managed primary's real gettid (Android) or pthread_threadid_np (Darwin) plus a
dedicated PROT_NONE host page. It exposes neither pthread_t nor a host control
object or guessed SDK fields. Direct token-field reads encounter protected
memory; foreign tokens are rejected without dereferencing them. Only the current
primary's token is accepted, including
the entire normal OS5-round cleanup. The token is acquired before publication,
invalidated when that primary leaves, and retired pages remain owned until
quiescent destroy, preventing address reuse between this Process's primaries.
Real munmap failure retains owner/pages/code for retry. Page and control-node
costs are explicit host administrative VA/metadata, outside the configured
guest backing and resource-pool budgets; they are not measured Switch RAM.

Contracts cover actual TID/stability, fresh/inactive/foreign output sentinels,
protected tokens, two retired generations, real map failure/callback withholding
and failed-unmap retention/retry. A self-authored native fixture verifies both
full-width returns/x18 and a nonnull OS destructor invoked with zero while the
identity remains live. Separate SCS/testing binaries retain hook separation.
Incremental source/object hashes and real codes are recorded in the private
primary-identity checkpoint. No ThreadType/TCB memory layout, arbitrary thread
lookup, CreateThread support, SDK TP writes or complete CRT/TLS is supplied.

## Fresh Process registration correction

Independent QA reproduced cfc's ordinary create-without-load API crash: the
registration function called nonnull-only nso_base before checking its module
lifecycle (raw SIGSEGV -11/shell139). Registration now verifies the actual live
primary and module before reading its base. A fresh Process returns typed
NSO_INVALID/EINVAL with no module/callback/allocation mutation. The new ordinary
contract preserves Query/allocation output sentinels, destroys a never-loaded
Process, then repeats create/reject followed by successful load/registration/
run/destroy. The full builder includes it; this correction's private incremental
build reuses hash-verified cfc objects and fixtures and recompiles affected code.
The original cfc source/artifacts and QA failure remain unchanged.

## Earlier checkpoint: allocator pair and limited IDLE consumers

Historical cfc checkpoint only. Its IDLE-only and HOST-unsupported statements
below describe that frozen implementation; current HOST support is at the top.

The b197 RG checkpoint passed both libm contracts and reached import69 after
the original allocator constructor mapped1286144 and1048576 bytes. SDK1445e0
only registers a nonnull pair once at+b91008/+b91010; null/repeat abort, and
registration makes no allocation. Initialized144620 tests the allocate pointer.
Allocate144640 passes(size,16); Free144650 skips null, otherwise passes(ptr,size).
The cached40 instructions were rechecked against SDK SHA9dfd1cc6...655a9.
Main13830/13870 route through the real allocator object's vtable+30/+50. Startup
1381c tail-branches after restoring LR, explaining the host LR/unsigned offset
underflow in the69 trap; it is not a usable guest caller offset.

This unit stores validated module-text callbacks until real module release.
Public consumers require the actual registered primary and an IDLE prepared
gateway; null free requires no prepared binding and makes no callback. Nonnull
free requires the same process's exact pointer/size record. Host administrative
records are allocated before guest side effects, with no Process lock across
the guest call. Query/page import reentry can therefore use the existing locks.
Callback failure or an uncertain result retains the identity, pair and code;
live records refuse destroy before pages/module/unload-observer changes. No
host free, automatic callback retry, separate physical charge or parallel VM
ledger is introduced. Returning null is a native typed allocation failure, not
a guessed SDK Result. Returned storage is opaque callback storage; these
records do not establish a general allocator's internal layout or ownership.

The self-authored native fixture registers the pair without invoking it, then
allocates distinct module-owned buffers through the gateway, records exact
size/alignment and frees in reverse order. Each callback reenters real Query.
The separate SCS-instrumented recovery contract checks host/guest x18, metadata
failure before any guest call, and actual HOST-phase consumer rejection. The
production ELF contains no testing hook. A separate live-allocation contract
intentionally retains two allocations plus module/pair until isolated process
shutdown; that result is not successful cleanup. Mac refuses native guest
execution; host registration/null/repeat and ordinary contracts remain real
host tests. Build logs, exact products and device argv are under the new private
`.local/tls-provider-v2/` manifest/freeze; Android execution is coordinator-owned.

SDK ELF consumers98a44 (48B destructor node),9ad50 (TLS aggregate) and their
free paths remained unbound at this historical checkpoint because imported
HOST services lacked same-scope reentry. Current HOST reentry is implemented;
the ELF consumers still need actual DSO/guest-TP/destructor integration. OS32-slot
scalar storage is unrelated, and the existing host module-template mirror is
not installed as SDK ThreadPointer/TCB. No SDK TP-writing path, original module
patch, full CRT/ELF TLS initialization or complete constructor success is claimed.

## Scalar libm import: logf

The coordinator's frozenff RG run passed the new comparison/capability contracts
and info0. Both original startup command lines now reserve62GiB VA and reach
logf153, caller+0xb3eac, strict133, with original frames
+0x137f4 -> +0xae93c -> +0xaf320 -> +0xb3eac. This is the real allocator
constructor chain, not completion of it or of CRT/TLS. Pre-init malloc139 is
not reinterpreted as a pass and is not repeated in the next device plan.

Bounded LLVM/byte/relocation evidence confirms +0xb3e98 converts w19 to s0,
+0xb3ea8 calls PLT+0x5e68440 -> GOT+0x77724b8 -> JMPREL+0x5e83458,
R_AARCH64_JUMP_SLOT/symbol153=logf. The returned s0 is immediately doubled at
+0xb3eac and converted to unsigned w8 at +0xb3eb0. Details and unchanged main
hash are in `.local/math-callsite-static/facts.json`; no successful REA Evidence
is claimed. No adjacent additional math import is proposed from this slice.

Only standard `<math.h>` float logf(float) is admitted, directly from system
libm. Native metadata is GP0, FP arguments1/returns1, register-only; the existing
gateway forwards s0 in its raw q0 register, restores the correct x18 domain and
retains normal scope/SCS rules. No CLI integer-to-float convention, custom math
algorithm, special-case adjustment or floating control-mode write was added.
Android ELF inspection requires UND logf and DT_NEEDED libm.so, using the existing
-lm link. Unknown math imports remain strict unresolved.

`.local/math-runtime-v1/manifest.json` contains101 expected commands, no timeout.
Host normal/recovery/contracts remain0; new direct logf evidence covers10
positive varying/boundary values against numeric tolerances and5 IEEE cases
against unchanged system behavior. The self-authored logf.nso fixture loads its
input with LDR s0, invokes the actual imported native target and stores returned
s0, while deliberately clobbering guest x18. Its15 calls verify input storage,
nonconstant outputs and x18; the integer entry argument is only an object
pointer. Mac explicitly refuses guest execution with no raw fallback. Android
FP/native execution and the next original startup gate are pending coordinator
RG runs; all products/hashes/argv are recorded in freeze. No loader, Context,
pages, boundary, CLI or original game bytes changed in this unit.

## Startup continuation: imports61/67

The coordinator's frozen3db RG run passed 13 component contracts and info0.
Original empty startup reached IsVirtualAddressMemoryEnabled67/+0x137a4/133;
nonempty reached strcasecmp61/+0x1345c/133. Standalone pre-init malloc reserved
0xf80000000 VA and then faulted139 at main+0xb705c/address0x30; CRT was NOT_RUN
after that failure. This is not a malloc pass or a resource-provider failure.
Its zero-vptr object/required original startup writer is tracked separately;
this change does not patch the object or add a startup-first execution mode.

Bounded new SDK slices (hash unchanged) confirm strcasecmp+0x59bdb0/148B:
unsigned byte loads, the same four tolower PLT targets+0x64f2f0 already resolved
by the locale followup, and an exact signed folded-byte difference. The typed
two-pointer import calls mature Bionic strcasecmp on Android; host evidence
uses fixed ASCII folding without depending on global locale. No new aliases,
locale objects or unrelated string APIs are admitted.

IsVirtualAddressMemoryEnabled export+0x1448b0 delegates to the VammManager method
at +0x145c6c/64B. That body ignores its object argument, requests Info16 for the
current process and returns canonical w0=(query success && total!=0). It does
not require a pre-existing allocation or positive remaining quota. The native
provider obtains the same locked Process/pages/resource snapshot used by Query,
requiring the actual primary and live sole VA owner, allocator identities,
coherent ledgers and no pending/retained/quarantined state. A valid explicit
resource policy with nonzero arena total yields1; an unconfigured policy yields0.
Invalid/missing owner leaves the public API output untouched and the guest
getter fails fatally. It reports only the admitted owned reserve/map/exact-free
policy, not full Horizon VM capabilities. Logs include live region/allocation
counts before and after actual VM operations; no unconditional true stub exists.

`.local/startup-next-static/facts.json` records exact words/BL targets and reused
relocation evidence. LLVM slices/run all0; no REA success is claimed or retried
after the already recorded pre-engine Swift helper failure. New build evidence
is `.local/startup-next-v1/manifest.json`: 97 expected commands, no timeout.
Host strcasecmp's 65536 u8 pairs and VA capability's five real VM stages under
both enabled/disabled policies all0; invalid capability retains real SIGABRT.
Self-authored strcasecmp.nso and va-enabled.nso cover six int32 results and ten
canonical bool returns/x18 crossings on Android. Products are cross-built only
here; the new original startup gate awaits coordinator RG execution. Main input
hash is unchanged; no Context/pages/region/loader/CLI code changed in this unit.

## Explicit VA management resource provider

New opt-in constructors retain the old two-field config/constructor ABI. CLI
`--guest-resource-quota <bytes>` supplies a positive exact host-page multiple;
there is no default. Without it, GetVirtualAddressMemoryResourceUsage stays
unresolved. The real RW anonymous arena uses 256-byte slabs (32-byte header,
224-byte payload), lazy allocation and a checked free chain. Application backing
quota remains separate. Real Context charge tickets, page region/allocation
nodes and raw VA reservation/interval nodes allocate from this bounded pool;
there is no malloc fallback when enabled or second interval ledger. Pool used
counts occupied slabs, including padding, and enforces an actual allocation
limit before VM mutation. It is explicit native metadata policy, not Horizon
page-table allocation cost, physical/RSS usage, or host RAM. Native control
objects, loader symbols/thunks, pthread/TLS and unrelated HLE metadata remain
host administration outside this resource domain.

A Process snapshot holds Process/pages serialization, verifies pool headers,
real ticket owners/amounts and every page/raw node's allocator identity, then
compares those with the existing sole ledgers. Lock order is Process -> pages
-> raw region -> terminal Context, or pages -> Context. Context never calls
back into the other owners. Page-node reservation/publication share its lock;
other Context providers and raw APIs must be quiescent for a complete snapshot.
Pending, retained or quarantined VM ownership cannot produce valid guest data.
Free/decommit release their real nodes/tickets; VM rollback uncertainty keeps
ownership. Context destroy refuses live slabs and only drops the arena after
successful munmap; failure retains the Context/mapping for a real retry.

The existing SDK+0x146930 slice confirms no-argument dual-u64 return:
x0=Info16 total; x1=min(total,Info17 used+0x80000). The original 512KiB reporting
margin is retained, without calling it mapped/physical storage. A reviewed
`__uint128_t` bridge and gp_returns2 use the existing native gateway. Invalid
snapshots fail fatally; no SVC fallback/default0 path is admitted. LAS fixed
[fe66 kernel.c](https://raw.githubusercontent.com/peiikai/LASRecomp/fe66f31a6dbcaee1faa1e6718805cb2dbcc67a93/runtime/kernel.c#L695-L721)
returns constant0 for Info16/17, so that implementation was not reused.

The separate locale followup verified SDK strncasecmp_l+0x59c7e0 (172B) ignores
x3 and uses unsigned ASCII folding with exact signed difference. A typed four
argument bridge discards the opaque guest locale and calls Android's mature
Bionic non-_l strncasecmp. Host evidence uses a deterministic ASCII-only path;
no host locale_t cast/global locale mutation or other locale API was added.
Full primary-source/LLVM provenance and the unsuccessful REA attempt are in
`rea-followups/locale-compare/LOCALE_COMPARE_REPORT.md`; source hash is recorded
in `.local/va-resource-v3/source_evidence.json`. Android ELF inspection confirms
an undefined system strncasecmp import and no system strncasecmp_l import.

`.local/va-resource-v3/manifest.json`: 88 expected commands, no timeout. Host
normal/recovery/resource/locale contracts0; existing strict abort/unsupported/
usage checks retain their actual nonzero exits. Pool tests cover exhaustion
before VM, allocation rollback, reuse, 8 threads/8000 allocations and 400
concurrent VA mutation/snapshot cycles, ending at used0. Normal resource tests
exercise 1MiB, 2MiB and one-host-page policies, actual node deltas, separate data
charges and the SDK's capped reporting margin. Recovery retains nodes/tickets
on protect+restore failure, then genuine whole release; arena mmap failure and
munmap failure/retry are also covered. Locale covers all 65536 unsigned-byte
pairs, zero n/no load, NUL/length bounds, >32-bit n and arbitrary ignored locale.
Self-authored resource.nso and locale.nso exercise dual GP returns and six
four-register native comparisons with guest x18 clobbered. Android execution
is NOT_RUN here; coordinator owns RG. V1/V2 raw logs/artifacts remain preserved.
Original main hash is unchanged; next real startup gate awaits RG. CRT/ELF TLS
and ThreadType remain unresolved.

## Owned guest command line checkpoint

`guest_process_create_with_arguments` accepts a complete explicit guest vector,
including the caller-selected argv0, and copies it into one page-rounded RW
mapping with a NULL tail. The original create API means an explicitly empty
command line; even that vector owns and charges one host page. The ordinary C
input contract requires stable readable strings/vector until create returns.
Host metadata caches source pointers/lengths, and quota is reserved before mmap,
then committed after the copy. Query verifies a third private ticket and adds
its actual mapped extent to used/baseline. These remain diagnostic port-policy
logical backing values, not original SVC measurements.

The mutable guest vector never selects a free address: private rawbase/extent
and ticket survive vector/string changes. Normal OS destructors, actual module
unmap and boundary cleanup all precede argument unmap; only successful unmap
permits refund. Failed unmap retains the Process/mapping/charge for retry.
Host metadata and native pthread administration retain their existing distinct
accounting policy. No loader, Context, boundary or OS component changed.

The exact SDK getters are `_ZN2nn2os11GetHostArgcEv` (SDK+0x143690, w0)
and `_ZN2nn2os11GetHostArgvEv` (+0x1436b0, x0), both without arguments.
Hash-matching bounded LLVM evidence is saved in `.local/guest-argv-plan/`;
this is ordinary tool evidence, not REA Evidence. Both imports now bind typed
register-only getters over real owned storage. CLI repeats `--guest-arg <literal>`
before the mode; the full guest vector is solely these values and never the
host diagnostic command line. Explicit quota/stack are still required.

`.local/guest-arguments-v1/manifest.json` captures 68 expected commands, no
timeout: ordinary host contracts0, invalid query/provider SIGABRT, unsupported
Mac guest execution1, untyped guard compile1, missing-policy usage2. New normal
storage cases cover empty/three strings/empty string/multiple pages, caller
mutation after create, guest vector mutation, NULL tail and Query charge/padding.
Recovery observes pending quota before mmap, quota refusal before map, real
mmap failure rollback, and failed unmap retaining readable storage until the
second attempt genuinely unmaps it. Existing five loader recovery cases still0.
A self-authored native fixture calls both getters with guest x18 clobbered;
the OS cleanup fixture also reads argv from a real destructor callback. These
Android paths are cross-built but NOT_RUN here; root owns device execution.
Production ELF symbol checks exclude argument/loader/boundary test hooks.

Android CLI: 771864B, SHA256
ff26a2887a723301b076f0e390fcaad3aad82589daf9f82916cefa287b7cda96.
Normal contract: 853288B, SHA256
430dcb1feccb757997b29a0b06abcfad1de0e689979afeffc53c83fbc052dd86.
Argument recovery: 867920B, SHA256
b62ab981f388603d25c465ec10acf09646b5fb1c850dd79b8aeb6df32b60f95e.
Full dependencies, generated fixture hashes, commands and official partial NDK
member validation are in manifest/freeze. Original startup with empty and
nonempty vectors is ready for RG; its next strict gate is not yet measured.
Original main hash is unchanged. CRT/ELF TLS/ThreadType remain unresolved.

## OS TLS binding checkpoint

The profile now owns the independent `guest_os_tls` manager (`9c24ed35`) and
registers a record only on its actual capacity-verified primary. The exact local
SDK exports are Allocate143b60 (`uint32_t*, callback` -> u32 Result), Free143bf0
(u32 -> void), Get143c10 (u32 -> u64), Set143c30 (u32,u64 -> void). Main imports
Allocate59, Set124 and Get131; these indices were checked against dynamic symbols,
not assumed from messages. Public APP exhaustion alone maps to proven 0x1203;
other unsupported/internal failures failstop without a guessed SDK Result.
SDK allocation wrappers, slot16, GetCurrentThread, SDK ThreadType and ELF TLS are
not provided. No TP register, BSS stack setting or source input is changed.

After normal user callback return, with nb_call idle but current_process,
ThreadRecord RUNNING and its fault scope/owned stack still alive, cleanup invokes
each opaque destructor through the same typed native boundary. The manager holds
no mutex across the call, so Get/Set/Allocate/Free imports may reenter normally.
The first round calls non-null callbacks even with value0, then clears the current
slot after return; the component implements the proven five-round limit.
Only successful cleanup may unregister before ThreadReturned, fault-scope end
and Context stack refund. Provider failure logs the retained event and aborts
BEFORE returning to Context; this isolated runner does not pretend to offer
recoverable stack retention or complete cleanup. Values are never host-freed.
After a real guest primary has ended, a second primary run is rejected outright
so allocated process callback identities cannot trigger on another dead session.

Normal host contracts exercise actual records, all16 APP slots, output-preserving
0x1203 exhaustion, stale value after Free and zero after reallocation. A separate
Mac unsupported-provider run registers a valid non-null module callback with
value0, then must failstop rather than silently skip it. A self-authored Android
fixture verifies deferred guest callbacks, zero first value, reentrant Set,
clear-after-callback, earlier-slot second round and Query observing a still-live
ThreadRecord/64KiB stack. Its callback order is A(0),B(9),A(5); no game bytes are
used. Actual Android execution belongs to the coordinator.

The original hard target is nninitStartup passing Allocate59 and reaching
GetHostArgc60/return+13420. The known straight-line original store of1MiB lies
between those calls. Reaching that gate is static-plus-control-flow evidence,
not direct BSS readback or complete OS/ELF TLS/CRT initialization.

The current profile dispatches actual guest code only through native_boundary
(`5c20b064f2866577ca1fdd903fa196dd842eb3a8`, including retained RX rollback).
The loader records validated import RELA write identities during its existing
parse. Quiescent retargeting preflights every requested write: known imported
function, 64-bit aligned zero-addend relocation, original writable data page,
unchanged expected value and no overlapping import write. Unsupported readonly
sites are rejected without mprotect or writes. No arbitrary pointer scan occurs.
The supplied main's 36 previously admitted import sites meet these restrictions.

Process.load remains on the coordinating host thread. The first actual guest
call lazily creates the scope on its registered, capacity-verified primary,
binds only explicit register-only signatures and atomically publishes their
recorded import sites after all bindings succeed. The native gateway restores
host x18 before entering host code and restores guest x18 on return. Same-primary
calls reuse this session and saved guest x18. Initialization uses an injected
executor for both DT_INIT and array entries, preserving the explicit wrapper
single-walk policy; failed initialization is not retried. The 21-argument CRT
import remains unresolved, without a guessed signature or success result.

When this primary ends, its module cannot execute on another primary. Bindings
remain alive because guest state may have cached thunk addresses. Destruction
first runs the existing discard-only unload observer and really releases the
module, then unbinds every mapping and closes the scope, including dead idle
owner cleanup. Failed bind NB_RETAINED9, module release, unbind or close retains
Process/handles for retry; a non-callable binding with entry NULL is never
published. Administrative thunk pages remain explicitly outside the native
logical backing quota. Non-idle dead boundary owners remain retained.

The boundary excludes callback reentry, foreign unwinding/longjmp, stack arguments
and unknown aggregate ABIs. Therefore profile exit and __cxa_finalize are now
strict unresolved imports: they previously invoked guest callbacks directly.
atexit registration remains functional and unload discards its records. Legacy
non-profile behavior is unchanged. New profile CLI execution accepts reviewed
named scalar entry signatures; it never guesses the ABI of a numeric offset.
Mac ordinary C callback/owner/Query/page contracts still run; actual guest code
returns explicit unsupported because this boundary supports Android AArch64.
There is no raw fallback to keep those calls running.

The contract includes a generated, non-game AArch64 fixture that clobbers x18,
calls an admitted typed host import and checks the restored guest value, multiple
calls and the ended-session prohibition. Loader-only valid fixtures exercise
retarget values, readonly refusal and init executor call counts/failure stop.
An Android-only failure product redirects just the component VM calls to test
RX failure plus two unmap failures: no guest execution/publication, retained
Process after first destroy, then one actual unmap before successful retry.
These hooks are absent from both production ELF products.

Fatal signal diagnostics remain an isolated experiment facility; native_boundary
does not provide asynchronous signal adaptation or make those stdio handlers
generally async-signal-safe. Query's old builtin-return-address diagnostic may
point into the import gateway; it is not mislabelled as an original guest PC.
The strict unresolved sentinel still records its original guest LR. CRT, ELF TLS,
SDK ThreadType/alias mapping, CreateThread and full game startup remain incomplete.

## Earlier checkpoints (historical evidence)

The unchanged supplied main executes original nnMain at +0x352f160 on an owned
64KiB native pthread stack. QueryMemoryInfo returns a complete native policy
snapshot at original caller +0x27a50, then stops strictly at AllocateMemoryPages,
symbol 71, caller +0x27f90: exit 133, no timeout. Its actual arguments request
64KiB at owned reservation+0x10000. Independent malloc(4096) crosses Query at
+0x1398c and +0x27a50, then stops at GetVirtualAddressMemoryResourceUsage,
symbol 66, caller +0x144b8, also 133. Neither is a CRT/TLS/game startup pass.

## Ownership and snapshot

The explicit diagnostic fixture quota is 1073741824 bytes, stack 65536; neither
is an original SVC RAM measurement. On this 16KiB host the validated mapped main
extent is 162873344, used=162938880, heap capacity/blocks=0/0,
baseline=162873344, registered stack sum=65536 and ThreadRecord count=1. The heap
is explicitly initialized READY_EMPTY with no allocator capability; all heap,
RW pages, new threads, GPU/alias/shared backing imports remain unresolved. The
strict profile resolver admits only reviewed memory/string functions, typed
Mutex/guard/exit/link anchor, owned PROT_NONE reservations and typed Query; it
never calls dlsym or the extension resolver. The old default resolver already
had a small dlsym allowlist; this is a separate, narrower opt-in profile.

Only six u64 fields and one u32 are written. Padding +0x34..37 is preserved.
Invalid/uninitialized/retained state writes nothing; the guest void-ABI wrapper
aborts with a precise diagnostic. A private Context, exact module ticket,
actual native primary allocation, live pthread identity/ThreadRecord and each
VA reservation record support the snapshot. Byte totals alone are insufficient.
The 62GiB PROT_NONE reservation is tracked but contributes zero backing charge.
Query requires every provider valid and no pending/foreign/quarantined backing.

Lock order is process -> Context or process -> VA registry. Lifecycle calls
and external raw loader/Context/VA APIs must be quiescent. One profile per process;
there is no general cross-module, concurrent-guest or foreign host-client support.
The ThreadRecord survives callback return through native join; only then is it
destroyed. Fault scope/altstack and prior signal dispositions are established
and restored on that actual child thread. Failed restoration is nonzero and
retains ownership. Fatal signal diagnostics still use stdio and are an isolated
experiment facility, not async-signal-safe recoverable game exception support.

The same loader parse precharges the actual validated image_size immediately
before image mmap; no second parse/reopen estimate is used. Typed release clears
each mapping only after successful munmap and refunds image backing only after
real release. Failed load/release preserves descriptor/owner/ticket for retry;
the existing exit-cleanup observer remains separate and runs once before unmap.
Legacy NULL/void APIs fail explicitly on retained cleanup. Source RO mmap,
sentinels, symbol arrays, guard VA, native pthread/TLS/HLE metadata are explicit
host administrative costs outside this logical application cap. Source mappings
also remain tracked on cleanup failure. MAP_PRIVATE is not an immutable snapshot:
inputs must remain unchanged/untruncated. Reservation munmap failure retains the
existing API's fatal behavior; only typed module release offers recovery here.

## Reproduce and evidence

```sh
sh scripts/runtime/build_guest_process_contract.sh \
  --out .local/owned-startup-next \
  --toolchain /Users/yuyan9923/GitHub/ff-resonance-rg477m-native/.local/minimal-ndk/toolchain \
  --main .local/uncompressed/main.bin --quota-bytes 1073741824 --stack-bytes 65536
```

`.local/owned-startup-v2/manifest.json` records 33 bounded steps, none timed out:
normal/recovery contracts 0; seven fields, padding, real SP, joined ThreadRecord,
VA ownership, extension exclusion and signal restoration pass; five deterministic
loader VM/limit phases verify precharge, failure rollback, retained mapping and
retry with observer once. Invalid Query is SIGABRT (-6; wrapper exit134), quota
refusal exit1; legacy/profile info0. The earlier v1 script asserted an incorrect
common post-Query boundary for malloc and stopped with exit1; raw logs remain.

Official partial NDK r27d members are CRC/size verified; whole archive verification
is not claimed. AppleClang/LLD cross-linked AArch64 ELF with real libc++abi guards
and discoverable EH metadata. Products in that directory:

| Product | Bytes | SHA256 |
|---|---:|---|
| guest_process_contract.android | 649672 | 646adf09e31758e9e813e70aa9a486a9e3e9d4ee880065223ed6e5fa0455ab95 |
| nso_owned_startup.android | 648848 | 6ac2cf2599d436dbdf34b96d69782c7d78d7cbff13f942da735b6f4e15f33c85 |

Android execution and independent QA belong to the coordinator, not this report.
Frozen Context/address_region files are byte-identical to e846dc1. Main SHA256
before/after remains 6e6d1f3cd1672243e2ed344cc1b8849b85437d41bdecec81a5f9181e472d68a5.
Its complete aligned 99000832-byte text scan finds no direct SVC or MSR to
TPIDR_EL0/TPIDRRO_EL0; this does not prove its remaining TLS reads compatible.
SDK initialization, actual guest TLS/TCB/destructors and the new page/VA-budget
boundaries remain unresolved. No SDK TP-writing path or device operation ran.

## RG contract failure and diagnostic checkpoint

The coordinator ran the frozen 649672-byte contract (SHA646adf09...0455ab95)
on RG477M: exit1/no timeout, after valid Query values and ordinary VA operations.
The callback's aggregate `value==&returned_token && !errors` assertion failed;
the original binary did not distinguish its internal checks. This is a real
Android contract failure, so the original entry was not run on RG with this
profile. Logs are in the integration worktree
`.local/device/rg477m-session1/owned-profile-{execution.json,contract.stderr}`.

`owned-startup-diagnostic-v1` adds per-field/per-condition failure labels and
actual pthread stack range/SP/local, altstack and returned-token values. The
65536-byte stack equality and all original pass requirements remain enforced;
no production runtime behavior changed. Host normal/recovery contracts still0.
The new Android contract is 663040 bytes, SHA256
d02c86845607be6f1da3a546a04b515c986d31c1402601f73e4af7ec715e36ef.
Fixture remains a8467dc70098b693ab2d0266ac9ec99c705b76a644da20fbc0e5b263cd237493;
the CLI ELF remains byte-identical to the frozen artifact. Device diagnosis is
pending coordinator execution; no Bionic usable-stack assumption is relaxed.

## Measured capacity repair

The coordinator's diagnostic binary isolated exactly one failure: native usable
64752 bytes (0xfcf0), 784 below 65536. SP/local, Query widths/padding, altstack,
reservations and returned token all passed. Both original failures remain saved.
Public AOSP [pthread creation source](https://android.googlesource.com/platform/bionic/+/808d176e7e0dd727c7f929622ec017f6e065c582/libc/bionic/pthread_create.cpp)
also demonstrates that pthread bookkeeping can reduce caller-supplied stack
capacity; its private size is not used by this fix or assumed to match RG.

Context now measures native usable capacity on the child before invoking any
guest callback. A short probe executes no guest code, is joined, genuinely
unmapped and refunded, then retries with the measured shortfall rounded to a
host page. Checked limits are three attempts and four extra host pages. Query
failure, exceeding either limit or an invalid/SP-excluding layout returns a
typed nonzero status; rollback/join uncertainty retains ownership as before.
Raw RW allocation/outer guards, native usable range, exact 65536-byte logical
guest window at its usable top, and extra administrative bytes are distinct
descriptor fields. SP/local must lie in that exact logical window; raw guards
are inspected at raw boundaries. This strengthens the capacity requirement;
the original insufficient 64752-byte layout cannot invoke the guest callback.
Only logical 65536 bytes is the stack quota charge; extra RW administration,
native pthread/TLS overhead and unused padding are explicitly outside it.

Guards bind directly to the three real typed compiler symbols, independent of
another translation unit's macros. A host profile without typed guard support
is rejected at compile time; the dedicated script tests that expected exit1.

New evidence: `.local/guest-context-capacity-v2/manifest.json` has eight steps0,
including real host normal/recovery contracts, one successful capacity rebuild,
three capacity refusal cases (query error, retry bound, admin bound), callback0
on refusal and empty post-join quota. `.local/owned-startup-capacity-v1/manifest.json`
has 34 expected steps (0/1/134/133), no timeout, and unchanged real main SHA.
Host nnMain/malloc retain their truthful post-Query boundaries. Android products:

| Product | Bytes | SHA256 |
|---|---:|---|
| guest_context_contract.android (capacity-v2) | 78432 | 6d7d46bae9a0b97f8a25221b6d1213f02ec748f8d17c92e6144ee198b761dda5 |
| guest_process_contract.android (owned-startup-capacity-v1) | 675792 | aa82fc592bb4db849375aa54efdc0cfb17b703cde29f3d5f4456aad77abb2d86 |
| nso_owned_startup.android (owned-startup-capacity-v1) | 655104 | 1e5953a8144855d65f93310c5d68fce215abe097d1ff94f8e6fed9cd8d6a6d69 |

All are cross-built from official CRC-verified partial NDK inputs. New RG
execution/independent review remain pending coordinator confirmation.
