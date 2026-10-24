# The command memory callback, and the memory the host lends

Date: 2026-10-25
Owner: P-MM-24 worker (GRAPHICS_CHAIN_IMPLEMENTATION)
Branch: `task/P-MM-24-command-memory`, base `work/rg477m-native` at `d598a9a`
Card: `coordination/prompts/P-MM-24-command-memory-callback.md`

This card makes a command package exist. It does not make a frame: there is
no replay, no submit and no present here, and none is claimed.

## What the device run showed

The device run of 2026-10-25 (APK sha256
`1c8974cadce1842c19a72d3c8ab15fb7ee3f97b45ddd0b4afb755f549a942cee`,
transcript `.local/logs/p-mm-29-guest-transcript.log`, 58539 lines) says:

```
[hostevent] [nvn-callback] the guest's command memory callback did not return; status=4; no raw fallback and no direct call to the guest's pointer
[hostevent] [nvn-callback] ... status=4 ...            (three times)
[hostevent] the recording of the command buffer 0x51b9a63638 is void: the guest did not hand over more command memory; the records of this recording are lost and its end will not hand out a handle
```

`status=4` is `NB_BUSY` (`src/runtime/nso/native_boundary.h:10`). The guest
reached the point where its command memory was full: the recorder asked for
more, the callback did not come back, and the recording was thrown away. So
there was no package, and the replay had nothing to read.

## Why it failed, exactly

**One: the boundary refuses the call by design.** The boundary has two ways
into the guest. `nb_call` wants the idle phase (`phase == 0`), and
`nb_call_from_host` wants the host phase (`phase == 2`). The host phase only
exists while a host function runs behind an **import gateway**
(`src/runtime/nso/native_boundary.S:429`). The NVN entry points are not
import gateways: the guest resolves them through the pointer that
`nvnDeviceGetProcAddress` handed out and calls them directly. While such a
call runs, the scope is in its guest phase, so the boundary refuses both of
its entries. The thread check passed - the status is `NB_BUSY`, not
`NB_WRONG_THREAD` - so the caller was on the guest's own thread; only the
phase was wrong.

**Two: the recorder had no second mechanism.** `record_open` asked the guest
for more memory and, when that did not help, voided the recording. The
reference that runs this same game does not void anything: it lends a segment
of the host's own memory and keeps recording
(`.local/refs/nxnative/dm/gfx/nvn/hle_nvn.c:782`, `cmd_alloc_keep`). The
reference also says why this is not an edge case for this game: *"Our records
are larger than real-device NVN commands, so the budget the game reserved for
real-device usage is not necessarily enough, so this is not an error state of
the game but something we must catch."* This runtime writes the reference's
own fat payload structures, so the budget the guest reserved runs out early
exactly as it does there.

## What changed

### 1. The callback really reaches the guest

`nvn_guest_call_through_boundary` (`src/runtime/hle/guest_process.c`) now
makes the call itself, on the guest's own thread, after three checks that are
made and not assumed:

| check | how | refusal |
|---|---|---|
| the caller is the guest's own thread | `p->thread.state==THREAD_RUNNING && pthread_equal(p->thread.native,pthread_self())` | `-1`, "the caller is not the guest's own thread" |
| the guest's thread pointer mirror is bound on this thread | `guest_tp_current_base() != NULL` | `-2`, "the guest's thread pointer mirror is not bound on this thread" |
| the address is inside the guest module's text and is aligned | `nso_entry_address(p->module, address-nso_base(p->module), &host)` | `-3`, "the address ... is not inside the text of the guest's module" |

The address the guest hands over is already the host entry of the guest
function, because `nso_base(m)` is `(uint64_t)m->image`: the guest's address
space is the host's. A pointer the guest made up is never called. The
guest's thread local storage is found by the guest's own patched code through
the pthread key, so a call made on the guest's own thread finds it by itself.

The call carries the four words the reference's callback takes: the command
buffer, the event (`NVN_COMMAND_BUFFER_MEMORY_EVENT_OUT_OF_COMMAND_MEMORY`,
which is 0), the number of bytes needed and the callback data the guest set.
The recorder's lock is not held while the callback runs: the callback calls
back into `nvnCommandBufferAddCommandMemory`, and the re-entry is expected.

The recorder counts each refusal under its own name
(`callback_refused_thread`, `callback_refused_pointer`,
`callback_refused_address`).

### 2. A segment of the host's memory

When the guest's callback has been tried twice and the guest still has not
added enough, the recording continues in a segment of the host's own memory,
the way the reference does. The rules:

* **A recording is never void because of memory.** The reason of a void
  recording is a command that does not fit, never a shortage of memory. The
  only way a recording is void now is that the bound of the lent memory was
  reached, and the refusal says so by name:
  `"[nvn] the host has no segment of command memory left: all 64 segments of
  256 KiB are lent out and the bound of 16384 KiB is reached, so a command
  that does not fit was refused"`.
* **The total is bounded**: 64 segments of 256 KiB, 16 MiB in all, the size
  the reference lends at a time. The bound is in the statistics
  (`host_segment_bound`), so nothing depends on a constant written down.
