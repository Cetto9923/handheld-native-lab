#!/usr/bin/env python3
"""Run the fragment-interface experiment on the RG477M and collect the numbers.

Why this exists (P-MM-29):

The device rejects the game's fragment modules with
``VK_ERROR_INITIALIZATION_FAILED`` (-3).  Reading the modules says why, but
reading is not measuring: this script builds one variant per hypothesis,
puts every variant in front of the driver **in one probe run**, and writes
the per-pipeline return codes out.

One probe run holds every hypothesis apart: each variant is its own
``vkCreateGraphicsPipelines`` call with its own module pair, so a pipeline
that is rejected says nothing about the pipeline next to it.  The run is
recorded in its own directory; nothing is overwritten.

The variants:

=================  =========================================================
``base``           the six game modules, untouched
``capfix``         the six modules with the capabilities they do not use
                   removed (the coordinator's fix_spirv_capabilities.py)
``capfix_nonp``    the three fragment modules with the ``NoPerspective``
                   decoration taken off their input variables
``capfix_vertnonp`` the vertex module with ``NoPerspective`` put on its
                   location 0 output
``syn``            a synthetic vertex module that writes locations 0..5,
                   used to test whether a fragment module that reads a
                   location the real vertex does not write is the reason
                   for the rejection
=================  =========================================================

The synthetic vertex is test scaffolding, not a game module.  It exists to
measure one thing: whether the driver's objection is the interface or the
module.

Usage:
    fix_spirv_experiment.py --spv-dir <dir> --out <experiment-dir> \\
        [--device SERIAL] [--skip-device]
"""
import json
import os
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
INTERFACE_TOOL = os.path.join(HERE, "fix_spirv_fragment_interface.py")

VERTEX_KEY = "9cb65be4f7d0d498"
FRAGMENT_KEYS = ["2ae58c692fb7fbb8", "174293a30c315aed", "3fefb8e379a30f64"]
COMPUTE_KEYS = ["41b20b5f09269ad8", "86affd63144799de"]

# The pipelines of the experiment.  Each line is one hypothesis.
PIPELINES = [
    # id, fragment key, fragment variant, vertex key, vertex variant, question
    ("ctrl-small-orig", "2ae58c692fb7fbb8", "base", VERTEX_KEY, "base",
     "control: the untouched game modules"),
    ("step1-small-capfix", "2ae58c692fb7fbb8", "capfix", VERTEX_KEY, "capfix",
     "step 1: only the capability declarations removed"),
    ("fix-small-nonp", "2ae58c692fb7fbb8", "capfix_nonp", VERTEX_KEY, "capfix",
     "capabilities removed and NoPerspective taken off the fragment input"),
    ("fix-small-vertnonp", "2ae58c692fb7fbb8", "capfix", VERTEX_KEY, "capfix_vertnonp",
     "the other side: NoPerspective put on the vertex output instead"),
    ("ctrl-small-synvert", "2ae58c692fb7fbb8", "capfix", "synthetic_vert", "syn",
     "a smooth synthetic vertex: the interpolation mismatch must survive"),
    ("caps-frag-only", "2ae58c692fb7fbb8", "base", VERTEX_KEY, "capfix",
     "only the fragment's own capability declarations are removed"),
    ("caps-vert-only", "2ae58c692fb7fbb8", "capfix", VERTEX_KEY, "base",
     "only the vertex's own capability declarations are removed"),
    ("ctrl-big1-orig", "174293a30c315aed", "base", VERTEX_KEY, "base",
     "control for the first large fragment module"),
    ("step1-big1-capfix", "174293a30c315aed", "capfix", VERTEX_KEY, "capfix",
     "capabilities removed, the interface left as it is"),
    ("loc-big1-synvert", "174293a30c315aed", "capfix", "synthetic_vert", "syn",
     "a vertex that writes location 1 as well"),
    ("ctrl-big2-orig", "3fefb8e379a30f64", "base", VERTEX_KEY, "base",
     "control for the second large fragment module"),
    ("step1-big2-capfix", "3fefb8e379a30f64", "capfix", VERTEX_KEY, "capfix",
     "capabilities removed, the interface left as it is"),
    ("loc-big2-synvert", "3fefb8e379a30f64", "capfix", "synthetic_vert", "syn",
     "a vertex that writes location 1 as well"),
    ("compute-1", "41b20b5f09269ad8", "capfix", "-", "-",
     "compute regression: the first compute module"),
    ("compute-2", "86affd63144799de", "capfix", "-", "-",
     "compute regression: the second compute module"),
]

# The capabilities the modules declare and do not use.  The tool refuses to
# drop one the module uses, and that refusal is part of the evidence.
UNUSED_CAPABILITIES = ["Float64", "FragmentShaderPixelInterlockEXT"]


