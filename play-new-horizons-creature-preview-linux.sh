#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
store=${NH_CREATURE_PREVIEW_STORE:-${XDG_DATA_HOME:-$HOME/.local/share}/new-horizons/cabir-wisp-preview-snapshots}
snapshot=$(python3 "$root/tools/ci/linux_playable_snapshot.py" resolve --store "$store")
printf '%s\n' 'New Horizons creature candidate: supplied Cabir/Magi animations and provisional Wisp gameplay. Start a new game.'
exec "$root/tools/new-horizons-launch.sh" \
	--assets "$(dirname -- "$root")" \
	--profile "${XDG_DATA_HOME:-$HOME/.local/share}/new-horizons-creature-preview" \
	--client "$snapshot/vcmiclient" --resources "$snapshot" "$@"
