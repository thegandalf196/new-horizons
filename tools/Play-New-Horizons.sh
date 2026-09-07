#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Linux package entry point. Purchaser assets and a separate profile are required.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
exec bash "$root/new-horizons-launch.sh" \
	--client "$root/vcmiclient" --resources "$root" "$@"
