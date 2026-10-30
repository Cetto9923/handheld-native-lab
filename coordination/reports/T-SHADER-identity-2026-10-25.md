# Shader identity of FINAL FANTASY RESONANCE on the RG477M native port

Date: 2026-10-25
Owner: coordinator
Branch: `work/rg477m-native`
Base commit: `064634f` (worktree dirty: the command buffer family and the identity tooling of this round are not in it yet)

## What was asked

Two hosts that run this game name a shader differently, and the port has to
decide which name it uses and what it does about the other one:

- the **content key** `K_content = sha256(control[:nctl] || code[:ncode])[:16]`,
  the control section and the code area hashed together. This is the name the
  game itself uses and the name the runtime computes at run time.
- the **control key** `K_control = sha256(control[:0x880])[:16]`, the control
  section alone. This is the name the other Vulkan host uses when it looks up
  a shader bundle on disk.

The worry with the control key is that it drops the code area from the
identity: two shaders with the same control section and a different code area
would share a name, and the second one would never be found. The instruction
was not to trade the game's own identity away for that host's convention.

## The name of the game was wrong before, and it matters

The reference tree's game profile `0100606026F12000` is this game. Its own
`game.tsv` says so:

```
name	FINAL FANTASY RESONANCE
build	v65536	1.0.1	main=84e182f72ee1da4f477804bd05b54c15
```

The profile's shader map holds the shader `174293a30c315aed` with a key that
is byte for byte the one this project computes. That is what makes the profile
usable as the reference for this game: its shader set, its shader facts and
its per-game notes describe the same shaders this port sees.

The input difference that was flagged earlier is still open and is not
explained by this:

| where | main build |
|---|---|
| this port, the guest log of the last device run | `736837afd390442ed7c74a7de730e71b` |
| the reference profile `0100606026F12000` | `84e182f72ee1da4f477804bd05b54c15` |

The title id in the original input note of this project, `0100B8A026F14000`,
is a third value. So the shader set agrees with the profile, and the build id
does not. The build id has to be settled before any patch offset, SDK version
or frame rate change is taken from the reference.

## The same shader under both names

One real shader, taken out of the game's own container
(`FFRS-Switch.utoc`, the pixel shader archive), with both names computed from
the same bytes:

```
content_key          174293a30c315aed
control_key          dbc58f224deb3525
stage                ps            (the tag in the container is SP)
control_length_used  2176          (0x880; the section states its own length as 0x880)
code_length_used     12800         (the code area is 12800 bytes long)
```

The content key is the one the reference profile's shader map carries for this
shader, byte for byte.

The runtime computes the same thing. `src/graphics/nvn_device/nvn_sha256.c`
is the runtime's own digest, and it was run over the same two files:

```
$ .local/shader-identity/nvn_sha256_key \
      174293a30c315aed.control.bin 174293a30c315aed.code.bin
{"content_key":"174293a30c315aed","control_key":"dbc58f224deb3525",
 "control_length_used":2176,"control_length_stated":2176,
 "code_length_used":12800,"code_length_actual":12800}
```

The runtime's digest and the offline tooling agree on a real shader, so the
name the runtime looks up at run time is the name the translator writes down.

## The collision question, measured over the whole shader set

Not assumed. Both libraries of the container were read and every shader
record in them was hashed twice:

```
library 39301 (global):  3542 shaders   ps 1466, vs 106, cs 1968, gs 2
library 51495 (game):   70702 shaders   ps 60515, vs 9218, cs 967, gs 2

shaders                 74244
distinct content keys   73496
duplicate content keys     592
distinct control keys   73496
control key collisions      0
```

What this says:

- **No two different shaders of this game share a control key.** Across all
  74244 records the control key is as unique as the content key, so the
  control-only name would not have collided here. The reference profile's own
  conflict file says the same thing (`control-section key conflict rulings:
  empty`), but that is the reference's claim; this is our own measurement of
  the game's bytes.
- **592 records share a content key with another record.** That is the
  opposite direction and it is not a problem: the same content key means the
  same control section and the same code area, so those are the same shader
  reached twice, not two shaders under one name.

The port still uses the content key. It is the game's own identity, it is
unique by construction rather than by measurement, and it stays unique if a
later update of the game ships a shader that shares a control section with an
older one.

**What this buys.** If the port ever has to hand a bundle to the other host,
which looks its bundles up by the control key, the two names can be mapped:
for every shader of this game the mapping is one to one, and it was checked
over the whole set rather than over a sample. A bundle written for that host
would be named by the control key and would still be found. The port's own
cache keeps the content key.

## The shader set is large, so it is translated on demand

74244 records is far too many to translate ahead of time, and only a few are
needed for the first frame. The port therefore looks a shader up by its
content key in a host side cache, and a miss is written down with the key,
the stage and the lengths of the control section and the code area, so that
the one missing shader can be taken out of the container and translated on the
build host, put into the cache and the run repeated. No one has to be sent
back to the user for one missing key, and the whole library is not translated
blind.

The tool that does this is `scripts/graphics/nvn_shader_identity.py`:

```
python3 scripts/graphics/nvn_shader_identity.py show 174293a30c315aed --library 51495
python3 scripts/graphics/nvn_shader_identity.py scan --out identity-full.json
```

