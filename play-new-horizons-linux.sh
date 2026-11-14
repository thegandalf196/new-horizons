#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
initial_assets=$(dirname -- "$root")
profile=${XDG_DATA_HOME:-$HOME/.local/share}/new-horizons-play
store=${NH_PLAYABLE_STORE:-${XDG_DATA_HOME:-$HOME/.local/share}/new-horizons/playable-snapshots}
snapshot_args=()
nh_python=${NH_PYTHON:-$(command -v python3)}
[[ $nh_python == /* && -x $nh_python ]] || { printf '%s\n' 'New Horizons requires an absolute Python 3 executable.' >&2; exit 1; }

# The build tree's resource directories are symlinks into the editable source
# tree. Default play uses a checksummed immutable copy created explicitly after
# a successful build. Never fall back to the build tree or an older release
# package when that snapshot is missing or invalid.
has_custom_client=false
has_custom_resources=false
show_help=false
setup_requested=false
selected_profile=$profile
previous_arg=''
for arg in "$@"; do
	if [[ $previous_arg == --profile ]]; then selected_profile=$arg; fi
	case $arg in
		--) break;;
		--client|--client=*) has_custom_client=true;;
		--resources|--resources=*) has_custom_resources=true;;
		--profile=*) selected_profile=${arg#*=};;
		--select-assets|--setup-only|--noninteractive) setup_requested=true;;
		--help|-h) show_help=true;;
	esac
	previous_arg=$arg
done
if ! $show_help && ! $has_custom_client; then
	snapshot=$("$nh_python" "$root/tools/ci/linux_playable_snapshot.py" resolve \
		--store "$store")
	snapshot_client=$("$nh_python" "$root/tools/ci/linux_playable_snapshot.py" client --snapshot "$snapshot")
	snapshot_args=(--client "$snapshot_client")
	if ! $has_custom_resources; then
		snapshot_args+=(--resources "$snapshot")
	fi
fi

# Preserve the established launch contract until a setup-capable candidate is
# actually validated and promoted. This is the previous fixed source path,
# never a directory search or fallback to another executable/snapshot.
if ! $show_help && ! $has_custom_client && ! $setup_requested \
	&& [[ ! -e $snapshot/new-horizons-setup \
		&& ! -e $selected_profile.setup/original-assets.json ]]; then
	exec "$root/tools/new-horizons-launch.sh" --assets "$initial_assets" \
		--profile "$profile" "${snapshot_args[@]}" "$@"
fi

if $show_help; then
	printf '%s\n' 'New Horizons: first launch selects and remembers your Complete installation.' \
		'--assets DIR overrides the selection for one launch; --select-assets chooses again.' \
		'--setup-only saves a valid selection without starting the game.'
	exec "$root/tools/new-horizons-launch.sh" --help
fi
exec "$nh_python" "$root/tools/linux_assets_setup.py" \
	--launcher "$root/tools/new-horizons-launch.sh" \
	--initial-assets "$initial_assets" \
	--profile "$profile" \
	"${snapshot_args[@]}" \
	"$@"
