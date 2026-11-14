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
export NH_PLAYABLE_STORE="$store"
bin="$repo/build/new-horizons-linux/bin"
sourceTree="$repo/editable-resources"
mkdir -p -- "$repo/tools/ci" "$repo/tools" "$bin" "$repo/build/linux-current-client1/stage/New-Horizons-Linux-x64" \
	"$sourceTree/config" "$sourceTree/scripts/damage" "$sourceTree/Mods/vcmi" \
	"$sourceTree/Mods/new-horizons/Images" "$temporary/original/Data" "$temporary/original/Maps" "$temporary/original/Mp3"
cp -- "$repoRoot/play-new-horizons-linux.sh" "$repo/play-new-horizons-linux.sh"
cp -- "$repoRoot/tools/ci/linux_playable_snapshot.py" "$repo/tools/ci/linux_playable_snapshot.py"
cp -- "$repoRoot/tools/ci/linux_snapshot_binaries.py" "$repo/tools/ci/linux_snapshot_binaries.py"
cp -- "$repoRoot/tools/ci/client_features.py" "$repo/tools/ci/client_features.py"
cp -- "$repoRoot/tools/linux_assets_setup.py" "$repo/tools/linux_assets_setup.py"
nh_python=${NH_PYTHON:-$(command -v python3)}
export NH_PYTHON="$nh_python"
for archive in h3bitmap.lod h3sprite.lod h3ab_bmp.lod h3ab_spr.lod; do
 printf synthetic > "$temporary/original/Data/$archive"
done
cp -- "$repoRoot/tools/verify_new_horizons_art_install.py" "$repo/tools/verify_new_horizons_art_install.py"
cp -- "$repoRoot/tools/nhart.py" "$repo/tools/nhart.py"
cp -- "$repoRoot/tools/ci/package_new_horizons_windows.py" "$repo/tools/ci/package_new_horizons_windows.py"
cp -- "$repoRoot/tools/ci/source_export_policy.py" "$repo/tools/ci/source_export_policy.py"
cp -- "$repoRoot/tools/ci/retired_public_paths.json" "$repo/tools/ci/retired_public_paths.json"
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
cp -- "$bin/vcmiclient" "$bin/new-horizons"
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

candidate=$("$nh_python" "$repo/tools/ci/linux_playable_snapshot.py" freeze --no-promote \
	--client "$bin/new-horizons" --resources "$bin" --store "$store")
[[ -f $candidate/new-horizons && ! -e $candidate/vcmiclient && -f $candidate/libvcmi.so ]]
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

# Synthetic acceptance uses the exact supplied candidate; no game is executed.
# A successful gate selects it without a separate promotion command.
"$nh_python" "$repo/tools/ci/linux_playable_snapshot.py" validate \
	--snapshot "$candidate" --store "$store" --timeout 10 -- \
	"$nh_python" -c 'import os, sys; from pathlib import Path; candidate = Path(os.environ["NH_VALIDATION_SNAPSHOT"]); assert candidate == Path(sys.argv[1]); assert candidate.name == "snapshot-" + os.environ["NH_VALIDATION_SNAPSHOT_SHA256"]; assert (candidate / "new-horizons").is_file()' \
	"$candidate" > /dev/null

# Legacy validated candidates retain the exact previous no-argument launch
# contract until a helper-bearing candidate is promoted; no setup is written.
HOME="$temporary/home" SNAPSHOT_TEST_ARGV="$temporary/legacy-default-argv" \
    bash "$repo/play-new-horizons-linux.sh"
mapfile -d '' -t legacy_default < "$temporary/legacy-default-argv"
[[ ${legacy_default[0]} == --assets && ${legacy_default[1]} == "$temporary" ]]
[[ ${legacy_default[5]} == "$candidate/new-horizons" ]]
[[ ! -e $temporary/home/.local/share/new-horizons-play.setup ]]