It reads the game's own IoStore container, so it needs no game data that is
not already in this project, and it writes only under `.local/`.

## What the SPIR-V is worth, and what it is not

`spirv-val` (SPIRV-Tools v2026.4) was installed. The result depends on the
target environment, and the default one is not the one Vulkan uses.

Default target environment (no `--target-env`), six real shaders:

```
174293a30c315aed  exit 0      2ae58c692fb7fbb8  exit 0
3fefb8e379a30f64  exit 0      41b20b5f09269ad8  exit 0
86affd63144799de  exit 0      9cb65be4f7d0d498  exit 0
```

Vulkan target environments:

```
$ spirv-val --target-env vulkan1.1 <file>       # and vulkan1.3, same result
174293a30c315aed  exit 0      2ae58c692fb7fbb8  exit 0
3fefb8e379a30f64  exit 0      41b20b5f09269ad8  exit 1
86affd63144799de  exit 1      9cb65be4f7d0d498  exit 0
```

The two failures say the same thing:

```
error: line 5381: Capability StorageImageWriteWithoutFormat is required to write to storage image
  OpImageWrite %5167 %5168 %5173 None
error: line 1622: Capability StorageImageWriteWithoutFormat is required to write to storage image
  OpImageWrite %1560 %1566 %1572 None
```

So the honest statement is: **six of six pass against the default SPIR-V
environment, four of six pass against Vulkan 1.1 and against Vulkan 1.3.**
The two that fail are compute shaders that write to a storage image without a
declared format. A Vulkan shader module for those needs the capability
declared in the module and the matching feature enabled on the device; the
device's `shaderStorageImageWriteWithoutFormat` support and its enabled state
have **not** been queried on the RG477M yet, and nothing here says that Mali
accepts these modules.

The earlier report of this port said "six of six valid". That was the default
environment and it was too wide. The earlier check, which only looked at the
magic number and at where the instructions end, is weaker still.

The commands, the exit codes and the full output are in
`.local/shader-verify/val/` (game derived, not in git).

What is still missing before any of this is "a shader the device takes":
the storage image format of the compute shaders has to come out of the game's
own control section, the translator has to declare the capability in the
module, the reflection has to carry it, the device has to have the feature
enabled, and a real `VkShaderModule` and a real `VkPipeline` have to be
created on the RG477M and a draw has to consume them. That chain is **NOT_RUN**.

## A claim about the other host that did not hold

This port's earlier notes said that the other Vulkan host provides the whole
NVN ABI through one entry point, and that the families therefore did not have
to be written by hand. That is **withdrawn**. In the pinned checkout of that
host, `xbg_nvn_get_proc_address` appears only as a declaration and a comment
in `dm/gfx/vulkan/engine/xbg_game.h`; no definition of it is present. Its
Android adapter `dm/gfx/vulkan/nvn_vk.c` also pulls in that host's own `dm`
host framework and its Metal shim, so it is not a self contained engine that
can be dropped in.

What that host does have is a real Vulkan replay engine with a defined
integration surface (`xbg_game_config`: the physical device, the device, the
queue, the queue family, the enabled features, the shader directory, the
present callback, the guest callback). That surface is the thing to adapt to,
not a ready made NVN ABI.

## The command stream format is the host's own, and this game's is known

The command buffer does not write a byte into the guest's object. The
commands themselves go into the command memory the guest hands over, and the
handle of a recording is where that recording starts.

The format of those records is a property of the host, not of the game, so
there is no single format that both hosts use. But the format for **this
game** is known, because the reference tree that runs this same game carries
it, with the payload layouts checked against the game:

```
.local/refs/nxnative/dm/gfx/nvn/nvn_obj.h
    typedef struct { uint32_t op, size; } op_hdr;   /* the record header */
    enum { OP_END, OP_JUMP, OP_BEGIN, OP_CALL, ... } /* about fifty-five opcodes */
    p_set_rt, p_clear_color, p_clear_ds, p_viewports, p_scissor,
    p_bind_buf, p_bind_vbuf, p_bind_handles, p_bind_tex1, ...
```

That is a real NVN command stream, not a parallel format of this port's own
making, and it is the format the production recorder has to write. The
numeric recorder that the port has today (an opcode index plus eight raw
words) is a **diagnostic layer**: it tells which entry points the guest calls
and with which arguments, and it must not be presented as a command stream
that a replay could consume.


## Sources

| what | where |
|---|---|
| this game's profile in the reference tree | `.local/refs/nxnative/dm/games/0100606026F12000/` |
| the reference tree | `https://github.com/seedds/nxnative` at `7a029f66d92c951ae6a86e38bb34eeb0cb7527e3`, MIT |
| the other host, for the control key rule | `.local/refs/d2r-mobile/dm/gfx/vulkan/engine/xbg_shader_key.c` |
| that host | `https://github.com/d2r-mobile/d2r-mobile` at `a8782caa28d7b49d1b341e34de488d3168acb86c`, MIT |
| the game's own container | `.local/romfs/FFRS/Content/Paks/FFRS-Switch.utoc` |
| the identity tool of this port | `scripts/graphics/nvn_shader_identity.py` |
| the runtime's digest, run over real bytes | `scripts/graphics/nvn_sha256_key.c` |
| the full measurement | `.local/shader-identity/identity-full.json` (game derived, not in git) |
