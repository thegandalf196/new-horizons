#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Uses synthetic empty assets and a shell stub only. NEVER invokes a game binary.
set -euo pipefail
launcher=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)/new-horizons-launch.sh
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT
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
# Mimic EntryPoint's argv[0]-based chdir without running VCMI.
cd -- "$(dirname -- "$0")"
[[ -e vcmiclient && -e config && -d Mods && -e scripts ]]
[[ -e Mods/vcmi/mod.json && ! -e Mods/unwanted && ! -e Mods/roe-demo ]]
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
printf 'stub only\n' >> "$STUB_RECEIPT"
printf 'save placeholder\n' > "$XDG_DATA_HOME/vcmi/Saves/stub-save"
exit "${STUB_EXIT:-0}"
STUB
chmod +x -- "$engine/vcmiclient"
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
STUB_EXIT=17 bash "$launcher" "${args[@]}" --resources "$engine" > "$tmp/output" || status=$?
[[ $status == 17 ]]
[[ -z $(find "$profile" -maxdepth 1 -name 'runtime.*' -print) ]]
mkdir -- "$profile/data/vcmi/Mods"
expect_fail "${args[@]}" --verify-only
rmdir -- "$profile/data/vcmi/Mods"
mv -- "$profile/config" "$profile/config-real"
ln -s -- "$profile/config-real" "$profile/config"
expect_fail "${args[@]}" --verify-only
[[ ! -e $assets/Saves && ! -e $engine/Saves ]]
[[ -e $assets/Mods/unwanted && -e $engine/Mods/unwanted ]]
printf '%s\n' 'PASS: verify-only, rejected inputs, isolated stub launch, reuse, cleanup; no game executed.'
