# GuestContext: explicit quota and owned primary native stack

This unit owns logical quota tickets and one synchronous native pthread stack.
It does not load/call the game, bind QueryMemoryInfo, initialize SDK ThreadType,
adapt TP/TLR/TLS, or alter the frozen address-region component/resolver.

The prior source/ownership design is .local/guest-context-plan/DESIGN.md with
source_evidence.json and a source-hashed reproduction capture (exit 0). It found
that nso_call currently runs on its caller's host stack. A diagnostic pthread
stack bound is not an exclusive guest stack or a source of original SVC values.
Module/heap/SDK thread statistics remain unmodeled; memory_info_valid_fields=0.

## Explicit inputs and ownership

guest_context_create requires positive caller-supplied quota_bytes and explicit
primary_stack_bytes. Stack size must be a current host-page multiple and at least
PTHREAD_STACK_MIN. Missing values are rejected; no host RAM/RSS is read, no
implicit quota/stack default is chosen, and the quota is not silently rounded.

The contract explicitly supplies 65,536 stack bytes, matching coordinator-verified
META+0x1c in the supplied main.npdm (1,620 bytes, SHA-256
a39b66ed07bcb7b2730a9bbd37ec9afe968dd28d6e3d5f4d7794c3528c1b3f95).
The coordinator's codex_npdm_memory_report.md records that source. Its 20-MiB
SystemResourceSize is not a total application budget. Contract quota values
(two stack payloads or two host pages) are explicit synthetic test policies,
not a selected game budget or measured Switch/Android physical-memory amount.

Tickets reserve pending quota under a Context mutex before resource allocation.
Checked subtraction rejects requests exceeding charged+pending+requested. A
successful real resource creation can commit its ticket; failed creation cancels
pending quota, and real release permits refund of committed quota. Ticket
metadata is allocated before resource creation. External ticket users own their
resource lifecycle and must release it before refunding the ticket; the ticket
API itself is an accounting primitive, not a certificate of physical residency.
Outstanding tickets block Context destruction.

run_primary reserves the complete stack payload, maps an inaccessible owned span
with one host-page guard on each side, grants RW to only the payload, commits
the charge, and sets pthread_attr_setstack to that actual payload. It disables
the pthread attribute's extra guard because the explicit mapping already owns
both guards. Attribute/create errors return their native error; there is no
fallback to a host default stack. An ordinary callback executes on the native
CPU, returns a real value, and is joined before munmap and quota refund.
Guard VA and host pthread/TLS/administrative allocations are not charged as
application payload and do not become original Switch physical-memory values.

Failed partial protection is cleaned up by unmapping the complete new owned
mapping before cancellation. If cleanup fails, the Context retains the mapping
and ticket and becomes quarantined; destroy can retry a quiescent real cleanup.
A failed join retains stack, quota and ownership because the thread might still
run. That Context cannot be reused/destroyed, and callback arguments must stay
alive; no forced cancellation/unmap is attempted. Native error and cleanup error
remain distinct. Cleanup failure and join failure handling are reviewed paths,
not claimed naturally reproduced platform failures in the current contract.

## Actual checks and artifact

Host normal/recovery contracts verify pending-to-committed state around real
small mmap allocations, cancellation, outstanding-ticket destroy refusal and
refund only after munmap. Eight competing host threads hold two successful
one-page allocations while six receive typed LIMIT; the final ledger is empty.

Three normal primary invocations verify the actual AArch64 SP and an automatic
volatile array inside the configured 64-KiB payload. Mach VM independently checks
RW payload and PROT_NONE guards; the Android test uses bounded self-map permission
lookups. No guard/freed bytes are read. Independent native C TLS markers and
errno work in the child and remain separate from the parent. Nested primary run
and destruction during the callback return BUSY. After each join the real mapping
is released and pending/charged/ticket counts are zero.

A separate host-only GUEST_CONTEXT_TESTING build injects one mmap failure. It
records the full pending stack charge before that failed operation, observes no
callback, checks complete rollback, then successfully reuses the same Context.
The production Android artifact contains no test hook symbol.

Reproduce from this worktree:

    sh scripts/runtime/build_guest_context_contract.sh \
      --toolchain /path/to/existing/minimal-ndk/toolchain \
      --out .local/guest-context-next

Final manifest: .local/guest-context-v2/manifest.json. All eight build/run/inspect
steps exit 0, no timeout; compiled source hashes and installed official member
CRC/SHA checks remain unchanged. Both host contracts report SP verified, 8 quota
threads (2 accepted/6 limited), zero pending/charged after join, zero valid
MemoryInfo fields and errors=0. Recovery reports one controlled failure case.
An initial compile missing limits.h was corrected; its true exit 1 remains in
.local/guest-context-v1/build rather than being counted as a successful run.

The offline cross-build uses AppleClang 21 plus verified installed r27d
headers/CRT/libc/compiler-rt and LLD, target aarch64-linux-android28, ELF64 PIE,
16-KiB segment alignment, RELRO/NOW and non-executable stack. Frozen small
contract: .local/guest-context-v2/guest_context_contract.android, 72,160 bytes,
SHA-256 e3d3500d9919d5aad1c67385802635fdaa29df4c340bc8529507ce2fb30d6ec1.
Android execution is pending the coordinator; this agent operated no device.
No module/page budget integration or seven-field Query bridge exists yet. Game
QueryMemoryInfo/CRT/TLS barriers and full T02 remain unresolved by this unit.

## Coordinator actual RG477M run

The frozen e846dc1 Android artifact above was hash-verified, staged to a fresh
hash-named file under the existing owned diagnostic directory and executed on
user-authorized RG477M Android14 ARM64/nativebridge0/4KiB. Actual exit0, no
timeout, elapsed0.0355s: 65536B stack, actual SP verified, both guards/TLS/join
checks passed; eight quota contenders accepted2/limited6; charged_after_join0,
pending_after_join0, memory_info_valid_fields0, errors0. Production binary has
no test hooks and map_failure_rollback_cases=0; no device fault injection claimed.
This device evidence is coordinator execution, separate from author host tests
and independent reviewer host execution. Raw index:
.local/device/rg477m-session1/guest-context-{plan,stage,execution}.json and
guest-context.stdout/stderr. Initial coordinator plan validation misspelled
returncode as exit_code and stopped before device actions; corrected against
the actual run_capture schema. No denied, pending or uncertain device mutation.

Independent frozen-source host review completed PASS: see codex_guest_context_review.md. Reviewer retained two failures caused by an inapplicable test-only TLS teardown-order assumption; author source and original contract were unchanged. Ordinary corrected supplement confirms charge/active survive pthread-key destructor work until native join, then refund. This is not SDK thread lifecycle acceptance.
