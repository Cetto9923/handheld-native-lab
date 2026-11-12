# OSS-NOTES: the retirement of a deferred event (transit task, 2026-11-12)

The change under test is the retirement rule of a deferred NVN/Metal event: when the guest reads the value of an event,
the submission that carries the newest signal to that address retires, and the submissions queued after it stay in flight.
Every source below was opened as text during this task; a search summary was not accepted as evidence.

## 1. Khronos Vulkan Specification, Synchronization chapter

- URL: https://github.com/KhronosGroup/Vulkan-Docs/blob/f7de774cace1509af0f10b8d859c973b5354ff62/chapters/synchronization.adoc
- Revision: commit `f7de774cace1509af0f10b8d859c973b5354ff62` (retrieved 2026-11-12)
- License: Apache-2.0
- Retrieved: **yes** - the raw adoc was fetched (393588 bytes) and the passages below were read in the file
- Support scope: the normative semantics of fences and semaphores. This is the layer the engine sits on: each submission
  carries a fence, and the engine waits on the fence of the submission it needs.

The passages that matter here:

> Fences are a synchronization primitive that can: be used to insert a dependency from a queue to the host. Fences have
> two states - signaled and unsignaled. A fence can: be signaled as part of the execution of a queue submission command.

> Timeline semaphores have a strictly increasing 64-bit unsigned integer payload and are signaled with respect to a
> particular reference value.

> A semaphore can: be signaled after execution of a queue operation is completed, and a queue operation can: wait for a
> semaphore to become signaled before it begins execution.

What this supports: the fence is signalled by the *execution of a submission*, and a value-carrying synchronisation
primitive exists precisely so that a host can wait for *its own* signal and not for the work that was queued afterwards.
The change gives the engine the same shape: the event's value is tied to one submission, and the wait covers that
submission.

## 2. Google Dawn, EventManager

- URL: https://github.com/google/dawn/blob/85b7901759827252adf542adf7a1d2858515b12e/src/dawn/native/EventManager.cpp
- Revision: commit `85b7901759827252adf542adf7a1d2858515b12e` (retrieved 2026-11-12)
- License: BSD-3-Clause (the file header carries the three-clause text)
- Retrieved: **yes** - the raw file was fetched (20905 bytes) at that commit
- Support scope: a shipping graphics stack that records an event at a point in the queue and completes it when the queue
  reaches that point.

The passage that matters here (EventManager::TrackEvent):

> `q->TrackSerialTask(QueuePriority::UserVisible, queueAndSerial->completionSerial, [this, event]() { ... SetFutureReady(event.Get()); });`

What this supports: the completion of the event is bound to the *serial* at which it was recorded, and the event becomes
ready when the queue reaches that serial - not when the queue is empty. This is the model the engine now follows for the
guest's event word: the value is published when the submission that carries the signal retires.

## 3. Apple Metal, MTLSharedEvent (the API the guest was written against)

