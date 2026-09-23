#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Linux/XDG VCMI only. See docs/NH_BUILD_HANDOFF.md and NH_CONTENT_ACCEPTANCE.md.
set -euo pipefail
umask 077
fail() { printf 'New Horizons: %s\n' "$*" >&2; exit 1; }
usage() {
	printf '%s\n' 'Usage: new-horizons-launch.sh --assets DIR --profile DIR [--client FILE] [--resources DIR] [--verify-only] [-- CLIENT_ARG ...]' \
		'Purchaser Complete installation: Data, Maps, Mp3 (case-insensitive names).' \
		'Profile must be new or previously created by this script; do not use a VCMI profile.' \
		'Arguments after -- are forwarded verbatim to vcmiclient after the mandatory --nointro.' \
		'--verify-only checks paths without creating a profile or executing the client.' \
		'Without --verify-only this manually invoked command launches the game.'
}
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
client="$root/build/new-horizons-linux/bin/vcmiclient"
resources=''
assets=''
profile=''
verify=false
client_args=()
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
		--)
			shift
			client_args+=("$@")
			break;;
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
# Old frozen previews remain usable. New command candidates must carry the whole
# curated module; never silently launch them without their rules or artwork.
curatedCommands=false
if [[ -e $resources/config/newHorizonsCombat.json || -e $resources/Mods/new-horizons ]]; then
	for item in config/newHorizonsCombat.json Mods/new-horizons/mod.json; do
		[[ -f $resources/$item && -r $resources/$item ]] || fail "Missing curated resource: $item"
	done
	[[ -d $resources/Mods/new-horizons/Images ]] || fail 'Missing curated command artwork directory.'
	curatedCommands=true
fi
[[ -r $(dirname -- "$client")/libvcmi.so ]] || fail 'Missing matching libvcmi.so beside client.'
# Capability snapshots are validated by libvcmi against their explicit schema
# and ruleset versions. File mtimes are deliberately not used as compatibility
# metadata: packaging or copying identical JSON after a build must not make a
# valid client permanently unlaunchable.
# Resolve only the three original directories, never the installation's Mods/config.
asset_dir() {
	local name=$1 entry base found=''
	# A source tree may sit beside VCMI's lowercase writable `data` directory.
	# Prefer the canonical Complete-installation spelling before falling back to
	# the case-insensitive search needed for installations copied from Windows.
	if [[ -d $assets/$name ]]; then
		printf '%s' "$assets/$name"
		return
	fi
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
lockHeld=false
if [[ -e $profile ]]; then
	[[ -d $profile && -f $profile/.nh-profile && ! -L $profile/.nh-profile ]] || fail 'Refusing an existing unmanaged profile.'
	[[ $(< "$profile/.nh-profile") == "$marker" ]] || fail 'Unrecognized profile marker.'
	# A live run has intentional runtime symlinks. Report its lock first, without
	# creating/truncating anything in read-only preflight. Keep the lock through
	# validation and launch; never follow a redirected lock file.
	if [[ -e $profile/.nh-lock || -L $profile/.nh-lock ]]; then
		[[ -f $profile/.nh-lock && ! -L $profile/.nh-lock ]] || fail 'Unexpected non-regular or symlink profile lock.'
		exec 9< "$profile/.nh-lock"
		flock -n 9 || fail 'This NH profile is already in use.'
		lockHeld=true
	fi
	# A power loss or SIGKILL cannot run the EXIT trap and may leave one of this
	# launcher's temporary runtime trees behind.  After taking the profile lock,
	# remove only an exact runtime layout whose links still resolve to the inputs
	# selected above.  Anything else remains subject to the generic symlink
	# rejection below.  Read-only verification never mutates the profile.
	if ! $verify; then
		for staleRuntime in "$profile"/runtime.????????; do
			[[ -d $staleRuntime && ! -L $staleRuntime ]] || continue
			managedRuntime=true
			for entry in "$staleRuntime"/*; do
				case ${entry##*/} in
					vcmiclient) expected=$client;;
					libvcmi.so) expected=$(dirname -- "$client")/libvcmi.so;;
					config) expected=$resources/config;;
					scripts) expected=$resources/scripts;;
					Data) expected=$data;;
					Maps) expected=$maps;;
					Mp3) expected=$mp3;;
					Mods)
						[[ -d $entry && ! -L $entry ]] || { managedRuntime=false; break; }
						continue;;
					*) managedRuntime=false; break;;
				esac
				[[ -L $entry && $(realpath -e -- "$entry") == $(realpath -e -- "$expected") ]] \
					|| { managedRuntime=false; break; }
			done
			for name in vcmiclient libvcmi.so config scripts Data Maps Mp3 Mods; do
				[[ -e $staleRuntime/$name || -L $staleRuntime/$name ]] || managedRuntime=false
			done
			if $managedRuntime; then
				for entry in "$staleRuntime/Mods"/*; do
					case ${entry##*/} in
						vcmi) expected=$resources/Mods/vcmi;;
						new-horizons)
							$curatedCommands || { managedRuntime=false; break; }
							expected=$resources/Mods/new-horizons;;
						*) managedRuntime=false; break;;
					esac
					[[ -L $entry && $(realpath -e -- "$entry") == $(realpath -e -- "$expected") ]] \
						|| { managedRuntime=false; break; }
				done
				[[ -L $staleRuntime/Mods/vcmi ]] || managedRuntime=false
				$curatedCommands && [[ -L $staleRuntime/Mods/new-horizons ]] || ! $curatedCommands \
					|| managedRuntime=false
			fi
			$managedRuntime && rm -r -- "$staleRuntime"
		done
	fi
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
if ! $lockHeld; then
	exec 9> "$profile/.nh-lock"
	flock -n 9 || fail 'This NH profile is already in use.'
