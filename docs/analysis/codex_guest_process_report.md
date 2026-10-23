# Owned startup profile: native boundary integration

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
