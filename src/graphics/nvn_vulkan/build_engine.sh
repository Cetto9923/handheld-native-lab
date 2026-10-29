#!/bin/sh
# Builds the ported NVN-to-Vulkan engine for Android ARM64 and proves that it links.
#
# What it proves: that every source in this tree compiles with the flags below and that the
# resulting shared object has no undefined symbol (-Wl,--no-undefined). It does NOT prove that the
# engine draws anything: that needs a Vulkan device, the FFRS NVN ABI and the game's shaders.
#
# The object files and the shared object go to <repo>/.local/build/nvn_vulkan, outside the git tree
# (the repository ignores .local/ and *.o / *.so).
#
# Usage:  sh build_engine.sh            (build; the exit code is the result)
#         NDK=<path> sh build_engine.sh (use another NDK)
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
NDK=${NDK:-/Users/yuyan9923/Library/Android/sdk/ndk/26.1.10909125}
# The NDK ships one prebuilt toolchain per host. Pick the one that holds the compiler: this host
# runs the x86_64 toolchain of the NDK under Rosetta, so the host's own tag is not the answer.
toolchain=""
for tag in darwin-x86_64 darwin-arm64 linux-x86_64; do
    if [ -x "$NDK/toolchains/llvm/prebuilt/$tag/bin/aarch64-linux-android28-clang" ]; then
        toolchain="$NDK/toolchains/llvm/prebuilt/$tag"
        break
    fi
done
CC="$toolchain/bin/aarch64-linux-android28-clang"
out="$repo/.local/build/nvn_vulkan"
lib="$out/libxbg.so"

if [ ! -x "$CC" ]; then
    echo "build_engine.sh: no aarch64-linux-android28-clang under $NDK/toolchains/llvm/prebuilt" >&2
    exit 2
fi

mkdir -p "$out"

CFLAGS="-std=c11 -fPIC -fno-strict-aliasing -O2 -Wall"
CXXFLAGS="-std=c++17 -fPIC -fno-strict-aliasing -O2 -Wall"
INCLUDES="-I$here -I$here/nvn -I$here/tex"
# xbg_texture.c reads the guest's texture bytes as signed chars; the default is unsigned on arm64.
CFLAGS="$CFLAGS -fsigned-char"

C_SOURCES="
engine/xbg_command_clone.c
engine/xbg_depth.c
engine/xbg_draw_plan.c
engine/xbg_framebuffer.c
engine/xbg_quad.c
engine/xbg_shader_key.c
engine/xbg_texture.c
engine/xbg_vertex.c
nvn/nvn_tex.c
tex/nvn_deswizzle.c
xbg_host_bridge.c
nvn_vk_adapter.c
"

CXX_SOURCES="
engine/xbg_blit.cpp
engine/xbg_clear.cpp
engine/xbg_game.cpp
engine/xbg_nvn.cpp
engine/xbg_sampler.cpp
engine/xbg_spirv_ldc.cpp
engine/xbg_spirv_quad.cpp
engine/xbg_spirv_raster.cpp
engine/xbg_spirv_temps.cpp
engine/xbg_spirv_warp.cpp
engine/xbg_stages.cpp
engine/xbg_vulkan.cpp
"

objects=""
for src in $C_SOURCES; do
    obj="$out/$(echo "$src" | tr '/' '_' | sed 's/\.c$/.o/')"
    echo "CC   $src"
    # shellcheck disable=SC2086
    "$CC" $CFLAGS $INCLUDES -c "$here/$src" -o "$obj"
    objects="$objects $obj"
done

for src in $CXX_SOURCES; do
    obj="$out/$(echo "$src" | tr '/' '_' | sed 's/\.cpp$/.o/')"
    echo "CXX  $src"
    # shellcheck disable=SC2086
    "$CC" $CXXFLAGS $INCLUDES -c "$here/$src" -o "$obj"
    objects="$objects $obj"
done

echo "LINK $lib"
# shellcheck disable=SC2086
"$CC" -shared -Wl,--no-undefined -o "$lib" $objects -lc++_static -lc++abi -lvulkan

echo "OK   $lib"
ls -l "$lib"
