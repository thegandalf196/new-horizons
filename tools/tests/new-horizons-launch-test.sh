#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Uses synthetic empty assets and a shell stub only. NEVER invokes a game binary.
set -euo pipefail
launcher=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)/new-horizons-launch.sh
tmp=$(mktemp -d)
cleanup_release_files=()
cleanup_done_files=()
cleanup_launcher_pids=()
cleanup() {
	local file pid attempt
	for file in "${cleanup_release_files[@]}"; do
		: > "$file"
	done
	for pid in "${cleanup_launcher_pids[@]}"; do
		if kill -0 "$pid" 2>/dev/null; then
			kill -TERM "$pid" 2>/dev/null || true
		fi
	done
	for pid in "${cleanup_launcher_pids[@]}"; do
		wait "$pid" 2>/dev/null || true
	done
	for file in "${cleanup_done_files[@]}"; do
		for ((attempt = 0; attempt < 200; ++attempt)); do
			[[ -f $file ]] && break
			sleep 0.05
		done
	done
	rm -rf -- "$tmp"
}
trap cleanup EXIT
forget_launcher() {
	local target=$1 pid
	local remaining=()
	for pid in "${cleanup_launcher_pids[@]}"; do
		[[ $pid == "$target" ]] || remaining+=("$pid")
	done
	cleanup_launcher_pids=("${remaining[@]}")
}
wait_for_file() {
	local file=$1 attempts=${2:-200} attempt
	for ((attempt = 0; attempt < attempts; ++attempt)); do
		[[ -f $file ]] && return 0
		sleep 0.05
	done
	return 1
}
engine="$tmp/engine with spaces"
assets="$tmp/purchaser's installation"
profile="$tmp/NH writable profile"
mkdir -p -- "$engine/config" "$engine/scripts/damage" "$engine/Mods/vcmi" \
	"$engine/Mods/unwanted" "$assets/dAtA" "$assets/MAPS" "$assets/mp3" \
	"$assets/Mods/unwanted" "$tmp/inherited/vcmi/Mods/unwanted"
touch -- "$engine/config/filesystem.json" "$engine/scripts/damage/damageCalculator.lua" \
	"$engine/Mods/vcmi/mod.json" "$engine/libvcmi.so" \
	"$assets/dAtA/H3BITMAP.LOD" "$assets/dAtA/h3sprite.lod"