* **The bytes are the host's**: the guest's memory is never written to, and
  the guest's own accounting (`GetCommandMemorySize`, `...Free`,
  `...Used`) counts only the bytes of the command memory the guest handed
  over.
* **The segment is given back when the recording is over**: the package holds
  a copy of the records, so the segment goes back to the pool of the host and
  the pool does not grow with the number of frames.
* **The guest gives the memory back**: the guest's command memory is one area
  that it hands out in pieces, every grant is a window at the start of that
  area, and a recording that ran out of it hangs off the range of the area it
  occupied. When the guest hands out a window that covers that range, the
  segments that took it over are released and counted
  (`host_segments_released`).

### 3. The recording ends in a handle

A recording that reaches its end hands out the handle of an immutable
package, also when part of it was recorded into a segment of the host's
memory. The package holds the records of the guest's memory and of the host's
segment **in the order they were recorded**, in one piece of memory, so a
reader that walks `package.records` for `package.bytes` bytes sees every
record (the replay does exactly that, and it needs no change). The package
also carries the map of its segments (`nvn_command_package.segments`), and
every record knows which memory it is in (`nvn_recorded_command.segment`).

### 4. The contract and the self proof

`tests/native/nvn_command_memory_contract.c` is a translation unit of its
own, with its own `main` and its own `--nvn-command-memory` switch. It drives
the real recorder through the real NVN entry points, resolved the way the
guest resolves them (`guest_process_resolve(0,"nvnBootstrapLoader")` and the
lookup it returns). The expectations never write a number of records down:
the records are counted before and after.

`scripts/runtime/selfproof_nvn_command_memory.sh` and
`scripts/runtime/make_nvn_command_memory_variants.py` are the three variant
self proof of the nine that already exist:

| variant | what is broken | result |
|---|---|---|
| A | the expectation (the callback got an event it never got), the final gate kept | **exit 1** |
| B | the same break, the final gate removed, the reported error count hardcoded to 0 | **exit 0** while the stdout still reports `errors:0` and the stderr carries the real `[check-fail]` |
| C | the contract untouched, the recorder stops calling the guest | **exit 1** |

## What the card asked and what this card could not do as written

Three points of the card do not hold against the code in this repository. The
closest correct thing was done and the difference is written down here.

1. **The callback cannot go through the boundary's entries.** The card says
   the boundary refuses both of its entries during an NVN call, and asks for
   the callback to reach the guest anyway. The two are only compatible if the
   call is made on the guest's own thread after the three checks, which is
   what the card's own three checks are for. The call is therefore made in the
   hook, and the boundary's phase-gated entries are not used. The name of the
   hook is the one the registration of the boundary uses; the registration
   itself is outside this card's write scope.
2. **"An address that belongs to no registered pool is refused by name" is
   the replay's check, not the recorder's.** The recorder never sees the
   guest's pool: the generated entry point throws the pool and the offset
   away (`nvnCommandBufferAddCommandMemory` calls
   `nvn_command_buffer_add_command_memory(commandBuffer, (uint64_t)arg3)`),
   and the generator and the generated file are outside this card's write
   scope. The named refusal of such an address lives in the replay layer
   (`src/graphics/nvn_device/nvn_command_replay.c`, the parallel card). The
   case of this card drives the memory pool family through the real entry
   points and checks that a pool that cannot be created is refused by name
   and counted, and that an address of no registered pool is not turned into
   a host pointer.
3. **The overlap of the reclaim rule is decided on the guest's command
   memory area, not on the guest's addresses**, for the same reason: the
   recorder does not see them. Each grant is a window at the start of the
   area, the recording occupies the area from its start, and a new grant that
   covers that range releases the segments. The rule is conservative (it
   releases earlier than the reference would) and the memory of a segment is
   only given back when the recording that used it is over, so a package
   never changes.

## What is not proven

* **No device run.** The device is the coordinator's: the lock
  `.local/agent-pm/BOOT_LOCK_MM_A` was not taken, no `adb` was run and no
  APK was built or installed. The three checks of the real hook, and the
  first real callback of this game, are proven on the device and not here.
* **The host has no guest thread boundary.** `nb_scope_init` is Android and
  AArch64 only, so on the host the boundary cannot be prepared and the real
  hook is never registered. The contract registers a stand-in that applies
  the same address check and calls the guest's callback with the same four
  words. What the host proves is the recorder's side: the four words, the
  fallback, the package, the bound and the named refusals.
* **The callback data of the guest is carried as a word.** The reference
  passes a `void*`; the recorder keeps it as the word it is and hands it
  back unchanged, which is what the contract checks.
* **The bound of 16 MiB is the host's own bound.** The reference aborts at
  1 GiB. A game that records more than 16 MiB of commands in one recording
  would be refused, by name, with the reason that a command does not fit.
* **The packages are still never given back.** The package of a recording
  stays in the registry until the registry's bound is reached, and the
  recorder refuses a new package then. That is a gap of an earlier card and
  it is not closed here.
* **No frame.** Nothing in this card draws, submits or presents.
