#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

python_exe=$(cygpath -u "$1")
build_root=$2
test_exe="$build_root/install/nhArtStartupRuntimeTest.exe"
pack="$build_root/install/Mods/new-horizons/NewHorizons.nhart"
pin_args_file="$build_root/nhart-native-pin-args.bin"

cp -- "$build_root/bin/nhArtStartupRuntimeTest.exe" "$test_exe"
trap 'rm -f -- "$test_exe"' EXIT
mkdir -p -- "$build_root/nhart-regression-cache"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy
unset DISPLAY WAYLAND_DISPLAY

"$python_exe" tools/ci/prepare_nhart_native_pin.py --repo . --pack "$pack" > "$pin_args_file"
mapfile -d '' -t pin_args < "$pin_args_file"
"$test_exe" --pack "$pack" --cache-root "$build_root/nhart-regression-cache" "${pin_args[@]}"

"$python_exe" tools/ci/windows_nhart_interruption.py --repo . --probe "$test_exe" --pack "$pack" \
    --output "$build_root/nhart-io"
"$python_exe" tools/ci/windows_nhart_interruption.py --repo . --probe "$test_exe" --pack "$pack" \
    --output "$build_root/nhart-deep-io" --deep-cache
