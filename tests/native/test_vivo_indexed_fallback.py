#!/usr/bin/env python3
"""Run renderer selection, readback dispatch and submit diagnostics with host fakes."""
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
ENGINE = ROOT / "src/graphics/nvn_vulkan/engine"
SOURCE = (ENGINE / "xbg_game.cpp").read_text()


def section(text, start, end):
    offset = text.index(start)
    return text[offset:text.index(end, offset)]


def production_fragments(source):
    init = source[source.index('extern "C" int xbg_game_initialize('):]
    assert init.index("vkGetPhysicalDeviceProperties(") < init.index("vivo_indexed_fallback =")
    gpu = source[source.index("bool gpu_draw_indexed(const p_draw_idx &a){"):]
    dispatch = source[source.index("void execute_draw_index_record(const p_draw_idx &a){"):]
    payload = (ENGINE.parent / "nvn/nvn_obj.h").read_text()
    return {
        "MATCH": section(source, "bool vivo_requires_direct_indexed(", "\n}\n") + "\n}",
        "CONFIG": section(init, "        vivo_indexed_fallback =", "\n    }"),
        "DISABLED": section(gpu, "    if(!gpu_indexed_enabled){", "    if(a.mdi_cnt)"),
        "PAYLOAD": next(line for line in payload.splitlines() if "} p_draw_idx;" in line),
        "GPU_DISPATCH": section(dispatch, "    if(gpu_draw_indexed(a))", "    static unsigned fallback_logs"),
        "READBACK": section(dispatch, "        cpu_access(a.ind,20,5);", "        note_large_indirect_index"),
        "DECODE_DRAW": section(dispatch, "        xbg_draw_call plan{}", "\n    }"),
        "SUBMIT": section(source, "            VkResult raw_submit_result=vkQueueSubmit(", "\n        }"),
    }


