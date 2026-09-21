#!/usr/bin/env bash

set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
launcher="$repo_root/play-new-horizons-linux.sh"
duration=${NH_SMOKE_SECONDS:-35}
map=${NH_SMOKE_MAP:-Maps/All for One.h3m}
minimum_turns=${NH_SMOKE_MINIMUM_TURNS:-10}
run_log=$(mktemp "${TMPDIR:-/tmp}/new-horizons-scenario-smoke.XXXXXX.log")

cleanup()
{
	if [[ "${NH_KEEP_SMOKE_LOG:-0}" != 1 ]]; then
		rm -f "$run_log"
	fi
}
trap cleanup EXIT

set +e
timeout --signal=TERM "${duration}s" "$launcher" -- \
	--testmap "$map" \
	--headless \
	--disable-video \
	--savefrequency 0 >"$run_log" 2>&1
status=$?
set -e

if [[ $status -ne 0 && $status -ne 124 ]]; then
	echo "FAIL: scenario client exited with status $status" >&2
	tail -n 80 "$run_log" >&2
	exit 1
fi

for pattern in "Map loaded!" "starting turn"; do
	if ! grep -Fq "$pattern" "$run_log"; then
		echo "FAIL: scenario did not reach required state: $pattern" >&2
		tail -n 80 "$run_log" >&2
		exit 1
	fi
done

for pattern in \
	"Server encountered a problem" \
	"Leadership limit exceeded" \
	"Got false in applying" \
	"FIXME: battleGetFightingHero access check" \
	"Encoding conversion failure" \
	"Encoding coversion failure" \
	"Unable to select shooter for tower" \
	"Disaster happened" \
	"Failed to launch game" \
	"Invalid New Horizons capability rules"; do
	if grep -Fq "$pattern" "$run_log"; then
		echo "FAIL: scenario emitted forbidden runtime error: $pattern" >&2
		grep -Fn -C 8 "$pattern" "$run_log" >&2 || true
		exit 1
	fi
done

turns=$(grep -Fc "starting turn" "$run_log")
if (( turns < minimum_turns )); then
	echo "FAIL: scenario advanced only $turns turns; expected at least $minimum_turns" >&2
	tail -n 80 "$run_log" >&2
	exit 1
fi
echo "PASS: loaded '$map' and observed $turns turns without forbidden runtime errors."
if [[ "${NH_KEEP_SMOKE_LOG:-0}" == 1 ]]; then
	echo "Log retained at $run_log"
fi
