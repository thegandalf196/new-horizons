#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Linux/XDG VCMI only. See docs/NH_DELIVERY_PIPELINE.md and NH_CONTENT_ACCEPTANCE.md.
set -euo pipefail
umask 077
fail() { printf 'New Horizons: %s\n' "$*" >&2; exit 1; }
usage() {
	printf '%s\n' 'Usage: new-horizons-launch.sh --assets DIR --profile DIR [--client FILE] [--resources DIR] [--debugger-log FILE] [--verify-only] [-- CLIENT_ARG ...]' \
		'Purchaser Complete installation: Data, Maps, Mp3 (case-insensitive names).' \
		'Profile must be new or previously created by this script; do not use a VCMI profile.' \
		'Arguments after -- are forwarded verbatim to New Horizons after the mandatory --nointro.' \
		'--verify-only checks paths without creating a profile or executing the client.' \
		'--debugger-log uses installed gdb and a new exclusive log inside the managed profile.' \
		'Without --verify-only this manually invoked command launches the game.'
}
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
client=''
resources=''
assets=''
profile=''
verify=false
debugger_log=''
client_args=()
while (($#)); do
	case $1 in
		--assets|--profile|--client|--resources|--debugger-log)
			(($# >= 2)) && [[ -n $2 ]] || fail "Missing value for $1"
			case $1 in
				--assets) assets=$2;; --profile) profile=$2;;
				--client) client=$2;; --resources) resources=$2;;
				--debugger-log) [[ -z $debugger_log ]] || fail 'Duplicate --debugger-log'; debugger_log=$2;;
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
if [[ -z $client ]]; then
	store=${NH_PLAYABLE_STORE:-${XDG_DATA_HOME:-$HOME/.local/share}/new-horizons/playable-snapshots}
	snapshot=$(python3 "$root/tools/ci/linux_playable_snapshot.py" resolve --store "$store")
	client=$(python3 "$root/tools/ci/linux_playable_snapshot.py" client --snapshot "$snapshot")
	[[ -n $resources ]] || resources=$snapshot
fi
[[ -d $assets ]] || fail 'Asset installation directory does not exist.'
[[ -f $client && -x $client ]] || fail 'Client is not an executable file; consult NH_DELIVERY_PIPELINE.md.'
client=$(realpath -e -- "$client")
[[ $client != *:* ]] || fail 'Client path cannot contain the LD_LIBRARY_PATH separator (:).'
[[ -n $resources ]] || resources=$(dirname -- "$client")
[[ -d $resources ]] || fail 'Engine resource directory does not exist.'
resources=$(realpath -e -- "$resources")
assets=$(realpath -e -- "$assets")
profile=$(realpath -m -- "$profile")
if [[ -n $debugger_log ]]; then
	debugger_log=$(realpath -m -- "$debugger_log")
	[[ $debugger_log == "$profile/"* ]] || fail 'Debugger log must be inside the managed profile.'
	[[ ! -e $debugger_log && ! -L $debugger_log ]] || fail 'Debugger log must be a new exclusive path.'
	debugger_parent=$(dirname -- "$debugger_log")
	[[ $debugger_parent == "$profile" || -d $debugger_parent ]] || fail 'Debugger log parent must exist or be the profile root.'
	debugger=$(command -v gdb) || fail 'Debugger mode requires installed gdb.'
