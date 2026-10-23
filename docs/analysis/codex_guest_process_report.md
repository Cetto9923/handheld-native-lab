# Owned startup profile: original nnMain crosses Query

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
