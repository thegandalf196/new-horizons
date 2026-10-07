#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
snapshot=$(python3 "$root/tools/ci/linux_playable_snapshot.py" resolve \
	--store "$root/build/new-horizons-linux/cabir-wisp-preview-snapshots")
printf '%s\n' 'Cabir / Wisp ART PREVIEW: borrowed Wisp gameplay; incomplete Cabir states use placeholders.'
exec "$root/tools/new-horizons-launch.sh" \
	--assets "$(dirname -- "$root")" \
	--profile "${XDG_DATA_HOME:-$HOME/.local/share}/new-horizons-creature-preview" \
	--client "$snapshot/vcmiclient" --resources "$snapshot" "$@"
