#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Linux package entry point. First launch selects the purchaser's original data.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
nh_python=${NH_PYTHON:-$(command -v python3)}
[[ $nh_python == /* && -x $nh_python ]] || { printf '%s\n' 'New Horizons requires an absolute Python 3 executable.' >&2; exit 1; }
exec "$nh_python" "$root/linux_assets_setup.py" \
	--launcher "$root/new-horizons-launch.sh" \
	--client "$root/new-horizons" --resources "$root" \
	--profile "${XDG_DATA_HOME:-$HOME/.local/share}/new-horizons-play" "$@"
