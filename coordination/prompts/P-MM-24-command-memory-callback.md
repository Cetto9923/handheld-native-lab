# P-MM-24 — the command memory callback and the host lent segments

Role: GRAPHICS_CHAIN_IMPLEMENTATION
Base: `work/rg477m-native` at `d598a9a`
Branch to create: `task/P-MM-24-command-memory`

## Why this card exists

The guest's recording of its commands never completes, so **there is no
command package, and the replay has nothing to read**. This is the wall
between the recording and the first frame.

The device run of 2026-10-25 (APK sha256
`1c8974cadce1842c19a72d3c8ab15fb7ee3f97b45ddd0b4afb755f549a942cee`,
transcript `.local/logs/p-mm-29-guest-transcript.log`, 58539 lines) says it
plainly:

```
[hostevent] nvnCommandBufferInitialize: buffer=0x51b9a63638; ...
[hostevent] nvnMemoryPoolInitialize: pool=0x5219a6dd00 ...
[hostevent] [nvn-callback] the guest's command memory callback did not return; status=4; no raw fallback and no direct call to the guest's pointer
[hostevent] [nvn-callback] ... status=4 ...            (three times)
[hostevent] the recording of the command buffer 0x51b9a63638 is void: the guest did not hand over more command memory; the records of this recording are lost and its end will not hand out a handle
```

`status=4` is `NB_BUSY`.

## Why it fails, exactly

The boundary has two ways into the guest:

- `nb_call` takes the scope out of its idle phase (`phase == 0`).
- `nb_call_from_host` needs the scope in its host phase (`phase == 2`).

The host phase only exists while a host function runs behind an **import
gateway**. The NVN entry points are not import gateways: the guest reaches
them through the pointer that `nvnDeviceGetProcAddress` handed out and calls
them directly. While such a call runs, the scope is in its guest phase
(`phase == 1`), and the boundary refuses both of its entries by design. The
thread check passed — the status is `NB_BUSY`, not `NB_WRONG_THREAD` — so
the caller is on the guest's own thread; only the phase is wrong.

Facts that make the fix possible, all checked in this repository:

- `nso_base(m)` is `(uint64_t)m->image` (`src/runtime/nso/nso_loader.c:403`).
  The guest's address space and the host's are the same, so the address the
  guest hands over as its callback is already the host entry of the guest
  function. There is no address space to translate.
- `nso_entry_address(m, off, &out)` (`src/runtime/nso/nso_loader.c:452`) checks
  that the offset is inside the module's text and 4 byte aligned, and returns
  the host entry. It is the validation the pointer needs before it is called.
- The guest's thread pointer is not in x18. It is bound with a pthread key
  (`guest_tp_bind_current`) and the guest's code is patched to find it
  (`guest_tp_patch_text`), so a call made on the guest's own thread finds the
  guest's thread local storage by itself.
- The reference that runs this game calls the callback directly and, when the
  callback does not add enough, lends a segment of host memory instead of
  aborting: `.local/refs/nxnative/dm/gfx/nvn/hle_nvn.c`, `cmd_alloc_keep`:
  ```c
  for (int tries = 0; !o->cur || o->cur + size + keep > o->end; tries++) {
      if (!o->cb || tries == 2) {
          LOG("[nvn] command memory: the game's callback did not add enough ..., using a host-lent segment");
          cmd_new_segment(o, hostseg_take(o), HOSTSEG_SZ); break;
      }
      o->cb((NVNcommandBuffer *)o, NVN_COMMAND_BUFFER_MEMORY_EVENT_OUT_OF_COMMAND_MEMORY, size + keep, o->cb_data);
  }
  ```
  And the reference says why the host segment is not an edge case for this
  game: *"Our records are larger than real-device NVN commands, so the budget
  the game reserved for real-device usage is not necessarily enough, so this
  is not an error state of the game but something we must catch."*

Our recorder has the same problem and no second mechanism: `work_limit()` is
the guest's own command memory budget minus one header, and our records are
the reference's fat payload structures, so the budget runs out early.

## What to build

### 1. The callback really reaches the guest

The hook that `guest_process.c` registers with the NVN layer
(`nvn_command_buffer_set_guest_call`) must call the guest's callback on the
guest's own thread. Three conditions are **checked**, not assumed:

- the caller is the guest's own thread (the guest process's thread handle
  equals `pthread_self()`),
