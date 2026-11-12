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
