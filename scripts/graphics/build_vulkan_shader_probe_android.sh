#!/usr/bin/env bash
# Build the standalone Vulkan shader probe for ARM64 Android (API 28) and, when the
# host allows it, for the host itself.
#
# The default cache is a partial official NDK, not a full NDK, so every input is
# checked against the explicit validated-members manifest.
#
# The probe loads the Vulkan loader at run time with dlopen(); it does not link
# against libvulkan.  That is what lets the same binary start on a host without
# a Vulkan loader and report "no Vulkan in this build" instead of failing to
# link or inventing a device.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TOOLCHAIN="${FFRS_VK_TOOLCHAIN:-${ROOT}/.local/minimal-ndk/toolchain}"
MEMBERS="${FFRS_VK_MEMBERS:-${ROOT}/.local/minimal-ndk/validated-members.json}"
RUST_ROOT="${FFRS_VK_RUST_ROOT:-${HOME}/.rustup/toolchains/stable-aarch64-apple-darwin}"
LINKER="${RUST_ROOT}/lib/rustlib/aarch64-apple-darwin/bin/gcc-ld/ld.lld"
CC="${FFRS_VK_CC:-$(command -v clang)}"
SYSROOT="${TOOLCHAIN}/sysroot"
RESOURCE="${TOOLCHAIN}/lib/clang/18"
API_LIBS="${SYSROOT}/usr/lib/aarch64-linux-android/28"
BUILTINS="${RESOURCE}/lib/linux/libclang_rt.builtins-aarch64-android.a"
SOURCE="${ROOT}/src/graphics/vulkan_probe/vulkan_shader_probe.c"
BISECT_SOURCE="${ROOT}/src/graphics/vulkan_probe/vulkan_graphics_bisect.c"
SPV_VARIANTS="${FFRS_SPV_VARIANTS:-${ROOT}/.local/shader-verify/spv=original,${ROOT}/.local/shader-verify/spv-fixed=fixed}"
SPV_STAGE_FILTER="${FFRS_SPV_STAGE_FILTER:-}"
# The hand-written trivial vertex/fragment pair P-MM-23 used as its control.
TRIVIAL_DIR="${FFRS_SPV_TRIVIAL:-${ROOT}/.local/shader-verify/spv-trivialpair}"
RUN="${FFRS_VK_BUILD_DIR:-${ROOT}/.local/vulkan-shader/$(date -u +%Y%m%dT%H%M%SZ)}"
if [[ ! -f "${MEMBERS}" || ! -f "${SYSROOT}/usr/include/vulkan/vulkan_core.h" ||
      ! -f "${API_LIBS}/libdl.so" || ! -x "${LINKER}" || ! -x "${TOOLCHAIN}/bin/llvm-readelf" ||
      ! -f "${BUILTINS}" ]]; then
    echo 'BLOCKED_TOOLCHAIN: explicit validated-members manifest, Vulkan headers, libdl, LLD and readelf required.' >&2
    exit 2
