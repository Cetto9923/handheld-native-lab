# The replay seam: how a recorded command reaches the screen

Date: 2026-10-25
Owner: coordinator
Branch: `work/rg477m-native`
Base commit: `61ce223` (worktree dirty: the command buffer layer is being rewritten by the P-MM-22 worker)

This note answers the question the review raised: where does the replay come
from, and how does a recorded command become a pixel. It is written so that
the next implementation has no decisions left to make.

## There is no replay engine to drop in, and that is now a fact, not a guess

The two hosts that were looked at for a reusable Vulkan replay were checked
properly.

**The other host (`d2r-mobile`, MIT, revision `a8782caa`).** It has a real
Vulkan replay engine: `dm/gfx/vulkan/engine/xbg_game.cpp` is about four
thousand lines and it does the whole chain, from the NVN entry points through
the command records to the draw and the present. But:

- `xbg_nvn_get_proc_address`, the entry point that would have supplied the
  whole NVN ABI, exists only as a **declaration** in `xbg_game.h`. There is no
  definition of it in that checkout.
- Its Android adapter `dm/gfx/vulkan/nvn_vk.c` includes `../../host/dm.h`,
  `../nvn/nvn_mtl.h` and `../../host/front.h`, so it needs that host's own
  application framework and its Metal shim.

What that host has and what is worth taking is its **integration surface**:
`xbg_game_config` names the physical device, the device, the queue, the queue
family, the enabled core features, the shader directory, the present
callback, the guest callback and the fatal handler. That is the shape of the
interface this project has to offer whatever does the replay, and it is a
good shape: it says the replay does not own the device and does not own the
window.

**The same-game host (`nxnative`, MIT, revision `7a029f66`).** Its replay is
`dm/gfx/nvn/nvn_mtl.m`, eight hundred kilobytes of Objective-C that executes
exactly this game's command stream on Metal. Its Vulkan path,
`dm/gfx/vk/hle_vk.m`, is a Metal-compatible layer over Vulkan for their own
iOS build; it is not a Vulkan replay of NVN.

**Conclusion.** There is no replay engine that can be dropped into this
project. The replay has to be written here. What the same-game host gives is
worth more than an engine would have been: it gives the **semantics of every
command of this game**, in a language and a graphics API this project can
read, with the payload layouts checked against the game.

## The recording format is now the same-game format, so the replay has a reference

The P-MM-22 worker changed the recorder to write the reference's own record
format:

```
op_hdr { uint32_t op, size; }          the header, size covers the header and is 8 byte aligned
enum   OP_END, OP_JUMP, OP_BEGIN, OP_CALL, ...     forty-nine opcodes, the reference's numbering
p_set_rt, p_clear_color, p_clear_ds, p_viewports, p_scissor, p_bind_buf,
p_bind_vbuf, p_bind_handles, p_bind_tex1, ...      the reference's payload structures
```

Two things follow, and they are the point of the change:

1. **A record written here can be read with the reference's own structures.**
   That is checkable: parse a record with `nvn_obj.h`'s `p_*` and compare
   field by field. The command stream stops being a private format of this
   port.
2. **The reference's replay is a specification for each opcode.**
   `nvn_mtl.m` contains, per opcode, what the command means and what the
   device is asked to do. The Vulkan replay of this project is that
   specification translated from Metal calls to Vulkan calls.

So the replay is not written from scratch against a guess. It is written
against the same game's own replay, opcode by opcode.

## The chain, and what each seam needs

```
guest call
  -> the recorder writes a record into host side command memory   (P-MM-22, done)
  -> EndRecording freezes that memory into an immutable package      (P-MM-22, done)
  -> nvnQueueSubmitCommands hands the handle to the replay
  -> the replay decodes the package with the reference's structures
  -> the replay resolves the guest's addresses into device resources
  -> the replay issues Vulkan draws
  -> the replay presents the guest's render target
```

### Seam A: the handle and the package

`nvn_command_buffer_end_recording` returns the address of the first record in
the host side command memory of that recording, and
`nvn_command_buffer_get_package(handle, out)` hands the immutable package
back. A recording that is made later into the same buffer, a finalization and
a reuse of the guest's buffer address all leave an earlier package untouched.