- the guest's thread pointer mirror is bound on this thread
  (`guest_tp_current_base() != NULL`),
- the address is inside the guest module's text and is aligned
  (`nso_entry_address`).

If any of them does not hold, the callback is **refused by name and
counted**, and the recording says what happened. A pointer the guest made up
is never called.

The call carries the four words the reference's callback takes: the command
buffer, the event (`NVN_COMMAND_BUFFER_MEMORY_EVENT_OUT_OF_COMMAND_MEMORY`,
which is 0), the number of bytes needed and the callback data the guest set.

Note that the guest's callback calls back into `nvnCommandBufferAddCommandMemory`,
which is a host function the guest reaches through its own pointer, so the
re-entry into this layer is expected. The recorder's lock must not be held
while the callback runs.

### 2. A segment of host memory when the guest cannot add enough

When the callback has been tried twice and the guest still has not added
enough, the recording continues in a segment of the host's own memory, like
the reference does. The rules:

- the recording is **never void because of memory**. A recording that cannot
  be completed says so in its own reason string, and the reason is about a
  command that does not fit, not about memory.
- the host segment is reclaimed when the guest gives that memory back, that
  is when the range the guest hands out overlaps the range the segment took
  over, and when the package that used it is gone.
- the total size of the host segments is bounded, and a bound that is hit is
  a refusal that is **named**, not a silent truncation.
- the bytes of the host segment are the host's, and the guest's memory is
  never written to.

### 3. The recording must end in a handle

A recording that reaches its end hands out the handle of an immutable
package, even when part of it was recorded into a host segment. The package
then holds the records of the guest's memory and of the host segment in the
order they were recorded, and a record knows which memory it is in.

## Write scope (yours, and nothing else)

- `src/graphics/nvn_device/nvn_command_buffer.c`
- `src/graphics/nvn_device/nvn_command_buffer.h`
- `src/runtime/hle/guest_process.c` — **only** the hook function and its
  comment. Nothing else in that file.
- `tests/native/guest_process_contract.c` — the new case and its switch.
- `scripts/runtime/selfproof_nvn_command_memory.sh` (new)
- `scripts/runtime/make_nvn_command_memory_variants.py` (new)

Not yours: `src/graphics/nvn_device/nvn_device.c` (the dispatch), the CMake
files, `scripts/runtime/build_guest_process_contract.sh`, and everything in
`coordination/`. The coordinator wires the dispatch and the build.

## Acceptance

1. The contract has a case that: drives the real recorder through the real
   NVN entry points; makes the command memory run out; checks that the
   guest's callback was called with the four words of the reference's
   callback; checks that the recording ends in a handle and that the package
   holds the records in order; checks that an address that belongs to no
   registered pool is refused by name; and checks that a callback address
   outside the module's text is refused by name instead of called.
2. The expectations are independent of the implementation's constants. The
   number of records is counted before and after, never written down.
3. A three variant self proof, in the shape of the nine that already exist:
   - **A** the expectation is broken and the final gate stays -> red,
   - **B** the same break, the final gate removed and the reported error count
     hardcoded to 0 -> the false green that exits 0 while its stdout still
     reports zero errors,
   - **C** the contract is untouched and the recorder stops calling the guest
     -> red.
   Read `scripts/runtime/selfproof_nvn_command_buffer.sh` and its generator
   before writing, and follow their shape exactly.
4. The host gate is green: `sh scripts/runtime/build_guest_process_contract.sh
   --out .local/build-p-mm-<n> --toolchain .local/minimal-ndk/toolchain
   --resource-quota-bytes 16777216` exits 0, and the new case reports zero
   errors.
5. The self proof runs A red, B the false green, C red, and the log shows the
   exit code and the stdout of every variant.
6. A note in `coordination/reports/` that says what the device run showed, what
   the root cause was, what was changed, and what is still **not** proven. The
   device run itself is the coordinator's, not yours, because the device lock
   is the coordinator's.

## What is not yours and not this card

- The NVN sampler object family (`nvnSamplerInitialize` and its two
  siblings). The coordinator is doing that one at the same time, in
  `src/graphics/nvn_device/nvn_sampler.c` and the dispatch.
- The Vulkan submit and present path. That is the next card, and it starts
  from a package that this card makes exist.
- Any claim about a frame. There is no frame and this card does not make one.
  It makes a package exist.
