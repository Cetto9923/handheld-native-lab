# Upstream references for the NVN to Vulkan graphics chain

Scope: the implementations this project may learn from for the step from a
real NVN device to a presented frame. Nothing here is copied into the tree
yet; this file records what each source is, what it is good for, and what
may not be taken from it.

## d2r-mobile (reference implementation of the same problem)

- URL: https://github.com/d2r-mobile/d2r-mobile (the acknowledgements in
  nxnative point at https://github.com/jhaizhou-ops/d2r-mobile; the clone
  used here reports the same tree)
- Revision: `a8782caa28d7b49d1b341e34de488d3168acb86c`
- License: MIT (Copyright (c) 2026 jhaizhou)
- Obtained: shallow clone into `.local/refs/d2r-mobile` (not part of the
  repository, not committed)

What it does: a Switch game whose graphics run through NVN is reimplemented
on the host. The host takes over the single NVN import, answers every name
the game asks for, records the calls, and replays the record stream through
a graphics backend.

What is directly relevant here:

- `dm/gfx/nvn/hle_nvn.c` and `dm/gfx/nvn/nvn_thunks.S`: the interface layer.
  Each NVN name gets a thunk that counts the call and jumps to the
  implementation; names that are not implemented get a stub that prints the
  name in the log. That is the same shape as this project's per-API trap,
  which stops the run instead of returning.
- `dm/gfx/nvn/nvn_obj.h`: host-side objects are stored in the memory the
  game allocates for NVN objects, with a static assertion that the host
  object never exceeds the space NVN reserved. This project does not know
  the guest's NVN object layouts, so it keeps the device state on the host
  side and uses the guest pointer as an opaque key; the layout discipline
  is worth keeping in mind when the queue, window and memory pool objects
  are implemented.
- `dm/gfx/vulkan/`: a real NVN to Vulkan replay backend (`nvn_vk.c`,
  `engine/`), plus `INTEGRATION.md`, which is the most useful document in
  either repository for this project. It records that the replay layer keeps
  the upstream `mtl_*` ABI as its seam, that Android window handling lives
  in `dm/android/vulkan_present.cpp`, that `OP_SIGNAL_EVENT` has to complete
  the earlier GPU work before the CPU event value is published and that the
  fence is a real `VkFence`, that presentation blits a really rendered image
  and reports success only after the blit and the queue completed, that
  Vulkan must not be called on the UI thread, and that the GPU time and the
  scanout time were left unmeasured rather than faked.
- `dm/gfx/hle_vi.c`: the `nn::vi` bridge. `nn::vi::GetNativeWindow` is
  answered with a host provided window object, which is exactly the seam
  this project needs for the Android `ANativeWindow`.
- `dm/android/front_android.c` and `dm/android/jni_android.c`: the shape of
  the in-app host. The game runs inside the application process on a host
  thread, and the Activity hands the window over through JNI.

What may not be taken from it: the Metal backend is Apple's, not Android's.
Its shader chain compiles to Metal shading libraries, which Vulkan cannot
use. Its memory pools are zero-copy `MTLBuffer`s with the GPU address equal
to the CPU address, which only unified memory devices support.

## nxnative (the generalisation of the same approach)

- URL: https://github.com/seedds/nxnative
- Revision: `7a029f66d92c951ae6a86e38bb34eeb0cb7527e3`
- License: MIT (Copyright (c) 2026 seedds)
- Obtained: shallow clone into `.local/refs/nxnative` (not committed)

What it is: the same native execution approach made generic for arbitrary
Switch games, with an iOS application shell. It documents that the game runs
directly on the phone with the graphics rebuilt on a Metal backend, that
nothing of any game is in the repository, and that it started as a copy of
the generic parts of d2r-mobile.

What is directly relevant here: the application shell model. The guest
runtime is loaded into the application process, so the window, the graphics
backend and the guest share one process and one address space. That is the
model this project has to follow on Android, because an `ANativeWindow` is
a process local handle.

Its shader note is the one to read before planning the shader work: the
Switch shaders are Tegra X1 (Maxwell) machine code, and the translation to
SPIR-V is done offline with Ryujinx's shader translator, and only then to
the target shading language. This project's guest is Unreal Engine's Switch
NVNRHI, so its shaders arrive as NVN shader binaries inside the IoStore
containers; the same shape of problem applies, and the same translator
family is the reference, but the guest specific parts have to be checked
against this game rather than assumed.

## Reuse decision for this project

- Reuse now: the window holder (`src/runtime/hle/native_window.c`), the
  record format (`src/graphics/nvn_las/command_record.h`, itself derived
  from peiikai/LASRecomp Beta4, MIT), the buffer component
  (`src/graphics/vulkan/buffer_resource.c`), and the Vulkan device and
  queue creation that already works on the RG477M.
- Take as a design, not as code: the record and replay split, the per-name
  thunk and red stub, the `mtl_*` seam idea, the event completion rule, and
  the presentation rule.
- Do not take: the Metal backend, the Metal shader libraries, the zero-copy
  unified memory assumption, and any game specific layout or shader fact
  from either repository.
