#!/usr/bin/env bash
# package-standalone.sh [--clean] [--skip-tests] [--sdl-prefix DIR]
#
# Builds a self-contained macOS package in packaging/macos/out/nectar-macos:
#
#   nectar-macos/
#     nectar            <- game, USA Rev 1 build
#     nectar-pal        <- game, European build
#     nectar-launcher   <- first-run installer (extracts your disc image)
#     lib/              <- SDL and anything else not shipped with macOS
#     README.txt
#
# The executables find SDL through @executable_path/lib, so the folder can be
# moved anywhere as long as lib/ stays beside them. Every binary is signed
# ad hoc: Apple Silicon refuses to run code whose signature was invalidated by
# rewriting its library paths.
#
# --sdl-prefix points CMake at a specific SDL2 install (CI builds one from
# source with a low deployment target). Without it, whatever SDL2 CMake finds
# is used -- Homebrew's sdl2-compat works, and its SDL3 is bundled with it.

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../.." && pwd)"
build_dir="${repo_root}/build-macos-standalone"
pal_build_dir="${repo_root}/build-macos-standalone-pal"
output_dir="${script_dir}/out/nectar-macos"
deployment_target="${MACOSX_DEPLOYMENT_TARGET:-12.0}"

clean=0
run_tests=1
sdl_prefix=""
while (($#)); do
    case "$1" in
        --clean) clean=1 ;;
        --skip-tests) run_tests=0 ;;
        --sdl-prefix) sdl_prefix="$2"; shift ;;
        --help|-h)
            printf 'Usage: %s [--clean] [--skip-tests] [--sdl-prefix DIR]\n' "$0"
            exit 0
            ;;
        *) printf 'Unknown option: %s\n' "$1" >&2; exit 2 ;;
    esac
    shift
done

if ((clean)); then
    rm -rf "${build_dir}" "${pal_build_dir}" "${output_dir}"
fi

# NATIVE_OPTIMIZE off: -march=native would tie the package to the CPU of the
# machine that built it. IPO stays on; it does not change the instruction set.
cmake_common=(
    -G Ninja
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_OSX_DEPLOYMENT_TARGET="${deployment_target}"
    -DPIKMIN_NATIVE_OPTIMIZE=OFF
    -DPIKMIN_ENABLE_IPO=ON
    -DPIKMIN_NATIVE_JAUDIO=ON
)
if [[ -n "${sdl_prefix}" ]]; then
    cmake_common+=(-DCMAKE_PREFIX_PATH="${sdl_prefix}" -DCMAKE_FIND_FRAMEWORK=LAST)
fi

printf '%s\n' "[1/5] Building (macOS ${deployment_target}+, $(uname -m))..."
cmake -S "${repo_root}" -B "${build_dir}" "${cmake_common[@]}"
cmake --build "${build_dir}"
# The European build is another executable: the decompiled game code is
# conditional on the release. Only the game binary is needed from it.
cmake -S "${repo_root}" -B "${pal_build_dir}" "${cmake_common[@]}" \
    -DPIKMIN_GAME_VERSION=VERSION_GPIP01_00 -DPIKMIN_BUILD_TESTS=OFF -DPIKMIN_BUILD_LAUNCHER=OFF
cmake --build "${pal_build_dir}" --target pikmin_pc

if ((run_tests)); then
    printf '%s\n' '[2/5] Offline tests...'
    ctest --test-dir "${build_dir}" --output-on-failure
else
    printf '%s\n' '[2/5] Tests skipped (--skip-tests).'
fi

printf '%s\n' '[3/5] Staging the package...'
rm -rf "${output_dir}"
mkdir -p "${output_dir}/lib"
cp "${build_dir}/bin/nectar" "${output_dir}/nectar"
cp "${pal_build_dir}/bin/nectar" "${output_dir}/nectar-pal"
cp "${build_dir}/bin/nectar-launcher" "${output_dir}/nectar-launcher"
cp "${script_dir}/README.txt" "${output_dir}/README.txt"
cp "${repo_root}/packaging/icon/open_nectar.png" "${output_dir}/open_nectar.png"
# -x keeps global symbols: the game's replacement operator new/delete must
# stay exported or libc++ frees game allocations with the system allocator.
strip -x "${output_dir}/nectar" "${output_dir}/nectar-pal" "${output_dir}/nectar-launcher"

