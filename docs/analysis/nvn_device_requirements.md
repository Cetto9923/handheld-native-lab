# NVN device requirements derived from the RG477M runtime evidence

**Status**: implementation requirements for the T04 NVN to Vulkan bridge.
**Derived from**: the P-MM-11 RG477M boot v62 (`.local/logs/rg477m-boot-v62-nvn-bootstrap-loader.log`,
57947 lines) and from static decoding of the guest main NSO.
**Scope**: what a home-grown NVN device has to provide for this game, and which
two assumptions in `docs/analysis/nvn_surface.md` the runtime evidence corrects.

Nothing in this document claims that a graphics device exists in this runtime.
There is none: the boot stops because the game aborts when the NVN device
initialisation fails.

---

## 1. The dispatch chain is two levels, not one

`docs/analysis/nvn_surface.md` says the 534 remaining APIs are obtained by
passing the name to `nvnBootstrapLoader`. The runtime evidence shows that is
not what the guest does, and the difference matters for the implementation.

Decoded guest block (main NSO module offsets, `0x14353a8` is the start of the
function; it is Unreal Engine's `NVNManager`):

```
0x1435414  adrp/add x0, #0x63fd01f          ; "nvnDeviceInitialize"
0x143541c  bl     nvnBootstrapLoader        ; -> x23
0x1435444  adrp/add x0, #0x636485c          ; "nvnDeviceGetProcAddress"
0x143544c  bl     nvnBootstrapLoader        ; -> x22
0x1435458  adrp/add x1, #0x6361cc6          ; "nvnDeviceBuilderSetDefaults"
0x1435460  mov    x0, xzr                  ; device = NULL
0x1435464  blr    x22                      ; GetProcAddress(NULL, name)
0x143546c  adrp/add x1, #0x6378018          ; "nvnDeviceBuilderSetFlags"
0x1435478  mov    x0, xzr
0x143547c  blr    x22
0x1435490  blr    x8                       ; SetDefaults(builder)
0x14354a0  blr    x8                       ; SetFlags(builder, flags)
0x14354b0  blr    x23                      ; nvnDeviceInitialize(device, builder)
```

So:

1. `nvnBootstrapLoader` is asked for exactly **two** names: `nvnDeviceInitialize`
   and `nvnDeviceGetProcAddress`. Five call sites, two distinct strings.
2. Every other entry point is resolved through the **`nvnDeviceGetProcAddress`
   that the bootstrap handed out**, not through the bootstrap itself. The first
   two lookups even pass a NULL device (`mov x0, xzr`); the bulk resolver at
   `0x1430e10` passes the real device.
3. A flat dispatch table inside `nvnBootstrapLoader` would therefore never be
   consulted for the other 534 names.

**Requirement**: the bridge needs a bootstrap that answers exactly those two
names, plus a device-scoped `GetProcAddress` that answers the 534 others.

### 1.1 The guest never checks the pointer it gets back

The resolver at `0x1430e10` stores each returned pointer into a global and
calls it later, with no NULL test anywhere in the loop:

```
0x1430e1c  adrp/add x1, #0x6361cc6
0x1430e28  mov    x20, x0                 ; the device
0x1430e2c  blr    x19                     ; GetProcAddress(device, name)
0x1430e3c  str    x0, [x8, #0x7b8]        ; stored, never tested
```

**Requirement**: `GetProcAddress` must not return NULL for a name the guest
asks for. Returning NULL sends the guest to `pc=0`, which is much harder to
diagnose than a clean trap. Unknown names are a different matter: a name
outside the `nvn` namespace can be refused.

### 1.2 The complete API inventory

`scripts/runtime/nvn_name_references.py` scans the main NSO text for the
ADRP/ADD pairs that materialise an NVN name and classifies each reference by
whether an indirect call follows it. The result is committed as
`evidence/nvn/guest_nvn_code_references.tsv`:

- 535 name strings exist in the image;
- **534 are referenced from code, and all 534 are handed to an indirect call**,
  so all 534 are resolved by name at run time;
- the only unreferenced name is `nvnBootstrapLoader` itself, which is the
  import rather than a lookup.

So the work list for the bridge is exactly: one bootstrap plus 534 entry
points, and the table names them with the code offset where each is looked up.

---

## 2. Correction: the NVN device builder layout is not a blocker

`docs/analysis/nvn_surface.md` and the dispatch plan assume the bundled NVN
header (the old 53.105 Darksiders ABI, which has no `nvnDeviceBuilder*`) forces
the real builder layout to be reconstructed, and treat that as an unknown.

The guest's own code shows the builder is **opaque to the guest**:

- The NVN manager frame is `sub sp, sp, #0x90` at `0x14353a8`, with the saved
  registers at `sp+0x40 .. sp+0x8f`. The local area is therefore
  `sp+0x00 .. sp+0x3f`, that is **64 bytes**.
- The builder is that local area: `mov x0, sp` at `0x143548c` and
  `0x1435498`, and `mov x1, sp` at `0x14354a8` passes it to
  `nvnDeviceInitialize`.
- The guest never reads or writes a field of the builder itself. It only
  passes the address to three NVN functions: `nvnDeviceBuilderSetDefaults`,
  `nvnDeviceBuilderSetFlags` and `nvnDeviceInitialize`.

**Requirement**: a home-grown device may define its own builder layout. It
only has to satisfy three constraints:

1. it must fit in the 64 bytes the guest reserved (anything larger would
   overwrite the guest's saved registers);
2. the builder setters and getters must agree with each other and with the
   initialisation, because the guest resolves both setters and getters by
   name and can compare them;
3. the initialisation must consume what the setters stored.

This does not reconstruct the Nintendo layout and does not claim to; it shows
the layout is not observable from the guest, so it does not have to be
reproduced.

The same argument applies to the device object: the guest passes
`x19+0x10` around as an opaque handle, so the device state can live on the
host side and the guest's object need not be written to at all.

---

## 3. Two defects in the current dispatch stub

`src/graphics/nvn/src/nvn_dispatch.c` has two placeholders that cannot be
integrated as they stand:

1. `stub_nvnDeviceInitialize` returns **1**, that is, it reports a successful
   device initialisation. That is the one thing this runtime must not do: the
   RG477M v62 boot shows the guest treats a false result as fatal
   (`LowLevelFatalError`, `NVNManager.cpp:1246`), so a true result would make
   the game proceed on a device that does not exist.
2. The fallback of `nvnBootstrapLoader` returns `stub_generic`, which returns
   NULL. For the two names the guest actually asks the bootstrap for, that
   would make the guest branch to `pc=0` at `0x14354b0`.

The v62 boot also shows the failure is **fatal, not a soft error**: UE raises
`LowLevelFatalError` at `NVNManager.cpp` line 1246 with the message
`nvnDeviceInitialize failed`, calls `nn::diag::detail::AbortImpl` (main NSO
import index 12) twice, and the guest backtrace frame at module offset
`0x14355c4` is exactly the failure branch the static decode predicted.

---

## 4. The runtime instrument that enumerates the surface

The null device backend in `src/runtime/hle/nso_host.c` logs every name the
guest resolves and every call it makes. That log is the instrument to use
once a real device exists: the resolution trace is the exact, ordered list of
the APIs the game touches, and the call count per API is the first workload
estimate. The static table in section 1.2 gives the complete set up front,
but only the runtime trace gives the order and the frequency.

The backend deliberately reports failure from `nvnDeviceInitialize`. That is
the honest answer for a runtime with no NVN device, and it is what makes the
guest's own fatal error visible instead of letting the game run on a device
that does not exist.

---

## 5. What is still needed for a title screen, beyond the NVN device

- **A window and a surface.** The NVN window and swapchain functions need an
  Android `ANativeWindow`, which comes from the `nn::vi` display family
  (main NSO import indexes 228 to 232). That is a separate platform bridge,
  not part of the NVN layer.
- **Shaders.** The cooked shader code is inside the IoStore containers that
  the guest already mounts (`FFRS-Switch.pak` 2958593334 bytes,
  `FFRS-Switch.ucas` 5976848928 bytes, `FFRS-Switch.utoc` 12382627 bytes,
  `global.ucas` 3353200 bytes), not in loose files, so the packaging is in
  place; the translation of the cooked shader code to Vulkan is still open.
- **Vulkan on the device.** `src/graphics/vulkan_probe/` already creates an
  instance, a device and a queue on the RG477M and exercises the buffer
  transfer path, so the Vulkan side is de-risked; the NVN layer on top of it
  is not built yet.
