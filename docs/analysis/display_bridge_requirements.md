# nn::vi display bridge requirements derived from the guest code

**Status**: implementation requirements for the Android display bridge that
feeds the NVN window and swapchain.
**Derived from**: static decoding of the guest main NSO (the `nn::vi` calls are
at module offsets `0x51eac4` to `0x51eaf0`) and from the RG477M boot v62
(`.local/logs/rg477m-boot-v62-nvn-bootstrap-loader.log`).

The bridge is small: **five imports**, all of them `void` returning, called in
one straight-line block. `nn::vi::GetNativeWindow` is the seam: it hands the
guest the native window pointer that a Vulkan surface needs.

---

## 1. The whole display family

| Import index | Symbol | Signature |
|---|---|---|
| 228 | `_ZN2nn2vi10InitializeEv` | `nn::vi::Initialize()` |
| 229 | `_ZN2nn2vi18OpenDefaultDisplayEPPNS0_7DisplayE` | `nn::vi::OpenDefaultDisplay(nn::vi::Display**)` |
| 230 | `_ZN2nn2vi20GetDisplayVsyncEventEPNS_2os15SystemEventTypeEPNS0_7DisplayE` | `nn::vi::GetDisplayVsyncEvent(nn::os::SystemEventType*, nn::vi::Display*)` |
| 231 | `_ZN2nn2vi11CreateLayerEPPNS0_5LayerEPNS0_7DisplayE` | `nn::vi::CreateLayer(nn::vi::Layer**, nn::vi::Display*)` |
| 232 | `_ZN2nn2vi15GetNativeWindowEPPvPNS0_5LayerE` | `nn::vi::GetNativeWindow(void**, nn::vi::Layer*)` |

Two more `nn::vi` imports exist but belong to the frame pacing path rather
than to the bridge itself: 354 `nn::vi::GetLatestFrameNumber` and 355
`nn::vi::ListFrameHistory`.

Each of the five has exactly **one** call site, and the five call sites are
adjacent in one straight-line block with no branch between them, so all five
are reached together.

## 2. The guest's call sequence

Decoded at `0x51eaa0`, in the constructor of the guest's display object (the
object is `0x60` bytes and is allocated with `operator new(0x60)`):

```
0x51eaa0  mov  w0, #0x60
0x51eaa4  bl   operator new
0x51eab4  mov  x21, x0                 ; the display object
0x51eabc  add  x22, x0, #0x30          ; &object->vsync_event
0x51eac4  bl   nn::vi::Initialize()
0x51eac8  add  x0, x21, #0x18          ; &object->display
0x51eacc  bl   nn::vi::OpenDefaultDisplay(&object->display)
0x51ead0  ldr  x1, [x21, #0x18]        ; the display handle
0x51ead4  mov  x0, x22                 ; &object->vsync_event
0x51ead8  bl   nn::vi::GetDisplayVsyncEvent(&object->vsync_event, display)
0x51eadc  ldr  x1, [x21, #0x18]        ; the display handle
0x51eae0  add  x0, x21, #0x28          ; &object->layer
0x51eae4  bl   nn::vi::CreateLayer(&object->layer, display)
0x51eae8  ldr  x1, [x21, #0x28]        ; the layer handle
0x51eaec  add  x0, x21, #0x20          ; &object->native_window
0x51eaf0  bl   nn::vi::GetNativeWindow(&object->native_window, layer)
```

The guest's display object therefore holds:

| Offset | Field |
|---|---|
| `+0x18` | `nn::vi::Display*` |
| `+0x20` | `void*`, the native window |
| `+0x28` | `nn::vi::Layer*` |
| `+0x30` | `nn::os::SystemEventType`, the vsync event |
| `+0x58` | a byte flag, cleared in the constructor |

**All five functions return void.** The Itanium mangling carries no return
type, so this comes from the call sites: after every one of the five calls the
next instruction overwrites `x0` or loads `x1`, and none of them reads a
result.

## 3. What the bridge has to provide

1. `nn::vi::Initialize()`: nothing to do on Android beyond bookkeeping.
2. `nn::vi::OpenDefaultDisplay(Display**)`: write a non-NULL display handle.
   The guest only ever passes that handle back to the other four functions, so
   the handle can be an opaque host token.
3. `nn::vi::GetDisplayVsyncEvent(SystemEventType*, Display*)`: the guest uses
   this to wait for a frame. `nn::os::SystemEventType` is an event handle
   structure whose layout this runtime does not know; the honest options are a
   real event built with the already bound `nn::os` event family (imports
   `InitializeEvent`, `WaitEvent`, `SignalEvent`, `ClearEvent`, `DestroyEvent`)
   or an event that is never signalled, which would stall the guest's frame
   loop. The choice has to be recorded, because the two behave very
   differently.
4. `nn::vi::CreateLayer(Layer**, Display*)`: write a non-NULL layer handle.
5. `nn::vi::GetNativeWindow(void**, Layer*)`: write the platform window
   pointer. **On Android this is the `ANativeWindow*` of the activity**, and
   it is what a Vulkan surface is created from
   (`vkCreateAndroidSurfaceKHR`). This is the single point where the display
   bridge and the T04 NVN bridge meet.

Nothing else in the guest reads the display object's fields, so the rest of
the object stays the guest's own.

## 4. Ordering: the display bridge is downstream of the NVN wall

The RG477M v62 boot contains **no** unresolved-import line, and the `nn::vi`
block did not run: the guest reached the NVN device initialisation
(`0x14354b0`) and aborted there, before the display object was built. The
display block has no direct caller in the text either, so it is entered
through an indirect path.

Consequence: binding the five `nn::vi` imports will **not** move the current
boot forward. The NVN device has to exist first, because the game aborts on
`nvnDeviceInitialize failed` before it ever builds a display. The two bridges
have to be delivered together for a frame to appear.

## 5. The input bridges, for completeness

The guest's input surface is the `nn::hid` family:

- 236 `nn::hid::GetKeyboardState`, 237 `nn::hid::GetMouseState`: the two
  per-frame readers;
- 385 `nn::hid::InitializeNpad` and 449 to 463: the Npad family, which is
  what a controller would drive;
- 234 and 235, the keyboard and mouse initialisers, are already bound.

The Npad state structures are large fixed layouts (`NpadFullKeyState` and
friends) whose field order this runtime has not verified; the layout has to
come from the guest's own reads of those structures, not from an assumed
header, for the same reason the NVN builder layout does not have to be
reproduced.
