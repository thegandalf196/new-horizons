#!/usr/bin/env bash
# Synthetic wrapper/snapshot integration test. Never invokes a game binary.
set -euo pipefail

repoRoot=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
temporary=$(mktemp -d)
cleanup() {
	chmod -R u+w -- "$temporary" 2>/dev/null || true
	rm -rf -- "$temporary"
}
trap cleanup EXIT
repo="$temporary/project"
store="$repo/build/new-horizons-linux/playable-snapshots"
bin="$repo/build/new-horizons-linux/bin"
sourceTree="$repo/editable-resources"
mkdir -p -- "$repo/tools/ci" "$repo/tools" "$bin" "$repo/build/linux-current-client1/stage/New-Horizons-Linux-x64" \
	"$sourceTree/config" "$sourceTree/scripts/damage" "$sourceTree/Mods/vcmi" \
	"$sourceTree/Mods/new-horizons/Images" "$temporary/Data" "$temporary/Maps" "$temporary/Mp3"
cp -- "$repoRoot/play-new-horizons-linux.sh" "$repo/play-new-horizons-linux.sh"
cp -- "$repoRoot/tools/ci/linux_playable_snapshot.py" "$repo/tools/ci/linux_playable_snapshot.py"
cp -- "$repoRoot/tools/new-horizons-launch.sh" "$repo/tools/new-horizons-launch.sh"

cat > "$repo/tools/new-horizons-launch.sh" <<'STUB'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\0' "$@" > "$SNAPSHOT_TEST_ARGV"
STUB
chmod +x -- "$repo/tools/new-horizons-launch.sh"

cat > "$bin/vcmiclient" <<'OLD'
#!/usr/bin/env bash
exit 91
OLD
chmod +x -- "$bin/vcmiclient"
printf 'old library\n' > "$bin/libvcmi.so"
cat > "$repo/build/linux-current-client1/stage/New-Horizons-Linux-x64/vcmiclient" <<'OLD_STAGE'
#!/usr/bin/env bash
exit 92
OLD_STAGE
chmod +x -- "$repo/build/linux-current-client1/stage/New-Horizons-Linux-x64/vcmiclient"
printf 'old staged library\n' > "$repo/build/linux-current-client1/stage/New-Horizons-Linux-x64/libvcmi.so"

cat > "$sourceTree/config/filesystem.json" <<'JSON'
{"source":"editable"}
JSON
cat > "$sourceTree/config/newHorizonsCombat.json" <<'JSON'
{}
JSON
cat > "$sourceTree/config/newHorizonsMagic.json" <<'JSON'
{}
JSON
printf 'return {}\n' > "$sourceTree/scripts/damage/damageCalculator.lua"
printf '{}\n' > "$sourceTree/Mods/vcmi/mod.json"
printf '{}\n' > "$sourceTree/Mods/new-horizons/mod.json"
printf 'synthetic art\n' > "$sourceTree/Mods/new-horizons/Images/test.png"
ln -s -- "$sourceTree/config" "$bin/config"
ln -s -- "$sourceTree/scripts" "$bin/scripts"
ln -s -- "$sourceTree/Mods" "$bin/Mods"

expectedFailure="$temporary/no-snapshot.log"
if HOME="$temporary/home" SNAPSHOT_TEST_ARGV="$temporary/argv" \
	bash "$repo/play-new-horizons-linux.sh" --verify-only > "$expectedFailure" 2>&1; then
	printf '%s\n' 'Default play unexpectedly used a live build or stale release stage.' >&2
	exit 1
fi
grep -q 'No frozen Linux playable snapshot is selected' "$expectedFailure"
[[ ! -e $temporary/argv ]]

candidate=$(python3 "$repo/tools/ci/linux_playable_snapshot.py" freeze --no-promote \
	--client "$bin/vcmiclient" --resources "$bin" --store "$store")
[[ -f $candidate/vcmiclient && -f $candidate/libvcmi.so ]]
[[ $(< "$candidate/config/filesystem.json") == '{"source":"editable"}' ]]

# A frozen but unvalidated candidate must remain opt-in until promotion.
if HOME="$temporary/home" SNAPSHOT_TEST_ARGV="$temporary/unpromoted-argv" \
	bash "$repo/play-new-horizons-linux.sh" --verify-only > "$expectedFailure" 2>&1; then
	printf '%s\n' 'Unpromoted candidate was selected by default.' >&2
	exit 1
fi
grep -q 'No frozen Linux playable snapshot is selected' "$expectedFailure"
[[ ! -e $temporary/unpromoted-argv ]]

# The default command keeps using the frozen bytes after source files change.
cat > "$sourceTree/config/filesystem.json" <<'JSON'
{"source":"changed while editing"}
JSON
[[ $(< "$candidate/config/filesystem.json") == '{"source":"editable"}' ]]

python3 "$repo/tools/ci/linux_playable_snapshot.py" promote \
	--snapshot "$candidate" --store "$store" > /dev/null

expectedMap='Maps/Map with spaces.h3m'
HOME="$temporary/home" SNAPSHOT_TEST_ARGV="$temporary/argv" \
	bash "$repo/play-new-horizons-linux.sh" --verify-only -- \
	--testmap "$expectedMap" --disable-video
mapfile -d '' -t actual < "$temporary/argv"
expected=(--assets "$temporary" --profile "$temporary/home/.local/share/new-horizons-play" \
	--client "$candidate/vcmiclient" --resources "$candidate" --verify-only -- \
	--testmap "$expectedMap" --disable-video)
[[ ${#actual[@]} == ${#expected[@]} ]]
for index in "${!expected[@]}"; do
	[[ ${actual[$index]} == "${expected[$index]}" ]]
done

# Explicit runtime overrides still bypass snapshot selection and keep their args.
custom="$temporary/custom-engine"
mkdir -p -- "$custom/config" "$custom/scripts/damage" "$custom/Mods/vcmi"
printf '{}\n' > "$custom/config/filesystem.json"
printf '{}\n' > "$custom/Mods/vcmi/mod.json"
printf 'return {}\n' > "$custom/scripts/damage/damageCalculator.lua"
printf 'custom library\n' > "$custom/libvcmi.so"
cat > "$custom/vcmiclient" <<'CUSTOM'
#!/usr/bin/env bash
exit 0
CUSTOM
chmod +x -- "$custom/vcmiclient"
HOME="$temporary/home" SNAPSHOT_TEST_ARGV="$temporary/override-argv" \
	bash "$repo/play-new-horizons-linux.sh" --client "$custom/vcmiclient" \
	--resources "$custom" --verify-only
mapfile -d '' -t override < "$temporary/override-argv"
expected_override=(--assets "$temporary" --profile "$temporary/home/.local/share/new-horizons-play" \
	--client "$custom/vcmiclient" --resources "$custom" --verify-only)
[[ ${#override[@]} == ${#expected_override[@]} ]]
for index in "${!expected_override[@]}"; do
	[[ ${override[$index]} == "${expected_override[$index]}" ]]
done

printf '%s\n' 'PASS: default requires an explicitly promoted frozen snapshot; frozen resources, runtime overrides, profile and game args are preserved.'
