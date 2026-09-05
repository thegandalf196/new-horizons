#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Linux/XDG VCMI only. See docs/NH_BUILD_HANDOFF.md and NH_CONTENT_ACCEPTANCE.md.
set -euo pipefail
umask 077
fail() { printf 'New Horizons: %s\n' "$*" >&2; exit 1; }
usage() {
	printf '%s\n' 'Usage: new-horizons-launch.sh --assets DIR --profile DIR [--client FILE] [--resources DIR] [--verify-only]' \
		'Purchaser Complete installation: Data, Maps, Mp3 (case-insensitive names).' \
		'Profile must be new or previously created by this script; do not use a VCMI profile.' \
		'--verify-only checks paths without creating a profile or executing the client.' \
		'Without --verify-only this manually invoked command launches the game.'
}
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
client="$root/build/new-horizons-linux/bin/vcmiclient"
resources=''
assets=''
profile=''
verify=false
while (($#)); do
	case $1 in
		--assets|--profile|--client|--resources)
			(($# >= 2)) && [[ -n $2 ]] || fail "Missing value for $1"
			case $1 in
				--assets) assets=$2;; --profile) profile=$2;;
				--client) client=$2;; --resources) resources=$2;;
			esac
			shift 2;;
		--verify-only) verify=true; shift;;
		--help|-h) usage; exit 0;;
		*) fail "Unknown argument: $1";;
	esac
done
[[ $(uname -s) == Linux ]] || fail 'Only the native Linux/XDG build is supported.'
[[ -n $assets && -n $profile ]] || { usage >&2; exit 1; }
[[ -d $assets ]] || fail 'Asset installation directory does not exist.'
[[ -f $client && -x $client ]] || fail 'Client is not an executable file; consult NH_BUILD_HANDOFF.md.'
client=$(realpath -e -- "$client")
[[ $client != *:* ]] || fail 'Client path cannot contain the LD_LIBRARY_PATH separator (:).'
[[ -n $resources ]] || resources=$(dirname -- "$client")
[[ -d $resources ]] || fail 'Engine resource directory does not exist.'
resources=$(realpath -e -- "$resources")
assets=$(realpath -e -- "$assets")
profile=$(realpath -m -- "$profile")
# Reject overlap in either direction: never create writable files in source assets/build.
for source in "$assets" "$resources" "$(dirname -- "$client")"; do
	[[ $profile != "$source" && $profile != "$source/"* && $source != "$profile/"* ]] || fail 'Profile overlaps input assets or engine files.'
done
[[ $profile != / ]] || fail 'Root is not a profile.'
for item in config/filesystem.json Mods/vcmi/mod.json scripts/damage/damageCalculator.lua; do
	[[ -r $resources/$item ]] || fail "Missing engine resource: $item"
done
[[ -r $(dirname -- "$client")/libvcmi.so ]] || fail 'Missing matching libvcmi.so beside client.'
# Resolve only the three original directories, never the installation's Mods/config.
asset_dir() {
	local name=$1 entry base found=''
	for entry in "$assets"/*; do
		base=${entry##*/}
		if [[ ${base,,} == ${name,,} && -d $entry ]]; then
			[[ -z $found ]] || fail "Ambiguous asset directory: $name"
			found=$entry
		fi
	done
	[[ -n $found ]] || fail "Missing asset directory: $name"
	printf '%s' "$found"
}
data=$(asset_dir Data)
maps=$(asset_dir Maps)
mp3=$(asset_dir Mp3)
# File presence is not archive integrity or legal ownership verification.
for name in h3bitmap.lod h3sprite.lod; do
	found=false
	for file in "$data"/*; do
		base=${file##*/}
		[[ ${base,,} != "$name" || ! -r $file || ! -f $file ]] || found=true
	done
	$found || fail "Missing original archive: $name"
done
marker='New Horizons launcher profile v1'
if [[ -e $profile ]]; then
	[[ -d $profile && -f $profile/.nh-profile && ! -L $profile/.nh-profile ]] || fail 'Refusing an existing unmanaged profile.'
	[[ $(< "$profile/.nh-profile") == "$marker" ]] || fail 'Unrecognized profile marker.'
	# Refuse redirecting writable settings/saves outside this managed profile.
	while IFS= read -r -d '' path; do
		fail "Unexpected symlink in writable profile: $path"
	done < <(find "$profile" -type l -print0)
	# No inherited filesystem trees. Saves are the only permitted user-data entry.
	if [[ -d $profile/data/vcmi ]]; then
		while IFS= read -r -d '' path; do
			[[ ${path##*/} == Saves ]] || fail 'Unexpected user data (including optional Mods); use a fresh NH profile.'
		done < <(find "$profile/data/vcmi" -mindepth 1 -maxdepth 1 -print0)
	fi
fi
printf 'Client: %s\nEngine resources: %s\nOriginal installation: %s\nNH profile: %s\n' "$client" "$resources" "$assets" "$profile"
if $verify; then
	printf '%s\n' 'Path checks passed; no files created and no client executed. Asset completeness and gameplay remain unverified.'
	exit 0
fi
mkdir -p -- "$profile/data/vcmi/Saves" "$profile/config/vcmi" "$profile/cache"
exec 9> "$profile/.nh-lock"
flock -n 9 || fail 'This NH profile is already in use.'
printf '%s\n' "$marker" > "$profile/.nh-profile"
# Fresh allowlisted root each run. argv[0] must remain this symlink path: EntryPoint
# chdirs to its parent; VCMIDirs developmentMode then excludes ALL system roots.
runtime=$(mktemp -d -- "$profile/runtime.XXXXXXXX")
trap 'rm -rf -- "$runtime"' EXIT
mkdir -- "$runtime/Mods"
ln -s -- "$client" "$runtime/vcmiclient"
ln -s -- "$(dirname -- "$client")/libvcmi.so" "$runtime/libvcmi.so"
ln -s -- "$resources/config" "$runtime/config"
ln -s -- "$resources/scripts" "$runtime/scripts"
ln -s -- "$resources/Mods/vcmi" "$runtime/Mods/vcmi"
ln -s -- "$data" "$runtime/Data"
ln -s -- "$maps" "$runtime/Maps"
ln -s -- "$mp3" "$runtime/Mp3"
# Do not inherit XDG VCMI settings, optional mods, or loader injection variables.
# This is a curated launch profile, not a sandbox or global mod prohibition.
env -u LD_PRELOAD -u LD_AUDIT \
	LD_LIBRARY_PATH="$(dirname -- "$client")" \
	XDG_DATA_HOME="$profile/data" XDG_CONFIG_HOME="$profile/config" \
	XDG_CACHE_HOME="$profile/cache" XDG_DATA_DIRS="$runtime" \
	"$runtime/vcmiclient"