# --------------------------------------------------------------------------
# A synthetic vertex module, emitted here so the experiment needs no tool
# outside this repository and no game data.
# --------------------------------------------------------------------------

def _literal_string(text):
    data = text.encode("utf-8") + b"\0"
    padding = (-len(data)) % 4
    data += b"\0" * padding
    return list(struct.unpack("<%dI" % (len(data) // 4), data))


def _instruction(opcode, operands):
    return [((len(operands) + 1) << 16) | opcode] + list(operands)


def synthetic_vertex(locations, key="synthetic_vert"):
    """A vertex module that writes a v4float at each of ``locations``."""
    words = [0x07230203, 0x00010000, 0, 0, 0]

    ids = {}
    next_id = 1
    for name in ("float", "v4float", "void", "fn", "ptr_out", "main", "f0", "v4"):
        ids[name] = next_id
        next_id += 1
    outputs = []
    for _ in locations:
        outputs.append(next_id)
        next_id += 1
    label = next_id
    next_id += 1
    bound = next_id

    words += _instruction(17, [1])                       # OpCapability Shader
    words += _instruction(14, [0, 1])                    # OpMemoryModel Logical GLSL450
    words += _instruction(15, [0, ids["main"]] + _literal_string("main") + outputs)
    for location, output in zip(locations, outputs):
        words += _instruction(71, [output, 30, location])  # OpDecorate Location
    words += _instruction(22, [ids["float"], 32])      # OpTypeFloat 32
    words += _instruction(23, [ids["v4float"], ids["float"], 4])
    words += _instruction(19, [ids["void"]])             # OpTypeVoid
    words += _instruction(33, [ids["fn"], ids["void"]])  # OpTypeFunction
    words += _instruction(32, [ids["ptr_out"], 3, ids["v4float"]])  # OpTypePointer Output
    words += _instruction(43, [ids["float"], ids["f0"], 0])         # OpConstant %float 0
    words += _instruction(44, [ids["v4float"], ids["v4"],
                               ids["f0"], ids["f0"], ids["f0"], ids["f0"]])
    for output in outputs:
        words += _instruction(59, [ids["ptr_out"], output, 3])  # OpVariable Output
    words += _instruction(54, [ids["void"], ids["main"], 0, ids["fn"]])  # OpFunction
    words += _instruction(248, [label])                  # OpLabel
    for output in outputs:
        words += _instruction(62, [output, ids["v4"]])   # OpStore
    words += _instruction(253, [])                       # OpReturn
    words += _instruction(56, [])                        # OpFunctionEnd

    words[3] = bound
    return words


# --------------------------------------------------------------------------

def run(command, cwd=None, env=None):
    completed = subprocess.run(command, cwd=cwd, env=env, capture_output=True, text=True)
    return completed.returncode, completed.stdout, completed.stderr


def copy_module(source, destination):
    shutil.copyfile(source, destination)


def make_variants(spv_dir, out_dir, report):
    """Build the variant directories.  Returns the list of directory=variant."""
    variants_dir = os.path.join(out_dir, "variants")
    os.makedirs(variants_dir, exist_ok=True)

    base = os.path.join(variants_dir, "base")
    os.makedirs(base, exist_ok=True)
    for name in sorted(os.listdir(spv_dir)):
        if name.endswith(".spv") or name.endswith(".res.json"):
            copy_module(os.path.join(spv_dir, name), os.path.join(base, name))

    def interface_tool(source, destination, extra, report_name):
        command = [sys.executable, INTERFACE_TOOL, "--in", source, "--out", destination,
                   "--report", os.path.join(out_dir, report_name)] + extra
        code, stdout, stderr = run(command)
        report.setdefault("interface_tool_runs", []).append({
            "command": command, "returncode": code, "stderr": stderr.strip(),
            "report": os.path.join(out_dir, report_name),
        })
        if code != 0:
            raise SystemExit("the interface tool failed on %s: %s" % (source, stderr))
        return json.load(open(os.path.join(out_dir, report_name)))

    caps = []
    for capability in UNUSED_CAPABILITIES:
        caps.extend(["--drop-capability", capability])
    # The compute modules write to a storage image of unknown format, so they
    # need the capability that says so; the fragment and vertex modules do not
    # write to a storage image and must not declare it.
    compute_caps = list(caps) + ["--add-capability", "StorageImageWriteWithoutFormat"]
    vertex_source = os.path.join(spv_dir, VERTEX_KEY + ".spv")

    # capfix: the capabilities the modules declare and do not use are removed.
    capfix = os.path.join(variants_dir, "capfix")
    os.makedirs(capfix, exist_ok=True)
    for key in [VERTEX_KEY] + FRAGMENT_KEYS + COMPUTE_KEYS:
        interface_tool(os.path.join(spv_dir, key + ".spv"),
                       os.path.join(capfix, key + ".spv"),
                       compute_caps if key in COMPUTE_KEYS else caps,
                       "report-%s-capfix.json" % key)

    # capfix_nonp: the fragment modules without the NoPerspective decoration.
    nonp = os.path.join(variants_dir, "capfix_nonp")
    os.makedirs(nonp, exist_ok=True)
    for key in FRAGMENT_KEYS:
        interface_tool(os.path.join(capfix, key + ".spv"),
                       os.path.join(nonp, key + ".spv"),
                       ["--drop-no-perspective", "--vertex", vertex_source],
                       "report-%s-nonp.json" % key)

    # capfix_vertnonp: the vertex module with NoPerspective on its output.
    vertnonp = os.path.join(variants_dir, "capfix_vertnonp")
    os.makedirs(vertnonp, exist_ok=True)
    capfix_vertex = os.path.join(capfix, VERTEX_KEY + ".spv")
    interface_tool(capfix_vertex,
                   os.path.join(vertnonp, VERTEX_KEY + ".spv"),
                   ["--vertex", capfix_vertex,
                    "--add-no-perspective-to-vertex-output", "0"],
                   "report-%s-vertnonp.json" % VERTEX_KEY)

    # syn: the synthetic vertex that writes locations 0..5.
    syn = os.path.join(variants_dir, "syn")
    os.makedirs(syn, exist_ok=True)
    words = synthetic_vertex([0, 1, 2, 3, 4, 5])
    open(os.path.join(syn, "synthetic_vert.spv"), "wb").write(
        struct.pack("<%dI" % len(words), *words))

    return [base, capfix, nonp, vertnonp, syn]


def rewrite_manifest(manifest_path, pipeline_lines):
    """Keep the module and binding lines; replace the pipeline lines."""
    kept = []
    for line in open(manifest_path).read().splitlines():
        if line.startswith("pipeline "):
            continue
        kept.append(line)
    kept.extend(pipeline_lines)
    open(manifest_path, "w").write("\n".join(kept) + "\n")


def main(argv):
    spv_dir = None
    out_dir = None
    local_root = None
    device = "RG477M01445122"
    skip_device = False
    index = 1
    while index < len(argv):
        argument = argv[index]
        if argument == "--spv-dir":
            index += 1
            spv_dir = argv[index]
        elif argument == "--out":
            index += 1
            out_dir = argv[index]
        elif argument == "--local-root":
            index += 1
            local_root = argv[index]
        elif argument == "--device":
            index += 1
            device = argv[index]
        elif argument == "--skip-device":
            skip_device = True
        else:
            sys.stderr.write("unknown argument: %s\n" % argument)
            return 3
        index += 1
    if not spv_dir or not out_dir:
        sys.stderr.write(__doc__)
        return 3
    root = os.path.abspath(os.path.join(HERE, "..", ".."))
    if local_root is None:
        # The untracked evidence area lives in the main checkout, not in a
        # worktree, so the caller says where it is.
        local_root = os.environ.get("FFRS_LOCAL_ROOT", os.path.join(root, ".local"))
    local_root = os.path.abspath(local_root)
    if not os.path.isdir(local_root):
        sys.stderr.write("the local evidence area is missing: %s\n" % local_root)
        return 3
    if os.path.exists(out_dir):
        sys.stderr.write("refusing to overwrite an existing experiment: %s\n" % out_dir)
        return 3
    os.makedirs(out_dir)

    report = {"schema": "ffrs.vulkan.fragment.experiment.v1", "device": device,
              "spv_dir": spv_dir, "out_dir": out_dir, "local_root": local_root}

    directories = make_variants(spv_dir, out_dir, report)
    variant_names = ["base", "capfix", "capfix_nonp", "capfix_vertnonp", "syn"]
    report["variants"] = dict(zip(variant_names, directories))

    root = os.path.abspath(os.path.join(HERE, "..", ".."))
    build_dir = os.path.join(out_dir, "build")
    env = dict(os.environ)
    env.update({
        "FFRS_VK_TOOLCHAIN": os.path.join(local_root, "minimal-ndk/toolchain"),
        "FFRS_VK_MEMBERS": os.path.join(local_root, "minimal-ndk/validated-members.json"),
        "FFRS_VK_BUILD_DIR": build_dir,
        "FFRS_SPV_TRIVIAL": os.path.join(local_root, "shader-verify/spv-trivialpair"),
        "FFRS_SPV_VARIANTS": ",".join("%s=%s" % (d, n)
                                       for d, n in zip(directories, variant_names)),
    })
    build_command = [os.path.join(root, "scripts/graphics/build_vulkan_shader_probe_android.sh")]
    code, stdout, stderr = run(build_command, cwd=root, env=env)
    report["build"] = {"command": build_command, "returncode": code,
                       "stdout_tail": stdout[-2000:], "stderr_tail": stderr[-2000:]}
    if code != 0:
        open(os.path.join(out_dir, "run.json"), "w").write(json.dumps(report, indent=1))
        sys.stderr.write("the build failed: %s\n" % stderr[-2000:])
        return code

    manifest_path = os.path.join(build_dir, "manifest.txt")
    pipeline_lines = ["pipeline %s %s %s %s %s" % (identifier, fragment_key,
                                                   fragment_variant, vertex_key,
                                                   vertex_variant)
                      for identifier, fragment_key, fragment_variant, vertex_key,
                      vertex_variant, _ in PIPELINES]
    rewrite_manifest(manifest_path, pipeline_lines)
    report["manifest"] = manifest_path
    report["pipelines"] = [
        {"id": identifier, "fragment": [fragment_key, fragment_variant],
         "vertex": [vertex_key, vertex_variant], "question": question}
        for identifier, fragment_key, fragment_variant, vertex_key, vertex_variant,
        question in PIPELINES]

    if skip_device:
        open(os.path.join(out_dir, "run.json"), "w").write(json.dumps(report, indent=1))
        return 0

    remote = "/data/local/tmp/ffrs-pm29"
    steps = []
    for command in (
        ["adb", "-s", device, "shell", "rm", "-rf", remote],
        ["adb", "-s", device, "shell", "mkdir", "-p", remote + "/modules"],
        ["adb", "-s", device, "push", os.path.join(build_dir, "vulkan_shader_probe"),
         remote + "/vulkan_shader_probe"],
        ["adb", "-s", device, "push", os.path.join(build_dir, "modules/."), remote + "/modules/"],
        ["adb", "-s", device, "push", manifest_path, remote + "/manifest.txt"],
    ):
        code, stdout, stderr = run(command)
        steps.append({"command": command, "returncode": code,
                      "stdout": stdout.strip(), "stderr": stderr.strip()})
        if code != 0:
            report["device_steps"] = steps
            open(os.path.join(out_dir, "run.json"), "w").write(json.dumps(report, indent=1))
            sys.stderr.write("the device step failed: %s\n" % " ".join(command))
            return code

    probe_command = ["adb", "-s", device, "shell",
                     "%s/vulkan_shader_probe %s/manifest.txt %s/modules"
                     % (remote, remote, remote)]
    code, stdout, stderr = run(probe_command)
    steps.append({"command": probe_command, "returncode": code})
    open(os.path.join(out_dir, "device-stdout.txt"), "w").write(stdout)
    open(os.path.join(out_dir, "device-stderr.txt"), "w").write(stderr)
    report["device_steps"] = steps
    report["probe_returncode"] = code

    try:
        probe = json.loads(stdout)
    except ValueError:
        report["probe_json"] = "PARSE_FAILED"
        open(os.path.join(out_dir, "run.json"), "w").write(json.dumps(report, indent=1))
        sys.stderr.write("the probe did not print JSON\n")
        return 5
    report["probe_json"] = "OK"

    results = {}
    for pipeline in probe.get("tiers", {}).get("3_pipeline", {}).get("pipelines", []):
        draw = pipeline.get("draw", {})
        results[pipeline["id"]] = {
            "created": pipeline.get("created"),
            "vk_result": pipeline.get("vk_result"),
            "vk_result_name": pipeline.get("vk_result_name"),
            "draw_attempted": draw.get("attempted"),
            "draw_consumed": draw.get("consumed"),
            "submit": draw.get("submit_vk_result_name"),
            "wait": draw.get("wait_vk_result_name"),
        }
    report["results"] = results
    report["tiers"] = {name: probe.get("tiers", {}).get(name, {}).get("status")
                        for name in ("1_device_facts", "2_shader_module", "3_pipeline", "4_draw")}
    report["modules_created"] = probe.get("tiers", {}).get("2_shader_module", {}).get("created")
    report["modules_total"] = probe.get("tiers", {}).get("2_shader_module", {}).get("total")
    report["device_name"] = probe.get("device_facts", {}).get("physical_device", {}).get("name")
    report["shaderFloat64_supported"] = probe.get("device_facts", {}).get(
        "features_supported", {}).get("shaderFloat64")
    open(os.path.join(out_dir, "run.json"), "w").write(json.dumps(report, indent=1) + "\n")
    print(json.dumps(report["results"], indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