fi
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
	if grep -q '"nhart"' "$resources/config/filesystem.json" "$resources/Mods/new-horizons/mod.json"; then
		art_pack="$resources/Mods/new-horizons/NewHorizons.nhart"
		[[ -f $art_pack && ! -L $art_pack && -r $art_pack ]] || fail 'Missing curated NewHorizons.nhart artwork package.'
		[[ $(head -c 5 -- "$art_pack") == NHART ]] || fail 'Invalid curated NHART artwork package header.'
	else
		# Historical frozen previews predate NHART; retain their explicit legacy path.
		[[ -d $resources/Mods/new-horizons/Images ]] || fail 'Missing legacy curated command artwork directory.'
	fi
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
			for entry in "$staleRuntime"/* "$staleRuntime"/.[!.]* "$staleRuntime"/..?*; do
				[[ -e $entry || -L $entry ]] || continue
				case ${entry##*/} in
					vcmiclient|new-horizons) expected=$client;;
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
			if [[ -L $staleRuntime/new-horizons && ! -e $staleRuntime/vcmiclient && ! -L $staleRuntime/vcmiclient ]] \
				|| [[ -L $staleRuntime/vcmiclient && ! -e $staleRuntime/new-horizons && ! -L $staleRuntime/new-horizons ]]; then
				:
			else
				managedRuntime=false
			fi
			for name in libvcmi.so config scripts Data Maps Mp3 Mods; do
				[[ -e $staleRuntime/$name || -L $staleRuntime/$name ]] || managedRuntime=false
			done
			if $managedRuntime; then
				for entry in "$staleRuntime/Mods"/* "$staleRuntime/Mods"/.[!.]* "$staleRuntime/Mods"/..?*; do
					[[ -e $entry || -L $entry ]] || continue
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
	# No inherited filesystem trees. Only saves and locally generated maps are
	# permitted; neither may redirect writes into the purchaser's installation.
	if [[ -d $profile/data/vcmi ]]; then
		while IFS= read -r -d '' path; do
			[[ (${path##*/} == Saves || ${path##*/} == Maps) && -d $path && ! -L $path ]] \
				|| fail 'Unexpected user data (including optional Mods); use a fresh NH profile.'
		done < <(find "$profile/data/vcmi" -mindepth 1 -maxdepth 1 -print0)
	fi
	if [[ -e $profile/data/new-horizons && ! -d $profile/data/new-horizons ]]; then
		fail 'New Horizons save root must be a directory.'
	fi
	if [[ -d $profile/data/new-horizons ]]; then
		while IFS= read -r -d '' path; do
			[[ ${path##*/} == Saves && -d $path && ! -L $path ]] \
				|| fail 'Unexpected New Horizons save data; use a fresh NH profile.'
		done < <(find "$profile/data/new-horizons" -mindepth 1 -maxdepth 1 -print0)
	fi
fi
printf 'Client: %s\nEngine resources: %s\nOriginal installation: %s\nNH profile: %s\n' "$client" "$resources" "$assets" "$profile"
if $verify; then
	printf '%s\n' 'Path checks passed; no files created and no client executed. Asset completeness and gameplay remain unverified.'
	exit 0
fi
mkdir -p -- "$profile/data/new-horizons/Saves" "$profile/config/vcmi" "$profile/cache"
if ! $lockHeld; then
	exec 9> "$profile/.nh-lock"
	flock -n 9 || fail 'This NH profile is already in use.'
fi
printf '%s\n' "$marker" > "$profile/.nh-profile"
if [[ -n $debugger_log ]]; then
	# noclobber also refuses an existing path created since read-only preflight.
	set -C
	exec 8> "$debugger_log" || fail 'Cannot create exclusive debugger log.'
	set +C
fi
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
ln -s -- "$client" "$runtime/new-horizons"
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
# GTK/GdkPixbuf needs system MIME and icon data for native dialogs. Engine data
# isolation is provided by EntryPoint's runtime cwd and VCMIDirs development mode,
# not by hiding desktop resources through XDG_DATA_DIRS. Do not inherit custom roots.
# A retained report must not send a new option to a historical rollback client.
# This bounded build declaration is optional and tied to the exact executable.
supports_bug_report() {
	local directory="$(dirname -- "$1")" advertised actual expected
	[[ $2 == "$directory" ]] || return 1
	local marker="$directory/new-horizons.features"
	[[ -f $marker && ! -L $marker ]] || return 1
	advertised=''
	IFS= read -r -N 128 advertised < "$marker" || true
	actual=$(sha256sum -- "$1") || return 1
	actual=${actual%% *}
	expected=$'NH_CLIENT_FEATURES_V1\nBUG_REPORT_V1_SHA256='"$actual"$'\n'
	[[ $actual =~ ^[0-9a-f]{64}$ && $(stat -c %s -- "$marker") == ${#expected} && $advertised == "$expected" ]]
}
reporter_supported=false
if supports_bug_report "$client" "$resources"; then reporter_supported=true; fi
# This exact launch owns a unique evidence path; reporter never guesses latest.
crash_token=$(od -An -N16 -tx1 /dev/urandom | tr -d ' \n')
crash_session="$profile/cache/vcmi/crash-reports/run-$crash_token"
# An explicit reporter request owns its single process. Do not prepend a
# fallback form or supervise that form as a new game crash.
report_only_requested=false
for client_arg in "${client_args[@]}"; do
	option_name=${client_arg%%=*}
	option_name=${option_name,,}
	# Match client case-insensitive long-option prefixes without consuming any
	# values. Bare -- is a separator, never a reporter request.
	if [[ $option_name == --?* && '--report-bug' == "$option_name"* ]]; then
		report_only_requested=true
		break
	fi
done
launch_command=("$runtime/new-horizons" --nointro "${client_args[@]}")
if [[ -n $debugger_log ]]; then
	# Do not source host/profile .gdbinit. Preserve inferior exit status; a
	# signal-stopped crash returns nonzero instead of gdb's usual batch success.
	launch_command=("$debugger" --nx --batch --return-child-result
		-ex run -ex 'thread apply all bt' --args "${launch_command[@]}")
fi
launch_managed_client() {
	exec env --default-signal=INT -u LD_PRELOAD -u LD_AUDIT \
	LD_LIBRARY_PATH="$(dirname -- "$client")" NH_CRASH_SESSION_DIR="$crash_session" \
	XDG_DATA_HOME="$profile/data" XDG_CONFIG_HOME="$profile/config" \
	XDG_CACHE_HOME="$profile/cache" XDG_DATA_DIRS=/usr/local/share:/usr/share \
	"${launch_command[@]}"
}
# Next-launch fallback offers one retained unclean session before starting play.
# Its exact directory remains fixed throughout this reporter invocation.
if $reporter_supported && ! $report_only_requested && [[ -z $debugger_log ]]; then
	for pending_marker in "$profile/cache/vcmi/crash-reports"/run-*/pending; do
		[[ -f $pending_marker && ! -L $pending_marker ]] || continue
		saved_launch_command=("${launch_command[@]}")
		launch_command=("$runtime/new-horizons" --nointro --report-bug pending)
		launch_managed_client &
		client_pid=$!
		while true; do
			signal_count_before_wait=$received_signal_count
			wait "$client_pid" && reporter_status=0 || reporter_status=$?
			(( received_signal_count == signal_count_before_wait )) && break
		done
		client_pid=''
		launch_command=("${saved_launch_command[@]}")
		break
	done
fi
if [[ -n $received_signal ]]; then
	case $received_signal in HUP) exit 129;; INT) exit 130;; TERM) exit 143;; esac
fi
if [[ -n $debugger_log ]]; then
	launch_managed_client >&8 2>&1 &
else
	launch_managed_client &
fi
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
# Preserve diagnostics, original status, runtime and lock through reporter exit.
# Cancellation/kill is not classified as a crash, and debugger mode retains its
# existing semantics. A report process is never recursively supervised here.
if $reporter_supported && ! $report_only_requested && [[ -z $received_signal && -z $debugger_log && -f $crash_session/pending ]]; then
	case $client_status in
		132|134|135|136|139|159)
			launch_command=("$runtime/new-horizons" --nointro --report-bug "$crash_session")
			launch_managed_client &
			client_pid=$!
			while true; do
				signal_count_before_wait=$received_signal_count
				wait "$client_pid" && reporter_status=0 || reporter_status=$?
				(( received_signal_count == signal_count_before_wait )) && break
			done
			client_pid=''
			;;
	esac
fi
exit "$client_status"
