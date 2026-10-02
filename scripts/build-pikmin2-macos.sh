#!/usr/bin/env bash
# Builds Pikmin 2 (games/pikmin2) on macOS.
#
# Two things differ from Linux, and both are handled here so the source tree
# stays exactly as upstream has it:
#
#  - The tree has headers whose names differ only in case (system.h and
#    System.h, Dolphin/mtx.h and the adapter's Dolphin/Mtx.h, ...). macOS
#    file systems are case-insensitive, so the wrong one wins. The sources are
#    mirrored onto a case-sensitive disk image (build/macos-cs.sparseimage,
#    sparse: it only takes the space actually used) and compiled there.
#  - The decompiled code relies on GCC's -fpermissive, which Clang treats as
#    hard errors, so it is built with Homebrew's GCC.
#
# The result is copied to build/pikmin2/pikmin2_pc, where scripts/run-fusion.sh
# and the launcher look for it. Requirements: brew install gcc cmake ninja sdl2
set -euo pipefail

fusion_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_type="${CMAKE_BUILD_TYPE:-RelWithDebInfo}"
image="${fusion_root}/build/macos-cs.sparseimage"
volume="${fusion_root}/build/macos-cs"
mirror="${volume}/pikmin2"
build_dir="${volume}/build-${build_type}"
output="${fusion_root}/build/pikmin2/pikmin2_pc"

gxx=""
for version in 16 15 14 13; do
    if command -v "g++-${version}" >/dev/null 2>&1; then
        gxx="g++-${version}"
        gcc="gcc-${version}"
        break
    fi
done
if [[ -z "${gxx}" ]]; then
    echo "Pikmin 2 needs GCC on macOS: brew install gcc" >&2
    exit 1
fi

mkdir -p "${fusion_root}/build" "${fusion_root}/build/pikmin2"
if [[ ! -f "${image}" ]]; then
    echo "Creating the case-sensitive build volume..."
    hdiutil create -quiet -size 20g -fs "Case-sensitive APFS" -volname NectarCaseSensitive \
        -type SPARSE "${image}"
fi
if ! mount | grep -q " on ${volume} "; then
    mkdir -p "${volume}"
    hdiutil attach -quiet -nobrowse -mountpoint "${volume}" "${image}"
fi

# Only the sources: game data, saves and build output stay where they are.
rsync -a --delete \
    --exclude '/assets/' --exclude '/save/' --exclude '/shader_cache/' \
    --exclude '/build*/' --exclude '/android/.gradle/' --exclude '/android/app/build/' \
    --exclude '/android/app/.cxx/' --exclude '/pikmin_settings.conf' --exclude '*.log' \
    "${fusion_root}/games/pikmin2/" "${mirror}/"

if [[ ! -f "${build_dir}/CMakeCache.txt" ]]; then
    CC="${gcc}" CXX="${gxx}" cmake -S "${mirror}" -B "${build_dir}" -G Ninja \
        -DCMAKE_BUILD_TYPE="${build_type}"
fi
cmake --build "${build_dir}" --target pikmin2_pc

cp -f "${build_dir}/pikmin2_pc" "${output}"
echo "Pikmin 2 built: ${output}"
