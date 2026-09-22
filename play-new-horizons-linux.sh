#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
assets=$(dirname -- "$root")
profile=${XDG_DATA_HOME:-$HOME/.local/share}/new-horizons-play
snapshot_args=()

# The build tree's resource directories are symlinks into the editable source
# tree. Default play uses a checksummed immutable copy created explicitly after
# a successful build. Never fall back to the build tree or an older release
# package when that snapshot is missing or invalid.
has_custom_client=false
has_custom_resources=false
show_help=false
for arg in "$@"; do
	case $arg in
		--) break;;
		--client) has_custom_client=true;;
		--resources) has_custom_resources=true;;
		--help|-h) show_help=true;;
	esac
done
if ! $show_help && ! $has_custom_client; then
	snapshot=$(python3 "$root/tools/ci/linux_playable_snapshot.py" resolve \
		--store "$root/build/new-horizons-linux/playable-snapshots")
	snapshot_args=(--client "$snapshot/vcmiclient")
	if ! $has_custom_resources; then
		snapshot_args+=(--resources "$snapshot")
	fi
fi

exec "$root/tools/new-horizons-launch.sh" \
	--assets "$assets" \
	--profile "$profile" \
	"${snapshot_args[@]}" \
	"$@"