FIXTURE = r"""
#include <array>
#include <atomic>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <unistd.h>
#include "xbg_draw_plan.h"

struct VkPhysicalDeviceProperties {
    uint32_t vendorID, deviceID, driverVersion;
    const char *deviceName;
};
const VkPhysicalDeviceProperties target{0x5143, 0x07030002, 0x80267061, "Adreno (TM) 730"};
const std::map<std::string, std::string> target_props{
    {"ro.product.manufacturer", "vivo"}, {"ro.product.model", "V2301A"}, {"ro.soc.model", "SM8475"}
};
auto props = target_props;
constexpr size_t PROP_VALUE_MAX = 92;
int __system_property_get(const char *key, char *dst) {
    return snprintf(dst, PROP_VALUE_MAX, "%s", props[key].c_str());
}
@MATCH@

bool gpu_indexed_enabled, vivo_indexed_fallback, indirect_diag_enabled, indirect_proof_enabled, indirect_probe;
int indirect_mode;
uint64_t gpu_indexed_draws, cpu_indexed_nonempty, indirect_proof_checks, indirect_proof_mismatches;
std::string last_log;
__attribute__((format(printf, 1, 2))) void xbg_logf(const char *fmt, ...) {
    char text[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    last_log = text;
}
void configure(const std::string &base, const VkPhysicalDeviceProperties &properties) {
@CONFIG@
}
void marker(const std::string &base, const char *name, const char *value) {
    FILE *f = fopen((base + "/" + name).c_str(), "wb");
    assert(f);
    fputs(value, f);
    fclose(f);
}
bool logged(const std::string &text) { return last_log.find(text) != std::string::npos; }

@PAYLOAD@
struct { std::atomic<uint64_t> presents{1495}; } stats;
bool honor_pipeline_diagnostics = false;
int resident_dispatches = 0, direct_dispatches = 0, readbacks = 0;
p_draw_idx last_draw{};
bool gpu_draw_indexed(const p_draw_idx &) {
@DISABLED@
    ++resident_dispatches;
    return true;
}
std::array<uint32_t, 5> producer{24, 3, 7, static_cast<uint32_t>(-2), 9};
void cpu_access(uint64_t address, size_t bytes, int reason) {
    assert(bytes == sizeof(producer) && reason == 5);
    ++readbacks;
    memcpy(reinterpret_cast<void *>(address), producer.data(), bytes);
}
void probe_indirect_arguments(const p_draw_idx &, const uint32_t *) {}
void note_empty_indirect_draw(const char *, int, uint64_t, const uint32_t *, size_t) {}
void invalid_indirect_draw(const char *, int, int, int, uint64_t, uint64_t, const uint32_t *, size_t) {
    assert(false);
}
void draw(int prim, int first, int count, const p_draw_idx *p) {
    assert(prim == p->prim && first == 0 && count == p->count);
    last_draw = *p;
    ++direct_dispatches;
}
void dispatch(const p_draw_idx &a) {
@GPU_DISPATCH@
@READBACK@
@DECODE_DRAW@
}

using VkResult = int;
constexpr VkResult VK_SUCCESS = 0;
struct { int queue = 11; } cfg;
struct { int fence = 17, raw_handle = 1; } s;
constexpr unsigned slot_index = 2;
constexpr uint64_t slot_serial = 244;
int si, submit_result, submit_calls, check_calls;
constexpr uint64_t timing_clock() { return 99; }
namespace hud { std::atomic<unsigned> frame{88}; }
struct {
    int calls = 0, last_result = 0;
    bool get(int handle) const { return handle == 1; }
    void submit_done(int handle, int result, uint64_t end, unsigned frame) {
        assert(get(handle) && end == 99 && frame == 88);
        ++calls;
        last_result = result;
    }
} gpu_raw;
VkResult vkQueueSubmit(int queue, int count, const int *info, int fence) {
    assert(queue == cfg.queue && count == 1 && info == &si && fence == s.fence);
    ++submit_calls;
    return submit_result;
}
void check(VkResult result, const char *call) {
    ++check_calls;
    assert(!strcmp(call, "vkQueueSubmit(cfg.queue,1,&si,s.fence)"));
    if (result != VK_SUCCESS) {
        assert(logged("NVN submit failure pid=" + std::to_string(getpid()) + " frame=1495 slot=2 serial=244 result=-4"));
        assert(logged("vivo_match=1 indexed_resident=0 gpu_indexed_recorded=0 cpu_indexed_nonempty=1"));
        throw result;
    }
}
void submit() {
@SUBMIT@
}

int main(int argc, char **argv) {
    assert(argc == 2);
    const std::string dir = argv[1];
    configure(dir, target);
#ifndef __ANDROID__
    assert(!vivo_indexed_fallback && gpu_indexed_enabled && logged("source=default"));
    return 0;
#else
    if (!vivo_indexed_fallback || gpu_indexed_enabled) return 41;
    assert(!indirect_proof_enabled && logged("source=vivo-driver pid=" + std::to_string(getpid())));
    for (const auto &property : target_props) {
        for (const char *mismatch : {"different", ""}) {
            props[property.first] = mismatch;
            configure(dir, target);
            assert(!vivo_indexed_fallback && gpu_indexed_enabled && indirect_proof_enabled && logged("source=default"));
        }
        props = target_props;
    }
    for (auto field : {&VkPhysicalDeviceProperties::vendorID, &VkPhysicalDeviceProperties::deviceID,
                       &VkPhysicalDeviceProperties::driverVersion}) {
        auto other = target;
        ++(other.*field);
        configure(dir, other);
        assert(!vivo_indexed_fallback && gpu_indexed_enabled);
    }
    auto other = target;
    other.deviceName = "Adreno (TM) 735";
    configure(dir, other);
    assert(!vivo_indexed_fallback && gpu_indexed_enabled);
    configure(dir, target);
    std::array<uint32_t, 5> guest{};
    p_draw_idx command{};
    command.prim = 4; command.itype = 1; command.ibuf = 0x2000;
    command.ind = reinterpret_cast<uint64_t>(guest.data());
    dispatch(command);
    assert(readbacks == 1 && resident_dispatches == 0 && direct_dispatches == 1 && cpu_indexed_nonempty == 1);
    assert(last_draw.prim == 4 && last_draw.count == 24 && last_draw.instances == 3 && last_draw.ibuf == 0x200e);
    assert(last_draw.itype == 1 && last_draw.base_vertex == -2 && last_draw.base_instance == 9);
    producer[1] = 0;
    dispatch(command);
    assert(readbacks == 2 && direct_dispatches == 1 && cpu_indexed_nonempty == 1);
    last_log.clear();
    submit();
    assert(last_log.empty() && submit_calls == 1 && check_calls == 1);
    assert(gpu_raw.calls == 1 && gpu_raw.last_result == VK_SUCCESS);
    submit_result = -4;
    try { submit(); return 42; } catch (int result) { assert(result == -4); }
    assert(submit_calls == 2 && check_calls == 2);
    assert(gpu_raw.calls == 2 && gpu_raw.last_result == -4);
    marker(dir, "gfx-indexed-resident", "1");
    marker(dir, "gfx-indirect-proof", "1");
    marker(dir, "gfx-indirect-mode", "2");
    marker(dir, "gfx-indirect-probe", "1");
    configure(dir, target);
    assert(vivo_indexed_fallback && gpu_indexed_enabled && indirect_diag_enabled && indirect_proof_enabled);
    assert(indirect_probe && indirect_mode == 2 && logged("mode=2 source=marker") && cpu_indexed_nonempty == 0);
    dispatch(command);
    assert(resident_dispatches == 1 && direct_dispatches == 1 && readbacks == 2);
    marker(dir, "gfx-indexed-resident", "0");
    configure(dir, target);
    assert(vivo_indexed_fallback && !gpu_indexed_enabled && indirect_proof_enabled && logged("source=marker"));
    configure(dir, other);
    assert(!vivo_indexed_fallback && !gpu_indexed_enabled && logged("source=marker"));
    return 0;
#endif
}
"""


def main():
    with tempfile.TemporaryDirectory() as tmp:
        directory = Path(tmp)
        plan = directory / "plan.o"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-c", ENGINE / "xbg_draw_plan.c", "-o", plan], check=True)
        for case, android, legacy in (("android", True, False), ("host", False, False), ("old-default", True, True)):
            case_dir = directory / case
            case_dir.mkdir()
            pieces = production_fragments(SOURCE)
            if legacy:
                pieces["CONFIG"] = pieces["CONFIG"].replace(
                    "gpu_indexed_enabled = !vivo_indexed_fallback;", "gpu_indexed_enabled = true;"
                )
            cpp = FIXTURE
            for name, value in pieces.items():
                cpp = cpp.replace(f"@{name}@", value)
            source = case_dir / "test.cpp"
            source.write_text(cpp)
            binary = case_dir / "test"
            flags = ["-D__ANDROID__"] if android else []
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Werror=format", "-Wformat=2",
                            *flags, "-I", ENGINE, source, plan, "-o", binary], check=True)
            result = subprocess.run([binary, case_dir])
            expected = 41 if legacy else 0
            if result.returncode != expected:
                raise SystemExit(f"{case}: expected {expected}, got {result.returncode}")
            print(f"{case}: exit {result.returncode} (expected {expected})")


if __name__ == "__main__":
    main()
