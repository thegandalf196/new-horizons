#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
assets=$(dirname -- "$root")
profile=${XDG_DATA_HOME:-$HOME/.local/share}/new-horizons-play

exec "$root/tools/new-horizons-launch.sh" \
	--assets "$assets" \
	--profile "$profile" \
	"$@"