fi
printf '%s\n' "$marker" > "$profile/.nh-profile"
# Root mods are not auto-enabled merely by mounting them. This managed profile
# owns its fixed preset; discard cached validation/optional preset selections,
# not game settings or saves. Saved games retain their own versioned rules.
mods='["vcmi", "core"]'
if $curatedCommands; then
	mods='["vcmi", "core", "new-horizons"]'
fi
preset=$(mktemp -- "$profile/config/vcmi/.nh-modSettings.XXXXXXXX")
printf '{\n  "activePreset": "default",\n  "presets": {"default": {"mods": %s, "settings": {}}}\n}\n' "$mods" > "$preset"
mv -fT -- "$preset" "$profile/config/vcmi/modSettings.json"
# Fresh allowlisted root each run. argv[0] must remain this symlink path: EntryPoint
# chdirs to its parent; VCMIDirs developmentMode then excludes ALL system roots.
runtime=$(mktemp -d -- "$profile/runtime.XXXXXXXX")
trap 'rm -rf -- "$runtime"' EXIT
# The client uses this directory as its working directory, so do not remove it
# until the child has actually exited. Bash traps interrupt `wait`; retrying the
# wait keeps the runtime and profile lock alive for TERM-delayed clients.
client_pid=''
received_signal=''
received_signal_count=0
forward_signal() {
	local signal=$1
	received_signal=$signal
	received_signal_count=$((received_signal_count + 1))
	if [[ -n ${client_pid:-} ]] && kill -0 "$client_pid" 2>/dev/null; then
		kill -s "$signal" "$client_pid" 2>/dev/null || true
	fi
	return 0
}
trap 'forward_signal HUP' HUP
trap 'forward_signal INT' INT
trap 'forward_signal TERM' TERM
mkdir -- "$runtime/Mods"
ln -s -- "$client" "$runtime/vcmiclient"
ln -s -- "$(dirname -- "$client")/libvcmi.so" "$runtime/libvcmi.so"
ln -s -- "$resources/config" "$runtime/config"
ln -s -- "$resources/scripts" "$runtime/scripts"
ln -s -- "$resources/Mods/vcmi" "$runtime/Mods/vcmi"
if $curatedCommands; then
	ln -s -- "$resources/Mods/new-horizons" "$runtime/Mods/new-horizons"
fi
ln -s -- "$data" "$runtime/Data"
ln -s -- "$maps" "$runtime/Maps"
ln -s -- "$mp3" "$runtime/Mp3"
# A signal received while assembling the runtime still prevents client startup.
if [[ -n $received_signal ]]; then
	case $received_signal in
		HUP) exit 129;;
		INT) exit 130;;
		TERM) exit 143;;
	esac
fi
# Do not inherit XDG VCMI settings, optional mods, or loader injection variables.
# This is a curated launch profile, not a sandbox or global mod prohibition.
env --default-signal=INT -u LD_PRELOAD -u LD_AUDIT \
	LD_LIBRARY_PATH="$(dirname -- "$client")" \
	XDG_DATA_HOME="$profile/data" XDG_CONFIG_HOME="$profile/config" \
	XDG_CACHE_HOME="$profile/cache" XDG_DATA_DIRS="$runtime" \
	"$runtime/vcmiclient" --nointro "${client_args[@]}" &
client_pid=$!
# Cover a signal that arrived between the pre-start check and PID assignment.
if [[ -n $received_signal ]] && kill -0 "$client_pid" 2>/dev/null; then
	kill -s "$received_signal" "$client_pid" 2>/dev/null || true
fi

# Preserve the child's status and keep the runtime and profile lock until the
# client exits. FD 9 intentionally remains inherited by the client so a
# forcibly killed launcher cannot release the profile lock while that child is
# still alive.
client_status=0
while true; do
	signal_count_before_wait=$received_signal_count
	if wait "$client_pid"; then
		client_status=0
	else
		client_status=$?
	fi
	# Bash wait returns 128+signal when a trapped signal interrupts it, even if
	# the client exits while the trap handler is forwarding that signal. Re-wait
	# after any handled signal to retrieve the child's cached exit status.
	if (( received_signal_count == signal_count_before_wait )); then
		break
	fi
done
client_pid=''
exit "$client_status"