**The replay must only ever take a handle through the registry.** A handle
that the registry does not know is refused and counted, never dereferenced.
This is the same rule the display bridge already follows after the contract
caught it reading a magic value out of a pointer the guest had invented.

### Seam B: the guest's addresses are not pointers

The guest addresses its resources with a buffer address that this runtime
hands out (`nvnMemoryPoolGetBufferAddress`). The address is **not** a host
pointer and must never be dereferenced as one.

The resolution already exists: `nvn_memory_pool_resolve_address(address,
available)` finds the registered pool the address belongs to and the host
readable range behind it. The replay must use it for every address the
records carry:

| what the record carries | what it must become |
|---|---|
| a buffer address in a bind or a copy | the host storage of that pool, at that offset, with that length, checked against the pool's own range |
| a texture handle | the texture of the texture pool the command buffer had bound |
| a sampler handle | the sampler of the sampler pool the command buffer had bound |
| a program | the program of the program family, with its shader key |
| an event or a sync | the object of the family that owns it |

Two checks are not optional and are not "the driver will catch it":

- **the offset plus the length must stay inside the pool's range**, and an
  address that belongs to no registered pool must be refused by name;
- **a texture or a sampler that belongs to a different pool** must be
  refused, because the texture and the sampler numbering of the reference are
  per pool (`tex_base` and `samp_base` in the reference's command buffer
  object).

The reference's own replay keeps a table of the pools a command buffer bound
last. The same table is needed here, and the command buffer state of this
project has to carry it.

### Seam C: one device, one queue, one window

The device that exists today is the one in `nvn_backend_vulkan.c`: a real
Vulkan instance, a real logical device with one graphics queue, a real
surface and a real swapchain, created on the RG477M. It is the **only**
device in the process.

The replay must use that device and that queue and must not create a second
one. Two devices in one process cannot share a `VkImage`, a `VkFence` or a
swapchain, and the guest's render target belongs to the device that made it.

So the replay is given the device, the queue and the queue family, and it
issues its work there. The swapchain stays where it is.

### Seam D: the present comes from the guest's render target

The present must show what the guest drew. The chain is:

1. the guest binds a texture as a render target (`SetRenderTargets`);
2. the replay draws into it, on the device and the queue above;
3. the guest asks for the window's texture to be presented
   (`nvnQueuePresentTexture`), and the replay copies that texture into the
   swapchain image;
4. the swapchain image is presented.

A host probe that clears the swapchain and presents it is **not** a game
frame, and it must never be counted as one. The evidence for a game frame is
a screenshot whose pixels are explained by the guest's command stream.

### Seam E: the guest callback boundary

The command memory callback is the guest's way of saying "I have run out of
command memory, give me more". When the replay runs out, it must call the
guest, and it must do that through this project's own boundary
(`src/runtime/nso/native_boundary.{h,c}` and `src/runtime/hle/guest_tp.{h,c}`),
on the guest's thread. The callback pointer that the guest handed over is
never called directly from the replay thread, and the replay must never
leave a dangling pointer to it.

### Seam F: the order of events

`SignalEvent`, a counter, a readback and a fence must all happen in the
order the GPU actually finished. The simplest correct baseline is: one
submit, one fence, wait for the fence on the thread that has to have the
answer, then raise the event. The reference's asynchronous optimisations are
not copied.

The current NVN trap still ends the process. That has to change before the
handheld can stay up across a guest failure, and it is a separate piece of
work from the replay: every guest thread, every lock and the device state
have to be coordinated, and none of that may be done with a longjmp across a
thread that holds the device.

## What is needed before the first frame, in order

1. The command stream is a real one (P-MM-22, in flight).
2. The builder families let the guest build what a draw needs (P-MM-21,
   landed; the device run with them is still to come).
3. The guest reaches a draw and hands over a program, and with it the first
   real shader key. The offline translation of that key has to exist, and the
   runtime has to find it.
4. A shader module and a pipeline have to exist on the device (P-MM-23, in
   flight). The four levels, separately: the module, the pipeline, the draw,
   the pixels.
5. The replay decodes the package and issues the draw, using this project's
   device, with the address resolution of seam B.
6. The present shows the guest's render target (seam D).
7. Only then is there a first frame, and only then does it mean anything.

Steps 3 to 6 are the work. Steps 1 and 2 are the input to it.
