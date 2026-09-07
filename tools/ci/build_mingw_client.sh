#!/usr/bin/env bash
# Local Linux -> Windows x64 build. Never uses MSVC prebuilt libraries or Linux client outputs.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo"
export PATH="$repo/build/new-horizons-linux/toolchain/venv/bin:$PATH"
export CONAN_HOME="$repo/build/new-horizons-linux/toolchain/conan"
out="$repo/build/new-horizons-windows-cross"
# Autotools rejects source/build paths containing spaces. Keep Conan metadata in
# the prescribed isolated home, but its package storage in a short user-local path.
package_storage="${NH_MINGW_PACKAGE_STORAGE:-$HOME/.cache/new-horizons-windows-cross/conan-p}"
if [[ "$package_storage" != /* || "$package_storage" == *" "* ]]; then
    echo 'NH_MINGW_PACKAGE_STORAGE must be an absolute, space-free user-local path' >&2
    exit 2
fi
mkdir -p "$out"
stage="${1:-deps}"
case "$stage" in deps|generate|configure|build) ;; *) echo 'Usage: build_mingw_client.sh [deps|generate|configure|build]' >&2; exit 2 ;; esac

if [[ "$stage" == deps || "$stage" == generate ]]; then
    build_policy=missing
    [[ "$stage" != generate ]] || build_policy=never
    if [[ ! -f "$CONAN_HOME/profiles/nh-linux-build" ]]; then
        conan profile detect --name=nh-linux-build
    fi
    remap_args=()
    if [[ "${NH_MINGW_REMAP_SOURCES:-0}" == 1 ]]; then
        # A distinct package-ID configuration prevents reuse/overwrite of the
        # original unremapped dependency payload. Preserve that frozen evidence.
        # Dependencies are compiled in the enforced space-free cache, not
        # the repository. Autotools does not reinterpret quotes inside CFLAGS.
        remap_flags="$(python - "$package_storage" <<'PY'
import json, sys
print(json.dumps(['-ffile-prefix-map=' + sys.argv[1] + '=conan-source']))
PY
)"
        remap_args+=( -c:h "tools.build:cflags=$remap_flags" -c:h "tools.build:cxxflags=$remap_flags"
            -c:h 'tools.info.package_id:confs=["tools.build:cflags","tools.build:cxxflags","tools.build:compiler_executables","tools.gnu:pkg_config"]'
            # FFmpeg embeds configure arguments. Its two observed personal paths
            # are the assembler/pkgconf executables, not __FILE__. Keep tool
            # selection via the exact Conan build environment PATH; do not embed
            # mapping flags (which themselves contain private source prefixes).
            -c:h 'ffmpeg/*:tools.build:cflags=[]' -c:h 'ffmpeg/*:tools.build:cxxflags=[]'
            -c:h 'ffmpeg/*:tools.build:compiler_executables={"c":"x86_64-w64-mingw32-gcc-posix","cpp":"x86_64-w64-mingw32-g++-posix","rc":"x86_64-w64-mingw32-windres","asm":"nasm"}'
            -c:h 'ffmpeg/*:tools.gnu:pkg_config=pkgconf' )
    fi
    conan install tools/ci/conan_mingw_client.py \
        -pr:h "$repo/tools/ci/conan-mingw-x64" -pr:b nh-linux-build \
        -s:b compiler.cppstd=20 --build="$build_policy" "${remap_args[@]}" \
        -cc "core.cache:storage_path=$package_storage" \
        -cc core.net.http:max_retries=0 -cc core.net.http:timeout=20 \
        -c:h tools.files.download:retry=0 -c:b tools.files.download:retry=0 \
        --output-folder="$out/conan-generated" --format=json > "$out/install.json"
elif [[ "$stage" == configure ]]; then
    cmake -S "$repo" -B "$out/client" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$out/conan-generated/conan_toolchain.cmake" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$out/install" \
        -DENABLE_CLIENT=ON -DENABLE_SERVER=OFF -DENABLE_LOBBY=OFF \
        -DENABLE_LAUNCHER=OFF -DENABLE_EDITOR=OFF -DENABLE_TRANSLATIONS=OFF \
        -DENABLE_SINGLE_APP_BUILD=OFF -DENABLE_MINIMAL_LIB=OFF -DENABLE_STATIC_LIBS=OFF \
        -DENABLE_INNOEXTRACT=OFF -DENABLE_MMAI=OFF -DENABLE_DISCORD=OFF -DENABLE_TEST=OFF \
        -DENABLE_NULLKILLER2_AI=ON -DENABLE_BATTLE_AI=ON -DENABLE_STUPID_AI=ON \
        -DENABLE_VIDEO=ON -DENABLE_SDL3=ON -DENABLE_CCACHE=OFF \
        -DENABLE_PCH=ON -DENABLE_STRICT_COMPILATION=OFF -DCOPY_CONFIG_ON_BUILD=OFF
else
    cmake --build "$out/client" --target vcmiclient --parallel 2
    cmake --install "$out/client"
fi