printf '%s\n' '[4/5] Bundling libraries...'
is_system_lib() {
    case "$1" in
        /System/*|/usr/lib/*) return 0 ;;
        *) return 1 ;;
    esac
}

# The LC_RPATH entries of a Mach-O file, with @loader_path expanded.
rpaths_of() {
    local file="$1" dir
    dir="$(cd -- "$(dirname -- "$file")" && pwd)"
    otool -l "$file" | awk '/cmd LC_RPATH/ { getline; getline; print $2 }' \
        | sed -e "s|@loader_path|${dir}|" -e "s|@executable_path|${dir}|"
}

# Resolves a dependency as the build tree sees it to a real file.
resolve_dep() {
    local owner="$1" dep="$2" name candidate
    case "$dep" in
        @rpath/*)
            name="${dep#@rpath/}"
            while IFS= read -r candidate; do
                if [[ -f "${candidate}/${name}" ]]; then
                    printf '%s\n' "${candidate}/${name}"
                    return 0
                fi
            done < <(rpaths_of "$owner"; [[ -n "${sdl_prefix}" ]] && printf '%s\n' "${sdl_prefix}/lib")
            return 1
            ;;
        @*) return 1 ;;
        *) [[ -f "$dep" ]] && printf '%s\n' "$dep" ;;
    esac
}

deps_of() {
    otool -L "$1" | tail -n +2 | awk '{ print $1 }'
}

# Copies every non-system dependency of $1 into lib/ and points $1 at the
# copy. $2 is the prefix the reference should use: @executable_path/lib for
# the executables, @loader_path for libraries that already sit in lib/.
bundle_deps() {
    local file="$1" prefix="$2" dep source name own_id
    own_id="$(otool -D "$file" 2>/dev/null | tail -n +2 | head -n1 || true)"
    while IFS= read -r dep; do
        [[ -z "$dep" || "$dep" == "$own_id" ]] && continue
        is_system_lib "$dep" && continue
        source="$(resolve_dep "$file" "$dep")" || {
            printf 'Cannot resolve %s (needed by %s)\n' "$dep" "$file" >&2
            exit 1
        }
        name="$(basename "$dep")"
        if [[ ! -f "${output_dir}/lib/${name}" ]]; then
            cp -L "$source" "${output_dir}/lib/${name}"
            chmod 644 "${output_dir}/lib/${name}"
            install_name_tool -id "@rpath/${name}" "${output_dir}/lib/${name}" 2>/dev/null
            bundle_deps "${output_dir}/lib/${name}" "@loader_path"
        fi
        install_name_tool -change "$dep" "${prefix}/${name}" "$file" 2>/dev/null
    done < <(deps_of "$file")
}

# Build-machine rpaths would only leak local paths into the package.
drop_rpaths() {
    local file="$1" rpath
    while IFS= read -r rpath; do
        [[ -n "$rpath" ]] && install_name_tool -delete_rpath "$rpath" "$file" 2>/dev/null || true
    done < <(otool -l "$file" | awk '/cmd LC_RPATH/ { getline; getline; print $2 }')
}

sdl_source=""
for binary in nectar nectar-pal nectar-launcher; do
    if [[ -z "${sdl_source}" ]]; then
        sdl_dep="$(deps_of "${output_dir}/${binary}" | grep -m1 'libSDL2' || true)"
        [[ -n "${sdl_dep}" ]] && sdl_source="$(resolve_dep "${output_dir}/${binary}" "${sdl_dep}" || true)"
    fi
    bundle_deps "${output_dir}/${binary}" "@executable_path/lib"
    drop_rpaths "${output_dir}/${binary}"
done

# sdl2-compat (what Homebrew installs as "sdl2") is a thin layer that dlopens
# SDL3 at runtime; otool cannot see that. It looks for @loader_path/libSDL3.dylib.
# grep without -q reads to the end: an early exit would SIGPIPE strings and,
# under pipefail, make a match look like a miss.
if [[ -n "${sdl_source}" ]] && strings "${sdl_source}" | grep 'Failed loading SDL3 library' >/dev/null; then
    sdl3_source=""
    for candidate in "$(dirname -- "${sdl_source}")/libSDL3.0.dylib" \
                     "$(pkg-config --variable=libdir sdl3 2>/dev/null || true)/libSDL3.0.dylib" \
                     /opt/homebrew/opt/sdl3/lib/libSDL3.0.dylib /usr/local/opt/sdl3/lib/libSDL3.0.dylib; do
        if [[ -f "$candidate" ]]; then sdl3_source="$candidate"; break; fi
    done
    if [[ -z "${sdl3_source}" ]]; then
        printf 'SDL2 is sdl2-compat, but libSDL3.0.dylib was not found to bundle.\n' >&2
        exit 1
    fi
    cp -L "${sdl3_source}" "${output_dir}/lib/libSDL3.dylib"
    chmod 644 "${output_dir}/lib/libSDL3.dylib"
    install_name_tool -id "@rpath/libSDL3.dylib" "${output_dir}/lib/libSDL3.dylib" 2>/dev/null
    bundle_deps "${output_dir}/lib/libSDL3.dylib" "@loader_path"
    printf '  sdl2-compat detected: bundled SDL3 from %s\n' "${sdl3_source}"
fi

# SDL is zlib-licensed; ship its notice with it.
mkdir -p "${output_dir}/lib/licenses"
cat >"${output_dir}/lib/licenses/SDL.txt" <<'EOF'
Simple DirectMedia Layer
Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
EOF

# Signing last: any install_name_tool edit after this would void it.
for file in "${output_dir}"/lib/*.dylib "${output_dir}/nectar" "${output_dir}/nectar-pal" "${output_dir}/nectar-launcher"; do
    codesign --force --sign - "$file" >/dev/null 2>&1
done
printf '  %d libraries bundled.\n' "$(find "${output_dir}/lib" -name '*.dylib' | wc -l | tr -d ' ')"

printf '%s\n' '[5/5] Verifying the package...'
failed=0
for file in "${output_dir}"/lib/*.dylib "${output_dir}/nectar" "${output_dir}/nectar-pal" "${output_dir}/nectar-launcher"; do
    while IFS= read -r dep; do
        case "$dep" in
            /System/*|/usr/lib/*|@executable_path/lib/*|@loader_path/*|@rpath/*) ;;
            *) printf '  %s still links %s\n' "$(basename "$file")" "$dep" >&2; failed=1 ;;
        esac
    done < <(deps_of "$file" | grep -v -F "$(otool -D "$file" 2>/dev/null | tail -n +2 | head -n1)" || true)
    codesign --verify "$file" || failed=1
    minos="$(otool -l "$file" | awk '/LC_BUILD_VERSION/ { found = 1 } found && $1 == "minos" { print $2; exit }')"
    printf '  %-26s %s, macOS %s+\n' "$(basename "$file")" "$(lipo -archs "$file")" "${minos:-?}"
done
if ((failed)); then
    printf 'Package verification failed.\n' >&2
    exit 1
fi
# Load test from the package itself, with nothing on the library path. The
# audio self-test exits 77 (skipped) without game data, 0 with it.
(
    cd "${output_dir}"
    env -u DYLD_LIBRARY_PATH -u DYLD_FALLBACK_LIBRARY_PATH ./nectar-launcher --help >/dev/null
    set +e
    env -u DYLD_LIBRARY_PATH -u DYLD_FALLBACK_LIBRARY_PATH ./nectar --audio-self-test >/dev/null 2>&1
    status=$?
    set -e
    if [[ $status -ne 0 && $status -ne 77 ]]; then
        printf 'nectar failed to start from the package (exit %d).\n' "$status" >&2
        exit 1
    fi
)

printf 'Package ready: %s\n' "${output_dir}"