- URL: https://developer.apple.com/documentation/metal/mtlsharedevent
  (the machine-readable form was fetched: https://developer.apple.com/tutorials/data/documentation/metal/mtlsharedevent.json)
- Revision: the documentation JSON has no revision; it was retrieved 2026-11-12
- License: Apple developer documentation, quoted here for semantics only; nothing is copied into the product
- Retrieved: **yes** - the JSON was fetched and its overview read
- Support scope: the event model of the API the guest uses: a value that the GPU writes and the CPU reads, and that
  synchronises the CPU with the GPU.

The passages that matter here:

> A shared event can synchronize memory operations across multiple Metal devices and the CPU.

> The MTLSharedEvent protocol inherits the MTLEvent protocol.

The page also points at "Synchronizing passes with a fence", which is the same idea on the GPU side.

What this supports: the guest's event word is a GPU-written value that the CPU is allowed to read, and it becomes
meaningful when the GPU has reached the signalling point. Reading it earlier is legal and yields the older value, which
is why the engine may publish the value of the newest signal to that address and leave the later work in flight.

## 4. Godot Engine, RenderingDevice

- URL: https://raw.githubusercontent.com/godotengine/godot/master/servers/rendering/rendering_device.h
- Revision: `master` at retrieval time (2026-11-12), **not pinned to a commit** - the file was read for the shape of
  the API, not for a version
- License: MIT
- Retrieved: **yes** - the header was fetched (86597 bytes)
- Support scope: a shipping engine that hands the host a fence for the point in the command stream it cares about.

The passage that matters here (around line 1835):

> `// Signaled by the command buffer submission. Must wait on this fence before beginning command recording for the frame.`
> `RDD::FenceID fence;`

What this supports: a host waits on the fence that its own submission signals. Waiting on more than that is the engine's
own extra, and it is what the change removes.

## 5. Sources that could not be obtained (recorded, not cited)

- **Ryujinx / yuzu NVN host implementation**: the GitHub API answers `404` for `Ryujinx/Ryujinx` and
  `Repository access blocked (dmca)` for the Unreal Engine mirror. The NVN host implementation of the Switch emulator is
  therefore not available as a source here, and nothing from it is used or quoted. The emulator-side precedent is taken
  from Dawn and from the specification instead.
- **Mesa, radv/anv driver sources**: the raw paths tried for `radv_wsi.c`, `radv_queue.c` and `anv_queue.c`
  returned `404` at retrieval time (2026-11-12, both `main` and the `mesa-24.0.0` tag). A driver-side "deferred fence"
  implementation was therefore **not** read, and the change does not claim one. The driver-level statement in this file
  comes from the specification, which is the contract the engine implements.

## How the sources map onto the change

| Part of the change | Source |
|---|---|
| The value of an event belongs to the submission that carries its signal | Vulkan fences (1), Dawn (2), Metal (3) |
| The host waits for its own signal and not for the work queued after it | Vulkan timeline semaphore values (1), Dawn (2), Godot (4) |
| The guest may read the value of an event before the GPU reached later work | Metal (3), Vulkan semaphore semantics (1) |
| The engine stops draining every submission at the read point | The rule above; the drain was the engine's own extra |

---

# OSS-NOTES, second round: the retirement of a readback (2026-11-12, 23:xx)

The change under test in the second round is the retirement rule of a readback: the guest's bytes are final when the
submission that carries the readback's copy has completed, and the host waits for that submission and for nothing else.
Every source below was opened as text during this round; a search summary was not accepted as evidence.

## 6. Khronos Vulkan Specification, Memory chapter (the readback's own contract)

- URL: https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/f7de774cace1509af0f10b8d859c973b5354ff62/chapters/memory.adoc
- Revision: commit `f7de774cace1509af0f10b8d859c973b5354ff62` (retrieved 2026-11-12, the same immutable revision the
  first round cited)
- License: the file header carries `SPDX-License-Identifier: CC-BY-4.0`
- Retrieved: **yes** - the raw adoc was fetched (298295 bytes) and the passages below were read in the file
- Support scope: the normative rules for a host read of a device write. This is the layer the engine's readback sits on:
  the copy is a queue operation, and the host reads its result from mapped memory afterwards.

The passages that matter here (around `vkInvalidateMappedMemoryRanges`):

> `vkInvalidateMappedMemoryRanges` guarantees that device writes to the memory ranges described by
> `pMemoryRanges`, which have been made available to the host memory domain using the
> `VK_ACCESS_HOST_WRITE_BIT` and `VK_ACCESS_HOST_READ_BIT` access types, are made visible to the host.

> This function does not synchronize with device operations directly - other host synchronization operations that
> depend on device operations such as `vkWaitForFences` must be executed beforehand.

What this supports: the host's read of the copy is bound to **one** synchronisation operation with the device - the
wait for the submission that carries the copy - and to the invalidation of that range. The specification does not ask
the host to wait for the other submissions in the queue, and the engine's readback path does exactly that: the copy's
own submission, and nothing else.

## 7. Google Dawn, EventManager (the completion of a readback is bound to a serial, not to an empty queue)

- URL: https://github.com/google/dawn/blob/85b7901759827252adf542adf7a1d2858515b12e/src/dawn/native/EventManager.cpp
- Revision: commit `85b7901759827252adf542adf7a1d2858515b12e` (retrieved 2026-11-12 in the first round; re-used here at the
  same immutable revision, no new fetch of a moving branch)
- License: BSD-3-Clause (the file header carries the three-clause text)
- Retrieved: **yes** (first round) - the raw file was fetched (20905 bytes) at that commit
- Support scope: a shipping graphics stack whose map/readback requests become ready when the queue reaches the serial
  at which they were recorded.

The passage that matters here (EventManager::TrackEvent):

> `q->TrackSerialTask(QueuePriority::UserVisible, queueAndSerial->completionSerial, [this, event]() { ... SetFutureReady(event.Get()); });`

What this supports: the completion of a host request is bound to the **serial** at which it was recorded, and the request
becomes ready when the queue reaches that serial - not when the queue is empty. The readback in this engine is the same
kind of request: the bytes are final when the submission that carries the copy has been executed.

## 8. Godot Engine, RenderingDevice::buffer_get_data (what a shipping engine does with a readback)

- URL: https://raw.githubusercontent.com/godotengine/godot/master/servers/rendering/rendering_device.cpp
- Revision: `master` at retrieval time (2026-11-12), **not pinned to a commit** - the file was read for the shape of the
  readback path, not for a version
- License: MIT (the repository's `LICENSE.txt` was fetched as well; it carries the MIT text)
- Retrieved: **yes** - the raw file was fetched (466501 bytes)
- Support scope: a shipping engine that hands a GPU result back to the CPU.

The passage that matters here (`buffer_get_data`):

> `RDD::BufferCopyRegion region;` … `draw_graph.add_buffer_get_data(buffer->driver_id, buffer->draw_tracker, tmp_buffer, region);`
>
> `// Flush everything so memory can be safely mapped.`
> `_flush_and_stall_for_all_frames();`
>
> `uint8_t *buffer_mem = driver->buffer_map(tmp_buffer);`

and the asynchronous variant (`buffer_get_data_async`), which records the copy into a staging buffer and hands the
result to a callback, with `STAGING_REQUIRED_ACTION_FLUSH_AND_STALL_ALL` as the case where the frames have to be
flushed first.

What this supports - and what it does not: the community's shipping engine **stalls the whole frame batch** for a
readback (`_flush_and_stall_for_all_frames()`). That is the over-wait this task removes, and it is recorded here as
the practice that exists, not as the rule the engine follows. The asynchronous variant is the shape the change adopts:
the result is delivered when the work that produced it is done, not when the queue is empty.

## 9. Sources that could not be obtained in this round (recorded, not cited)

- **wgpu, `wgpu-core/src/buffer.rs`**: the raw path returned `404` at `trunk` (2026-11-12). The wgpu readback
  implementation was therefore **not** read, and nothing from it is used or quoted.
- **Google Dawn, `src/dawn/native/BufferBase.cpp`**: the raw path returned `404` at the pinned commit
  `85b7901759827252adf542adf7a1d2858515b12e` (2026-11-12). The map-async implementation was therefore **not** read;
  the Dawn statement above is the EventManager, which was fetched.
- **Mesa, radv/anv driver sources** and **Ryujinx / yuzu**: as recorded in section 5 of the first round - `404` and
  `Repository access blocked (dmca)` respectively. Nothing from them is used or quoted.

## How the sources map onto the readback change

| Part of the change | Source |
|---|---|
| The bytes of a readback are final when the submission that carries the copy has completed | Vulkan memory chapter (6), Dawn (7) |
| The host waits for that submission and not for the rest of the queue | Vulkan memory chapter (6) (`vkWaitForFences` + invalidate), Dawn (7) (the serial) |
| The readback's completion is delivered when its own work is done | Dawn (7), Godot's asynchronous variant (8) |
| The engine stops draining every submission in flight at the readback point | The rule above; the drain was the engine's own extra, and Godot's synchronous variant (8) shows the same over-wait in a shipping engine |