expectedMap='Maps/Map with spaces.h3m'
HOME="$temporary/home" SNAPSHOT_TEST_ARGV="$temporary/argv" \
	bash "$repo/play-new-horizons-linux.sh" --noninteractive --assets "$temporary/original" --verify-only -- \
	--testmap "$expectedMap" --disable-video
mapfile -d '' -t actual < "$temporary/argv"
expected=(--assets "$temporary/original" --profile "$temporary/home/.local/share/new-horizons-play" \
    --client "$candidate/new-horizons" --resources "$candidate" --verify-only)
[[ ${#actual[@]} == ${#expected[@]} ]]
for index in "${!expected[@]}"; do
	[[ ${actual[$index]} == "${expected[$index]}" ]]
done

# Model an immutable historical snapshot; selecting it must not rename its
# executable or rewrite its manifest. This modifies only the synthetic fixture.
legacy=$("$nh_python" - "$repo/tools/ci" "$candidate" <<'PY'
import sys
from pathlib import Path
sys.path.insert(0, sys.argv[1])
from linux_playable_snapshot import make_removable, package_files, readonly_tree, write_metadata
candidate = Path(sys.argv[2])
make_removable(candidate)
(candidate / 'new-horizons').rename(candidate / 'vcmiclient')
digest = write_metadata(candidate, package_files(candidate))
legacy = candidate.parent / ('snapshot-' + digest)
candidate.rename(legacy)
readonly_tree(legacy)
print(legacy)
PY
)
"$nh_python" "$repo/tools/ci/linux_playable_snapshot.py" promote \
	--snapshot "$legacy" --store "$store" > /dev/null
legacy_manifest=$(cksum < "$legacy/SNAPSHOT.json")
HOME="$temporary/home" SNAPSHOT_TEST_ARGV="$temporary/legacy-argv" \
	bash "$repo/play-new-horizons-linux.sh" --noninteractive --assets "$temporary/original" --verify-only
mapfile -d '' -t legacy_args < "$temporary/legacy-argv"
[[ ${legacy_args[5]} == "$legacy/vcmiclient" ]]
[[ ${legacy_args[7]} == "$legacy" ]]
[[ $(cksum < "$legacy/SNAPSHOT.json") == "$legacy_manifest" ]]
[[ -f $legacy/vcmiclient && ! -e $legacy/new-horizons ]]

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
	--resources "$custom" --assets "$temporary/original" --verify-only
mapfile -d '' -t override < "$temporary/override-argv"
expected_override=(--assets "$temporary/original" --profile "$temporary/home/.local/share/new-horizons-play" \
	--client "$custom/vcmiclient" --resources "$custom" --verify-only)
[[ ${#override[@]} == ${#expected_override[@]} ]]
for index in "${!expected_override[@]}"; do
	[[ ${override[$index]} == "${expected_override[$index]}" ]]
done

# A custom resources path still obtains its required client from the validated
# selected snapshot. A custom client alone defaults resources to its directory.
HOME="$temporary/home" SNAPSHOT_TEST_ARGV="$temporary/resources-argv" \
    bash "$repo/play-new-horizons-linux.sh" --noninteractive --assets "$temporary/original" \
    --resources "$custom" --verify-only
mapfile -d '' -t resources_args < "$temporary/resources-argv"
[[ ${resources_args[5]} == "$legacy/vcmiclient" && ${resources_args[7]} == "$custom" ]]
HOME="$temporary/home" SNAPSHOT_TEST_ARGV="$temporary/client-only-argv" \
    bash "$repo/play-new-horizons-linux.sh" --assets "$temporary/original" \
    --client="$custom/vcmiclient" --verify-only
mapfile -d '' -t client_only_args < "$temporary/client-only-argv"
[[ ${client_only_args[5]} == "$custom/vcmiclient" && ${client_only_args[7]} == "$custom" ]]

printf '%s\n' 'PASS: successful validation automatically selects the frozen snapshot; runtime overrides, legacy rollback, profile and game args are preserved.'