cat > "$engine/vcmiclient" <<'STUB'
#!/usr/bin/env bash
set -euo pipefail
[[ $# -ge 1 && $1 == --nointro ]]
shift
if [[ -n ${EXPECTED_TESTMAP:-} ]]; then
	[[ $# == 3 && $1 == --testmap && $2 == "$EXPECTED_TESTMAP" && $3 == --disable-video ]]
else
	[[ $# == 0 ]]
fi
# Mimic EntryPoint's argv[0]-based chdir without running VCMI.
cd -- "$(dirname -- "$0")"
[[ -e vcmiclient && -e config && -d Mods && -e scripts ]]
[[ -e Mods/vcmi/mod.json && ! -e Mods/unwanted && ! -e Mods/roe-demo ]]
if [[ ${EXPECTED_COMMANDS:-0} == 1 ]]; then
	[[ -e Mods/new-horizons/mod.json && -d Mods/new-horizons/Images ]]
	[[ $(readlink -- Mods/new-horizons) == "$EXPECTED_ENGINE/Mods/new-horizons" ]]
else
	[[ ! -e Mods/new-horizons ]]
fi
[[ -e Data/H3BITMAP.LOD && -d Maps && -d Mp3 ]]
[[ $(readlink -- config) == "$EXPECTED_ENGINE/config" ]]
[[ $(readlink -- Data) == "$EXPECTED_ASSETS/dAtA" ]]
[[ $XDG_DATA_HOME == "$EXPECTED_PROFILE/data" ]]
[[ $XDG_CONFIG_HOME == "$EXPECTED_PROFILE/config" ]]
[[ $XDG_CACHE_HOME == "$EXPECTED_PROFILE/cache" ]]
[[ $XDG_DATA_DIRS == "$PWD" ]]
[[ $LD_LIBRARY_PATH == "$EXPECTED_ENGINE" ]]
[[ -z ${LD_PRELOAD+x} && -z ${LD_AUDIT+x} ]]
[[ ! -e $XDG_DATA_HOME/vcmi/Mods ]]
[[ -d $XDG_DATA_HOME/vcmi/Saves ]]
# Mounting a root mod does not activate it in VCMI's fresh default preset.
preset="$XDG_CONFIG_HOME/vcmi/modSettings.json"
[[ -f $preset ]]
grep -q '"activePreset": "default"' "$preset"
if [[ ${EXPECTED_COMMANDS:-0} == 1 ]]; then
	grep -Fq '"mods": ["vcmi", "core", "new-horizons"]' "$preset"
else
	grep -Fq '"mods": ["vcmi", "core"]' "$preset"
	! grep -q 'new-horizons' "$preset"
fi
if [[ -n ${LIFETIME_SIGNAL:-} ]]; then
	handle_lifetime_signal() {
		printf '%s received\n' "$LIFETIME_SIGNAL" > "$LIFETIME_SIGNAL_SEEN"
		if [[ ${LIFETIME_EXIT_ON_SIGNAL:-0} == 1 ]]; then
			[[ -r Data/h3sprite.lod ]]
			printf 'child read Data after signal\n' > "$LIFETIME_READ"
			exit "${LIFETIME_EXIT:-23}"
		fi
	}
	trap handle_lifetime_signal "$LIFETIME_SIGNAL"
	printf '%s\n%s\n' "$PWD" "$$" > "$LIFETIME_READY"
	while [[ ! -e $LIFETIME_RELEASE ]]; do sleep 0.05; done
	[[ -r Data/h3sprite.lod ]]
	printf 'child read Data after signal\n' > "$LIFETIME_READ"
	exit "${LIFETIME_EXIT:-23}"
fi
printf 'stub only\n' >> "$STUB_RECEIPT"
printf 'save placeholder\n' > "$XDG_DATA_HOME/vcmi/Saves/stub-save"
exit "${STUB_EXIT:-0}"
STUB
chmod +x -- "$engine/vcmiclient"
# A packaged entry point must not inherit the developer launcher's build path.
cp -- "$launcher" "$engine/new-horizons-launch.sh"
cp -- "$(dirname -- "$launcher")/Play-New-Horizons.sh" "$engine/Play-New-Horizons.sh"
bash "$engine/Play-New-Horizons.sh" --assets "$assets" --profile "$tmp/packaged-profile" --verify-only
[[ ! -e "$tmp/packaged-profile" ]]
export EXPECTED_ENGINE="$engine" EXPECTED_ASSETS="$assets" EXPECTED_PROFILE="$profile"
export STUB_RECEIPT="$tmp/stub-receipt"
export XDG_DATA_HOME="$tmp/inherited" XDG_CONFIG_HOME="$tmp/inherited" XDG_CACHE_HOME="$tmp/inherited"
export XDG_DATA_DIRS="$tmp/inherited"
args=(--client "$engine/vcmiclient" --assets "$assets" --profile "$profile")
expect_fail() {
	if bash "$launcher" "$@" > "$tmp/output" 2>&1; then
		printf 'Expected rejection: %s\n' "$*" >&2; exit 1
	fi
}
exercise_signal_lifecycle() {
	local signal=$1 exitOnSignal=${2:-0} expectedExit=${3:-23} label ready release seen readFile launchPid
	local childRuntime childPid status
	label=${signal,,}
	[[ $exitOnSignal == 1 ]] && label+="-immediate-$expectedExit"
	ready="$tmp/$label.ready"
	release="$tmp/$label.release"
	seen="$tmp/$label.signal-seen"
	readFile="$tmp/$label.read"
	LIFETIME_SIGNAL=$signal LIFETIME_EXIT_ON_SIGNAL=$exitOnSignal LIFETIME_EXIT=$expectedExit \
		LIFETIME_READY=$ready LIFETIME_RELEASE=$release \
		LIFETIME_SIGNAL_SEEN=$seen LIFETIME_READ=$readFile \
		env --default-signal=INT bash "$launcher" "${args[@]}" \
		> "$tmp/$label.launch-output" 2>&1 &
	launchPid=$!
	cleanup_launcher_pids+=("$launchPid")
	cleanup_release_files+=("$release")
	cleanup_done_files+=("$readFile")
	wait_for_file "$ready" || { printf 'Timed out waiting for %s client.\n' "$signal" >&2; return 1; }
	mapfile -t readyValues < "$ready"
	childRuntime=${readyValues[0]}
	childPid=${readyValues[1]}
	[[ -n $childRuntime && $childPid =~ ^[0-9]+$ ]]
	[[ -L $childRuntime/Data && -r $childRuntime/Data/h3sprite.lod ]]
	kill -s "$signal" "$launchPid"
	wait_for_file "$seen" || { printf 'Client did not receive %s.\n' "$signal" >&2; return 1; }
	[[ $(< "$seen") == "$signal received" ]]
	if [[ $exitOnSignal == 1 ]]; then
		if wait "$launchPid"; then status=0; else status=$?; fi
		forget_launcher "$launchPid"
		[[ $status == "$expectedExit" ]]
		[[ $(< "$readFile") == 'child read Data after signal' ]]
		[[ ! -e $childRuntime ]]
		return
	fi
	kill -0 "$launchPid"
	kill -0 "$childPid"
	[[ -L $childRuntime/Data && -r $childRuntime/Data/h3sprite.lod ]]
	[[ -f $profile/.nh-lock ]]
	if bash "$launcher" "${args[@]}" --verify-only > "$tmp/$label.lock-output" 2>&1; then
		printf '%s signal released the profile lock before the client exited.\n' "$signal" >&2
		return 1
	fi
	grep -q 'This NH profile is already in use.' "$tmp/$label.lock-output"
	: > "$release"
	if wait "$launchPid"; then status=0; else status=$?; fi
	forget_launcher "$launchPid"
	[[ $status == 23 ]]
	[[ $(< "$readFile") == 'child read Data after signal' ]]
	[[ ! -e $childRuntime ]]
}
bash "$launcher" "${args[@]}" --verify-only > "$tmp/output"
[[ ! -e $profile && ! -e $STUB_RECEIPT ]]
expect_fail --verify-only
expect_fail "${args[@]}" --bogus
expect_fail "${args[@]}" --assets
expect_fail "${args[@]}" --profile "$assets/nested" --verify-only
mkdir -- "$profile"
expect_fail "${args[@]}" --verify-only
rmdir -- "$profile"
mv -- "$assets/dAtA/h3sprite.lod" "$tmp/sprite"
expect_fail "${args[@]}" --verify-only
mv -- "$tmp/sprite" "$assets/dAtA/h3sprite.lod"
mkdir -- "$assets/Data"
expect_fail "${args[@]}" --verify-only
rmdir -- "$assets/Data"
mv -- "$engine/Mods/vcmi/mod.json" "$tmp/mod.json"
expect_fail "${args[@]}" --verify-only
mv -- "$tmp/mod.json" "$engine/Mods/vcmi/mod.json"
expect_fail "${args[@]}" --resources "$tmp/missing resources" --verify-only
mv -- "$engine/libvcmi.so" "$tmp/library"
expect_fail "${args[@]}" --verify-only
mv -- "$tmp/library" "$engine/libvcmi.so"
# Re-copying unchanged data after the binary must not be mistaken for a schema
# mismatch. Compatibility is carried by schema/ruleset versions, not mtimes.
touch -- "$engine/config/newHorizonsCapabilities.json"
bash "$launcher" "${args[@]}" --verify-only > "$tmp/output"
# Exercise the launch plumbing exclusively with the synthetic shell stub above.
bash "$launcher" "${args[@]}" > "$tmp/output"
[[ $(wc -l < "$STUB_RECEIPT") == 1 ]]
[[ -e $profile/data/vcmi/Saves/stub-save ]]
[[ -z $(find "$profile" -maxdepth 1 -name 'runtime.*' -print) ]]
bash "$launcher" "${args[@]}" --verify-only > "$tmp/output"
[[ $(wc -l < "$STUB_RECEIPT") == 1 ]]
bash "$launcher" "${args[@]}" > "$tmp/output"
[[ $(wc -l < "$STUB_RECEIPT") == 2 ]]
status=0
expectedMap="$tmp/Smoke Map.h3m"
EXPECTED_TESTMAP="$expectedMap" STUB_EXIT=17 bash "$launcher" "${args[@]}" --resources "$engine" -- --testmap "$expectedMap" --disable-video > "$tmp/output" || status=$?
[[ $status == 17 ]]
[[ -z $(find "$profile" -maxdepth 1 -name 'runtime.*' -print) ]]
# New candidates must include their curated module, but no arbitrary extra mods.
touch -- "$engine/config/newHorizonsCombat.json"
expect_fail "${args[@]}" --verify-only
mkdir -p -- "$engine/Mods/new-horizons"
touch -- "$engine/Mods/new-horizons/mod.json"
expect_fail "${args[@]}" --verify-only
mkdir -- "$engine/Mods/new-horizons/Images"
mv -- "$engine/config/newHorizonsCombat.json" "$tmp/commands.json"
expect_fail "${args[@]}" --verify-only
mv -- "$tmp/commands.json" "$engine/config/newHorizonsCombat.json"
export EXPECTED_COMMANDS=1
printf '{"activePreset":"unwanted","presets":{"unwanted":{"mods":["unwanted"]}}}\n' > "$profile/config/vcmi/modSettings.json"
printf 'settings sentinel\n' > "$profile/config/vcmi/settings.json"
printf 'legacy save sentinel\n' > "$profile/data/vcmi/Saves/existing-save"
presetBefore=$(cksum < "$profile/config/vcmi/modSettings.json")
bash "$launcher" "${args[@]}" --verify-only > "$tmp/output"
[[ $(cksum < "$profile/config/vcmi/modSettings.json") == "$presetBefore" ]]
bash "$launcher" "${args[@]}" > "$tmp/output"
[[ $(wc -l < "$STUB_RECEIPT") == 4 ]]
[[ $(< "$profile/config/vcmi/settings.json") == 'settings sentinel' ]]
[[ $(< "$profile/data/vcmi/Saves/existing-save") == 'legacy save sentinel' ]]
! grep -q 'unwanted' "$profile/config/vcmi/modSettings.json"
for signal in TERM HUP INT; do
	exercise_signal_lifecycle "$signal"
done
exercise_signal_lifecycle TERM 1
exercise_signal_lifecycle TERM 1 127
# Simulate a hard-killed previous launch.  An exact launcher-owned runtime is
# reclaimed after locking; a lookalike with unexpected contents is still refused.
stale=$profile/runtime.A1b2C3d4
mkdir -- "$stale" "$stale/Mods"
ln -s -- "$engine/vcmiclient" "$stale/vcmiclient"
ln -s -- "$engine/libvcmi.so" "$stale/libvcmi.so"
ln -s -- "$engine/config" "$stale/config"
ln -s -- "$engine/scripts" "$stale/scripts"
ln -s -- "$engine/Mods/vcmi" "$stale/Mods/vcmi"
ln -s -- "$engine/Mods/new-horizons" "$stale/Mods/new-horizons"
ln -s -- "$assets/dAtA" "$stale/Data"
ln -s -- "$assets/MAPS" "$stale/Maps"
ln -s -- "$assets/mp3" "$stale/Mp3"
bash "$launcher" "${args[@]}" > "$tmp/output"
[[ $(wc -l < "$STUB_RECEIPT") == 5 ]]
[[ ! -e $stale ]]
# A live profile contains runtime symlinks: diagnose its lock before scanning
# those links, and do not truncate/write the lock even during verify-only.
printf 'lock sentinel\n' > "$profile/.nh-lock"
mkdir -- "$profile/runtime.synthetic"
ln -s -- "$assets/dAtA" "$profile/runtime.synthetic/Data"
exec 8< "$profile/.nh-lock"
flock -n 8
for mode in launch verify; do
	if [[ $mode == verify ]]; then
		expect_fail "${args[@]}" --verify-only
	else
		expect_fail "${args[@]}"
	fi
	grep -q 'This NH profile is already in use.' "$tmp/output"
	[[ $(< "$profile/.nh-lock") == 'lock sentinel' ]]
	[[ -L $profile/runtime.synthetic/Data ]]
	[[ $(wc -l < "$STUB_RECEIPT") == 5 ]]
done
flock -u 8
exec 8<&-
# With no live owner, the same symlink must still be rejected, not allowlisted.
expect_fail "${args[@]}" --verify-only
grep -q 'Unexpected symlink in writable profile:' "$tmp/output"
rm -- "$profile/runtime.synthetic/Data"
rmdir -- "$profile/runtime.synthetic"
bash "$launcher" "${args[@]}" --verify-only > "$tmp/output"
[[ $(< "$profile/.nh-lock") == 'lock sentinel' ]]
# Never follow a symlink when opening the lock for the early check.
mv -- "$profile/.nh-lock" "$profile/lock-original"
ln -s -- "$profile/lock-original" "$profile/.nh-lock"
expect_fail "${args[@]}" --verify-only
grep -q 'Unexpected non-regular or symlink profile lock.' "$tmp/output"
[[ $(< "$profile/lock-original") == 'lock sentinel' ]]
rm -- "$profile/.nh-lock"
mv -- "$profile/lock-original" "$profile/.nh-lock"
mkdir -- "$profile/data/vcmi/Mods"
expect_fail "${args[@]}" --verify-only
rmdir -- "$profile/data/vcmi/Mods"
mv -- "$profile/config" "$profile/config-real"
ln -s -- "$profile/config-real" "$profile/config"
expect_fail "${args[@]}" --verify-only
[[ ! -e $assets/Saves && ! -e $engine/Saves ]]
[[ -e $assets/Mods/unwanted && -e $engine/Mods/unwanted ]]
printf '%s\n' 'PASS: verify-only, rejected inputs, isolated stub launch, signal lifecycle/status, reuse, lock ordering, cleanup; no game executed.'