fi
for entry in ${SPV_VARIANTS//,/ }; do
    directory="${entry%=*}"
    if [[ ! -d "${directory}" ]]; then
        echo "BLOCKED_INPUTS: SPIR-V directory not found: ${directory}" >&2
        exit 2
    fi
done
if [[ ! -f "${TRIVIAL_DIR}/trivial_vert.spv" || ! -f "${TRIVIAL_DIR}/trivial_frag.spv" ]]; then
    echo "BLOCKED_INPUTS: the trivial vertex/fragment pair not found: ${TRIVIAL_DIR}" >&2
    exit 2
fi
if [[ ! -f "${BISECT_SOURCE}" ]]; then
    echo "BLOCKED_INPUTS: the bisect probe source not found: ${BISECT_SOURCE}" >&2
    exit 2
fi
if [[ -e "${RUN}" ]]; then
    echo "Refusing to overwrite existing build evidence: ${RUN}" >&2
    exit 2
fi
mkdir -p "${RUN}/logs" "${RUN}/source_freeze/src/graphics/vulkan_probe" "${RUN}/modules"
cp "${SOURCE}" "${RUN}/source_freeze/src/graphics/vulkan_probe/"
cp "${BASH_SOURCE[0]}" "${RUN}/source_freeze/scripts_graphics_build_vulkan_shader_probe_android.sh"
git -C "${ROOT}" rev-parse HEAD > "${RUN}/git-head.txt"
git -C "${ROOT}" status --short > "${RUN}/git-status.txt"
git -C "${ROOT}" diff -- src/graphics/vulkan_probe scripts/graphics > "${RUN}/tracked-source.diff"
run_step() {
    local label="$1" code=0
    shift
    printf '%q ' "$@" > "${RUN}/logs/${label}.command"
    printf '\n' >> "${RUN}/logs/${label}.command"
    "$@" > "${RUN}/logs/${label}.log" 2>&1 || code=$?
    printf '%s\n' "${code}" > "${RUN}/logs/${label}.exitcode"
    cat "${RUN}/logs/${label}.log"
    return "${code}"
}

# ---------------------------------------------------------------------------
# Derive the descriptor set layout from the game's own reflection data and
# cross-check it against the SPIR-V decorations.  The manifest is the only
# thing the probe reads; an empty layout is never emitted.
# ---------------------------------------------------------------------------
python3 - "${SPV_VARIANTS}" "${SPV_STAGE_FILTER}" "${RUN}" <<'PY'
import json
import os
import shutil
import struct
import sys

variant_spec = sys.argv[1]
stage_filter = sys.argv[2]
run_dir = sys.argv[3]
modules_dir = os.path.join(run_dir, "modules")
variant_dirs = []
for entry in variant_spec.split(","):
    directory, _, variant = entry.rpartition("=")
    if not variant:
        raise SystemExit("variant entry needs directory=variant: %s" % entry)
    variant_dirs.append((directory, variant))
base_variant = variant_dirs[0][1]
os.makedirs(modules_dir, exist_ok=True)

OP_ENTRY_POINT = 15
OP_TYPE_IMAGE = 25
OP_TYPE_SAMPLED_IMAGE = 27
OP_TYPE_STRUCT = 30
OP_TYPE_POINTER = 32
OP_VARIABLE = 59
OP_DECORATE = 71
DEC_BINDING = 33
DEC_DESCRIPTOR_SET = 34
DEC_BLOCK = 2
DEC_BUFFER_BLOCK = 3
STAGE_NAMES = {0: "vert", 4: "frag", 5: "comp"}


def instructions(path):
    data = open(path, "rb").read()
    if len(data) % 4:
        raise SystemExit("%s: SPIR-V length is not a multiple of 4" % path)
    words = list(struct.unpack("<%dI" % (len(data) // 4), data))
    if words[0] != 0x07230203:
        raise SystemExit("%s: not a SPIR-V module" % path)
    index = 5
    while index < len(words):
        opcode = words[index] & 0xFFFF
        count = words[index] >> 16
        yield opcode, words[index + 1:index + count]
        index += count


def analyse(path):
    """Return (stage, {descriptor set, binding}: descriptor class) for a module."""
    stage = None
    variable_type = {}
    pointee = {}
    kind = {}
    image_sampled = {}
    image_dim = {}
    decorations = {}
    for opcode, operands in instructions(path):
        if opcode == OP_ENTRY_POINT:
            stage = STAGE_NAMES.get(operands[0])
        elif opcode == OP_DECORATE:
            decorations.setdefault(operands[0], []).append((operands[1], operands[2:]))
        elif opcode == OP_VARIABLE:
            variable_type[operands[1]] = operands[0]
        elif opcode == OP_TYPE_POINTER:
            pointee[operands[0]] = operands[2]
        elif opcode == OP_TYPE_SAMPLED_IMAGE:
            kind[operands[0]] = "sampled_image"
        elif opcode == OP_TYPE_IMAGE:
            kind[operands[0]] = "image"
            image_sampled[operands[0]] = operands[6]
            image_dim[operands[0]] = operands[2]
        elif opcode == OP_TYPE_STRUCT:
            kind[operands[0]] = "struct"
    bindings = {}
    for variable, items in decorations.items():
        binding = None
        descriptor_set = 0
        for decoration, arguments in items:
            if decoration == DEC_BINDING:
                binding = arguments[0]
            elif decoration == DEC_DESCRIPTOR_SET:
                descriptor_set = arguments[0]
        if binding is None:
            continue
        target = pointee.get(variable_type.get(variable))
        target_kind = kind.get(target)
        if target_kind == "sampled_image":
            descriptor_class = "combined_image_sampler"
        elif target_kind == "image":
            if image_sampled.get(target) == 2:
                descriptor_class = "storage_image"
            elif image_dim.get(target) == 6:
                descriptor_class = "input_attachment"
            else:
                descriptor_class = "sampled_image"
        elif target_kind == "struct":
            target_decorations = decorations.get(target, [])
            if any(decoration == DEC_BUFFER_BLOCK for decoration, _ in target_decorations):
                descriptor_class = "storage_buffer"
            elif any(decoration == DEC_BLOCK for decoration, _ in target_decorations):
                descriptor_class = "uniform_buffer"
            else:
                descriptor_class = "uniform_buffer"
        else:
            descriptor_class = "unknown"
        bindings[(descriptor_set, binding)] = descriptor_class
    return stage, bindings


def reflection_bindings(path):
    data = json.load(open(path))
    rows = {}
    for row in data.get("cbufs") or []:
        rows[(0, row["binding"])] = "uniform_buffer"
    for row in data.get("sbufs") or []:
        rows[(0, row["binding"])] = "storage_buffer"
    for row in data.get("textures") or []:
        rows[(0, row["binding"])] = "combined_image_sampler"
    for row in data.get("images") or []:
        rows[(0, row["binding"])] = "storage_image"
    return rows, data.get("stage")


records = []
for directory, variant in variant_dirs:
    if not os.path.isdir(directory):
        continue
    for name in sorted(os.listdir(directory)):
        if not name.endswith(".spv"):
            continue
        key = name[:-4]
        stage, bindings = analyse(os.path.join(directory, name))
        if stage is None:
            raise SystemExit("%s: no entry point" % name)
        reflection_path = os.path.join(variant_dirs[0][0], key + ".res.json")
        reflected = {}
        if os.path.isfile(reflection_path):
            reflected, _ = reflection_bindings(reflection_path)
        merged = []
        for key_pair in sorted(bindings):
            descriptor_class = bindings[key_pair]
            if key_pair in reflected:
                provenance = "reflection"
                if reflected[key_pair] != descriptor_class:
                    provenance = "conflict"
            else:
                provenance = "spirv_only"
            merged.append((key_pair[0], key_pair[1], descriptor_class, provenance))
        missing = [pair for pair in reflected if pair not in bindings]
        target = os.path.join(modules_dir, "%s_%s.spv" % (key, variant))
        shutil.copyfile(os.path.join(directory, name), target)
        records.append({"key": key, "variant": variant, "stage": stage,
                        "file": os.path.basename(target), "bindings": merged,
                        "reflection_only_bindings": sorted(missing)})

if not records:
    raise SystemExit("no SPIR-V modules found")

lines = ["# ffrs.shader.plan.v1"]
for record in records:
    lines.append("module %s %s %s %s" % (record["key"], record["variant"], record["stage"],
                                         record["file"]))
    for descriptor_set, binding, descriptor_class, provenance in record["bindings"]:
        lines.append("binding %s %s %d %d %s %s" % (record["key"], record["variant"],
                                                    descriptor_set, binding,
                                                    descriptor_class, provenance))

vert = [r for r in records if r["stage"] == "vert" and r["variant"] == base_variant]
frag = [r for r in records if r["stage"] == "frag" and r["variant"] == base_variant]
comp = [r for r in records if r["stage"] == "comp"]
vertex_stage = vert[0] if vert else None
smallest_frag = min(frag, key=lambda r: len(r["bindings"])) if frag else None
for record in records:
    if stage_filter and record["stage"] != stage_filter:
        continue
    identifier = "%s_%s" % (record["key"], record["variant"])
    if record["stage"] == "comp":
        lines.append("pipeline %s %s %s - -" % (identifier, record["key"], record["variant"]))
    elif record["stage"] == "frag" and vertex_stage:
        lines.append("pipeline %s %s %s %s %s" % (identifier, record["key"], record["variant"],
                                                 vertex_stage["key"], vertex_stage["variant"]))
    elif record["stage"] == "vert" and smallest_frag:
        lines.append("pipeline %s %s %s %s %s" % (identifier, record["key"], record["variant"],
                                                 smallest_frag["key"], smallest_frag["variant"]))

open(os.path.join(run_dir, "manifest.txt"), "w").write("\n".join(lines) + "\n")
summary = {"modules": [{"key": r["key"], "variant": r["variant"], "stage": r["stage"],
                         "binding_count": len(r["bindings"]),
                         "reflection_only_bindings": [list(pair) for pair in r["reflection_only_bindings"]],
                         "bindings": [{"set": s, "binding": b, "class": c, "provenance": p}
                                      for s, b, c, p in r["bindings"]]}
                        for r in records],
           "vertex_stage": vertex_stage["key"] if vertex_stage else None,
           "fragment_partner_for_vertex_stage": smallest_frag["key"] if smallest_frag else None}
open(os.path.join(run_dir, "layout-summary.json"), "w").write(json.dumps(summary, indent=2) + "\n")
print(json.dumps(summary, indent=2))
PY

run_step compiler-version "${CC}" --version
run_step compile "${CC}" --target=aarch64-linux-android28 --sysroot="${SYSROOT}" \
    -resource-dir "${RESOURCE}" -isystem "${SYSROOT}/usr/include/aarch64-linux-android" \
    -isystem "${SYSROOT}/usr/include/vulkan" -std=c11 -O2 -g -fPIE -fno-omit-frame-pointer \
    -Wall -Wextra -Werror -Wpedantic -MD -MF "${RUN}/probe.d" \
    -c "${RUN}/source_freeze/src/graphics/vulkan_probe/vulkan_shader_probe.c" -o "${RUN}/probe.o"
run_step link env "DYLD_LIBRARY_PATH=${RUST_ROOT}/lib" "${LINKER}" \
    --no-undefined --fatal-warnings --build-id=sha1 --eh-frame-hdr \
    -pie --dynamic-linker /system/bin/linker64 -z max-page-size=16384 -z noexecstack -z relro -z now \
    -o "${RUN}/vulkan_shader_probe" "${API_LIBS}/crtbegin_dynamic.o" "${RUN}/probe.o" \
    --start-group "${BUILTINS}" -L "${API_LIBS}" -lc -ldl --end-group "${API_LIBS}/crtend_android.o"
run_step elf "${TOOLCHAIN}/bin/llvm-readelf" --file-header --program-headers --dynamic-table \
    "${RUN}/vulkan_shader_probe"
run_step symbols "${TOOLCHAIN}/bin/llvm-readelf" --symbols "${RUN}/probe.o"

# ---------------------------------------------------------------------------
# The graphics-pipeline crash bisect (P-MM-27).  It is a second, independent
# binary: it owns its own instance and device and never links the runtime.
# ---------------------------------------------------------------------------
cp "${BISECT_SOURCE}" "${RUN}/source_freeze/src/graphics/vulkan_probe/"
mkdir -p "${RUN}/modules-trivial"
cp "${TRIVIAL_DIR}/trivial_vert.spv" "${RUN}/modules-trivial/trivial_vert.spv"
cp "${TRIVIAL_DIR}/trivial_frag.spv" "${RUN}/modules-trivial/trivial_frag.spv"
run_step bisect-compile "${CC}" --target=aarch64-linux-android28 --sysroot="${SYSROOT}" \
    -resource-dir "${RESOURCE}" -isystem "${SYSROOT}/usr/include/aarch64-linux-android" \
    -isystem "${SYSROOT}/usr/include/vulkan" -std=c11 -O2 -g -fPIE -fno-omit-frame-pointer \
    -Wall -Wextra -Werror -Wpedantic -MD -MF "${RUN}/bisect.d" \
    -c "${RUN}/source_freeze/src/graphics/vulkan_probe/vulkan_graphics_bisect.c" \
    -o "${RUN}/bisect.o"
run_step bisect-link env "DYLD_LIBRARY_PATH=${RUST_ROOT}/lib" "${LINKER}" \
    --no-undefined --fatal-warnings --build-id=sha1 --eh-frame-hdr \
    -pie --dynamic-linker /system/bin/linker64 -z max-page-size=16384 -z noexecstack -z relro -z now \
    -o "${RUN}/vulkan_graphics_bisect" "${API_LIBS}/crtbegin_dynamic.o" "${RUN}/bisect.o" \
    --start-group "${BUILTINS}" -L "${API_LIBS}" -lc -ldl --end-group "${API_LIBS}/crtend_android.o"
run_step bisect-elf "${TOOLCHAIN}/bin/llvm-readelf" --file-header --program-headers --dynamic-table \
    "${RUN}/vulkan_graphics_bisect"
run_step bisect-symbols "${TOOLCHAIN}/bin/llvm-readelf" --symbols "${RUN}/bisect.o"

# ---------------------------------------------------------------------------
# Evidence: ELF protections and the re-checked local build inputs.
# ---------------------------------------------------------------------------
run_step evidence python3 - "${RUN}" "${TOOLCHAIN}" "${MEMBERS}" "${CC}" "${LINKER}" "${BUILTINS}" <<'PY'
import hashlib
import json
import os
from pathlib import Path
import shlex
import struct
import sys
import zlib

run, toolchain, members, compiler, linker, builtins = [Path(p).resolve() for p in sys.argv[1:]]
manifest = json.loads(members.read_text())
assert manifest["whole_archive_sha1_verified"] is False, "expected explicit partial-NDK provenance"
known = {entry["path"]: entry for entry in manifest["members"]}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def verify_elf(path):
    data = Path(path).read_bytes()
    assert data[:6] == b"\x7fELF\x02\x01", "ELF64 little endian required"
    header = struct.unpack_from("<16sHHIQQQIHHHHHH", data)
    assert header[1] == 3 and header[2] == 183, "AArch64 ET_DYN required"
    offset, size, count = header[5], header[9], header[10]
    entries = [struct.unpack_from("<IIQQQQQQ", data, offset + i * size) for i in range(count)]
    loads = [e for e in entries if e[0] == 1]
    assert loads and all(e[7] >= 16384 for e in loads), "16KiB LOAD alignment required"
    assert all(not (e[1] & 1 and e[1] & 2) for e in loads), "writable executable LOAD"
    stack = [e for e in entries if e[0] == 0x6474E551]
    assert len(stack) == 1 and stack[0][1] & 1 == 0, "non-executable GNU_STACK required"
    assert any(e[0] == 0x6474E552 for e in entries), "GNU_RELRO required"
    assert any(e[0] == 0x6474E550 for e in entries), "GNU_EH_FRAME required"
    interpreter = [e for e in entries if e[0] == 3]
    assert len(interpreter) == 1, "PT_INTERP required"
    p = interpreter[0]
    assert data[p[2]:p[2] + p[5]].rstrip(b"\0") == b"/system/bin/linker64", "wrong Android interpreter"
    dynamic = [e for e in entries if e[0] == 2]
    assert len(dynamic) == 1, "PT_DYNAMIC required"
    p = dynamic[0]
    tags = [struct.unpack_from("<qQ", data, i) for i in range(p[2], p[2] + p[5], 16)]
    assert any(tag == 30 and value & 8 or tag == 0x6FFFFFFB and value & 1 or tag == 24
               for tag, value in tags), "BIND_NOW required"
    return {"elf64": True, "aarch64": True, "pie": True,
            "load_alignments": [e[7] for e in loads],
            "no_writable_executable_load": True, "noexecstack": True, "relro": True,
            "bind_now": True, "eh_frame_hdr": True, "interpreter": "/system/bin/linker64"}


api = toolchain / "sysroot/usr/lib/aarch64-linux-android/28"
dependency_text = (run / "probe.d").read_text().replace("\\\n", " ")
dependencies = [Path(p).resolve() for p in shlex.split(dependency_text.split(":", 1)[1])]
inputs = set(dependencies + [api / name for name in
                             ("crtbegin_dynamic.o", "crtend_android.o", "libc.so", "libdl.so")]
             + [builtins])
records = []
for path in sorted(inputs):
    content = path.read_bytes()
    entry = {"path": str(path), "bytes": len(content), "sha256": hashlib.sha256(content).hexdigest()}
    if path.is_relative_to(toolchain):
        relative = path.relative_to(toolchain).as_posix()
        assert relative in known, "input absent from validated NDK manifest: %s" % relative
        expected = known[relative]
        crc = "%08x" % (zlib.crc32(content) & 0xFFFFFFFF)
        assert len(content) == expected["bytes"] and crc == expected["crc32"], \
            "CRC mismatch: %s" % relative
        entry["crc32_rechecked"] = crc
    records.append(entry)
artifact = run / "vulkan_shader_probe"
bisect_artifact = run / "vulkan_graphics_bisect"
bisect_dependency_text = (run / "bisect.d").read_text().replace("\\\n", " ")
bisect_dependencies = [Path(p).resolve() for p in
                       shlex.split(bisect_dependency_text.split(":", 1)[1])]
bisect_inputs = set(bisect_dependencies + [api / name for name in
                                           ("crtbegin_dynamic.o", "crtend_android.o", "libc.so",
                                            "libdl.so")]
                    + [builtins])
bisect_records = []
for path in sorted(bisect_inputs):
    content = path.read_bytes()
    entry = {"path": str(path), "bytes": len(content), "sha256": hashlib.sha256(content).hexdigest()}
    if path.is_relative_to(toolchain):
        relative = path.relative_to(toolchain).as_posix()
        assert relative in known, "input absent from validated NDK manifest: %s" % relative
        expected = known[relative]
        crc = "%08x" % (zlib.crc32(content) & 0xFFFFFFFF)
        assert len(content) == expected["bytes"] and crc == expected["crc32"], \
            "CRC mismatch: %s" % relative
        entry["crc32_rechecked"] = crc
    bisect_records.append(entry)
trivial_modules = {}
for name in ("trivial_vert.spv", "trivial_frag.spv"):
    path = run / "modules-trivial" / name
    trivial_modules[name] = {"path": str(path), "bytes": path.stat().st_size,
                             "sha256": digest(path)}
evidence = {"schema": "ffrs.vulkan.shader.build.v1", "android_execution": "NOT_RUN",
            "git_head_at_build": (run / "git-head.txt").read_text().strip(),
            "source_sha256": digest(run / "source_freeze/src/graphics/vulkan_probe/vulkan_shader_probe.c"),
            "artifact": {"path": str(artifact), "bytes": artifact.stat().st_size,
                         "sha256": digest(artifact)},
            "elf_checks": verify_elf(artifact),
            "bisect": {"source_sha256": digest(
                           run / "source_freeze/src/graphics/vulkan_probe/vulkan_graphics_bisect.c"),
                       "artifact": {"path": str(bisect_artifact),
                                    "bytes": bisect_artifact.stat().st_size,
                                    "sha256": digest(bisect_artifact)},
                       "elf_checks": verify_elf(bisect_artifact),
                       "trivial_modules": trivial_modules,
                       "inputs": bisect_records},
            "links_against_vulkan": False,
            "loader": "dlopen at run time",
            "inputs": records,
            "tools": [{"path": str(p), "sha256": digest(p)} for p in (compiler, linker)],
            "ndk_manifest": {"path": str(members), "sha256": digest(members),
                             "source": manifest["source"],
                             "whole_archive_sha1_verified": False}}
(run / "build-evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")
print(json.dumps({"artifact": evidence["artifact"], "elf_checks": evidence["elf_checks"],
                  "bisect_artifact": evidence["bisect"]["artifact"],
                  "bisect_elf_checks": evidence["bisect"]["elf_checks"],
                  "trivial_modules": trivial_modules,
                  "validated_input_count": len(records),
                  "bisect_validated_input_count": len(bisect_records)}, indent=2))
PY

# ---------------------------------------------------------------------------
# Host build: the same source must start on a host without a Vulkan loader and
# say so.  This is the "no Vulkan in this build" evidence.
# ---------------------------------------------------------------------------
HOST_STATUS="SKIPPED"
if [[ "$(uname -s)" == "Darwin" || "$(uname -s)" == "Linux" ]]; then
    # The Vulkan headers of the partial NDK include vk_video/ headers that live
    # outside the vulkan/ directory.  Stage both directories under one include
    # root so the host build finds them without putting the NDK's C library
    # headers in front of the host's own.
    mkdir -p "${RUN}/host-include"
    ln -sfn "${SYSROOT}/usr/include/vulkan" "${RUN}/host-include/vulkan"
    ln -sfn "${SYSROOT}/usr/include/vk_video" "${RUN}/host-include/vk_video"
    if run_step host-compile "${CC}" -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic \
        -I "${RUN}/host-include/vulkan" -I "${RUN}/host-include" \
        -o "${RUN}/vulkan_shader_probe_host" \
        "${RUN}/source_freeze/src/graphics/vulkan_probe/vulkan_shader_probe.c"; then
        HOST_STATUS="BUILT"
        set +e
        "${RUN}/vulkan_shader_probe_host" "${RUN}/manifest.txt" "${RUN}/modules" \
            > "${RUN}/logs/host-run.log" 2> "${RUN}/logs/host-run.stderr.log"
        HOST_EXIT=$?
        set -e
        printf '%s\n' "${HOST_EXIT}" > "${RUN}/logs/host-run.exitcode"
        HOST_STATUS="$(python3 - "${RUN}/logs/host-run.log" <<'PY'
import json
import sys
try:
    report = json.load(open(sys.argv[1]))
    print(report.get("status", "UNKNOWN"))
except Exception:
    print("UNPARSEABLE")
PY
)"
        echo "Host run status: ${HOST_STATUS} (exit ${HOST_EXIT})"
        cat "${RUN}/logs/host-run.log"
    fi
fi

python3 - "${RUN}" "${HOST_STATUS}" <<'PY'
import json
import sys
from pathlib import Path

run = Path(sys.argv[1])
host_status = sys.argv[2]
evidence = json.loads((run / "build-evidence.json").read_text())
evidence["host_build"] = {"status": host_status,
                           "log": str(run / "logs/host-run.log") if host_status != "SKIPPED" else None}
(run / "build-evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")
PY

printf 'Android execution: NOT_RUN\nBuild evidence: %s\nManifest: %s\n' \
    "${RUN}" "${RUN}/manifest.txt"
