# Local Linux playable snapshots

## Isolated Cabir / Wisp art preview

The separate preview described here is historical, with unselected prototype
art/gameplay. It is not the current normal-game delivery route. Selected Cabirs,
Wisps, turbaned Magi, Guild composites and other runtime visuals now ship in
`Mods/new-horizons/NewHorizons.nhart`; do not apply this prototype's private-only
exclusion to those selected assets. Unchanged original installation data remains
external. Preserve prototype masters and old preview snapshots as reference.

`play-new-horizons-creature-preview-linux.sh` selects the separately checksummed
`${XDG_DATA_HOME:-$HOME/.local/share}/new-horizons/cabir-wisp-preview-snapshots`
store (overridable with `NH_CREATURE_PREVIEW_STORE`) and uses its own
`new-horizons-creature-preview` profile. It never changes the ordinary launcher
pointer. Start a new **Cabir and Wisp Art Preview** scenario as Red/Solmyr: both
forms of each creature are already in the army, and Peasants stand beside him.
Do not load normal saves in this preview. Wisp uses temporary borrowed Core
shooter values and Psychic/Magic Elemental identities; Cabir's missing states
are supplied-pose aliases, including a standing death/corpse placeholder.
This is an art preview, not final Wisp gameplay or finished Cabir animation.
Its purchaser-derived Cabir pixels stay local and must not be published.

The two preview builders emit detached `Mods/new-horizons` overlays, combined
with `tools/ci/compose_creature_preview.py` and frozen using the normal snapshot
helper into the separate preview store. Generated maps and payloads remain in
explicit local storage outside the disposable build tree; tracked source contains
only tools and the opt-in map exporter.

## Ordinary playable snapshots

`linux_playable_snapshot.py` is a development-only helper. It copies the
currently built `new-horizons` and neighboring `libvcmi.so`, plus `config`,
`scripts`, `Mods/vcmi` and `Mods/new-horizons`, into a checksum-verified,
read-only snapshot. It is separate from `stage_linux_client.py`, which prepares
audited distribution packages.

The managed launcher keeps GTK's standard system desktop-data paths available
(`/usr/local/share:/usr/share`) for MIME recognition and native-dialog icons.
Replacing those paths with the game runtime directory prevents GdkPixbuf from
recognizing PNG icons on affected installations and can abort GTK dialogs.
Game-resource isolation remains separate: EntryPoint changes to the curated
runtime directory, where VCMIDirs development mode searches only that directory.
Private settings/cache paths and exclusion of inherited custom data roots remain
unchanged. A native image-decoding check is not full-game startup acceptance;
an error dialog may also be reporting an earlier startup failure.

Reporter-capable Linux client targets produce `new-horizons.features` beside
`new-horizons` after linking, and regenerate it after Linux installation so
RPATH or stripping changes bind the final installed bytes. This delivery-only
sidecar contains protocol
`NH_CLIENT_FEATURES_V1` and `BUG_REPORT_V1_SHA256=<lowercase client SHA-256>` on
separate LF-terminated lines. It contains no paths or user identity. Only the
client target that compiles `BugReportBootstrap.cpp` advertises this feature.

Freeze, snapshot verification and Linux packaging preserve and validate the
optional declaration against the actual client. Missing declarations retain
historical snapshot compatibility; malformed or mismatched declarations refuse
candidate assembly. The launcher skips automatic reporting when the sidecar is
missing, malformed, linked or bound to another executable, leaving retained
reports untouched. Automatic reporting also requires the canonical resource root
to equal the client directory, explicitly pairing a flat snapshot/package;
separate development/install resource roots retain play and explicit reporting.
This prevents a rollback client from receiving an unsupported
report option. An explicitly requested report command remains explicit. This is
a build/delivery contract, not runtime proof that the reporter renders correctly.

After a successful build, create a candidate without changing the default:

For NHART candidates, `--resources` must point to a detached, verified resource
stage with packed source files excluded, not the development `bin/config` link
to authoring inputs. Verify it with `tools/verify_new_horizons_art_install.py`.
In particular, the packed casting/Guild descriptor must not also exist loose.
Preserve the authoring source and committed package; do not repack to make an
incomplete development view pass. The example below assumes that stage is ready.

```sh
store=${NH_PLAYABLE_STORE:-${XDG_DATA_HOME:-$HOME/.local/share}/new-horizons/playable-snapshots}
candidate=$(python3 tools/ci/linux_playable_snapshot.py freeze --no-promote \
  --client build/new-horizons-linux/bin/new-horizons \
  --resources "$nh_resource_stage" \
  --store "$store")
```

Validate that exact candidate with the managed launcher and a private profile.
When a comparison build needs non-system shared libraries, repeat
`--runtime-library "$nh_provider_stage/libSDL3.so.0"` for each required provider.
Inputs must be regular ELF shared-object files with unique safe basenames;
copy resolved provider bytes under their SONAME names into local staging first.
These files participate in the snapshot's checksum and copy-time mutation checks.
The helper's structural ELF check does not establish architecture, SONAME,
dependency compatibility or runtime selection: verify those separately, including
actual loaded paths during acceptance. Do not use a mutable SDK library path to
mask a missing frozen provider. Omitting this option preserves the usual payload.

When replacing an existing candidate, add
`--retain-resources-from /absolute/path/to/verified/previous/snapshot` to `freeze`.
This verifies the previous snapshot and refuses missing curated resource paths
before copying. Updated files and binaries are permitted; old gameplay data is
never copied automatically. Historical private-overlay candidates required
explicit artwork assembly before freezing.
This catches optional artwork being silently lost in a newer build, including
the historical school-casting/Guild omission. Current normal candidates use the
committed NHART package and bootstrap mounts, not undistributed private overlays.

Normal development connects reviewed acceptance to promotion with `validate`.
Supply an explicit reviewed acceptance-driver argv after `--`; no command is
selected automatically and no shell interprets it:

```sh
python3 tools/ci/linux_playable_snapshot.py validate \
  --snapshot "$candidate" \
  --store "$store" --timeout 640 -- \
  /absolute/path/to/reviewed-validation-driver --config /path/to/private/config
```

That driver path is illustrative, not a shipped/checksum-only acceptance gate.
`--timeout` is required, finite and positive, in seconds. The helper overwrites
`NH_VALIDATION_SNAPSHOT` with the canonical in-store candidate path and
`NH_VALIDATION_SNAPSHOT_SHA256` with its payload digest. The reviewed driver
must consume and verify both, selecting that candidate's client/resources
explicitly rather than the current pointer or newest directory. Other environment
variables are inherited: strict environment, original-content, private-profile,
silent-audio, display/provider and gameplay admission remain driver obligations.
Zero means the driver declares its reviewed acceptance and cleanup complete;
this generic wrapper does not independently certify gameplay or rendering.
Mere startup, a timeout or a checksum-only command must not declare acceptance.

The snapshot is verified before and after acceptance, including metadata and
directory identity. Success automatically promotes only that identical candidate.
The current-pointer lease captures absence or valid file identity, timestamps and
bytes under the store lock before launch. The lock is released during validation.
Exclusive post-verification/CAS rejects a changed or malformed pointer, including
same-byte atomic replacement; a late run never refreshes its lease or overwrites
a newer selection. Invalid existing pointers are not treated as absence.

This Linux-only command supports ordinary main-thread CLI drivers whose children
remain in the fresh owned session/process group and are reaped by the driver.
Timeout or TERM/INT interruption triggers bounded group TERM/KILL (two seconds
per phase), then direct-driver reaping; descendants after a normal return also
cause failure/cleanup. The helper retains the unreaped session leader during
all signalling, preventing PID/PGID reuse from targeting an unrelated group.
Unconfirmed cleanup never promotes. Drivers must not daemonize or escape this
group: this is not a universal arbitrary-process cleanup guarantee.
Existing guarded graphical owner protocols with exact-PID cleanup or group-signal
prohibitions are **not automatically compatible**; their pinned invocation is
unchanged, and integration needs separate review. Driver stdout goes to stderr;
successful helper stdout contains only the selected path. Logs/receipts and
acceptance criteria belong to the driver, not a fabricated wrapper certificate.

### Signal-free completion for externally owned acceptance

A separately reviewed endpoint with its own fixed exact-PID graphical protocol
can use the same selection transaction without being wrapped by `validate`:

```text
ticket = begin_validation(store, exact_managed_candidate)  # Before any launch.
# The reviewed endpoint performs/owns its existing acceptance and full postchecks.
selected = finish_validation(ticket, accepted=reviewed_acceptance_passed)
```

These Python entry points spawn and signal nothing. `ticket.candidate` and
`ticket.digest` expose the bound identity; the private context stays in the
reviewed endpoint process, outside the payload. There is no JSON import, CLI
“accept” command or generic receipt-to-certificate parser. The endpoint must
hold the original ticket through its complete receipt, actual acceptance and
post-preservation checks. Only literal boolean `True` can finish promotion.
Every completion attempt consumes the ticket, including refusal; it cannot be
replayed after a mutation, stale lease or failed acceptance. Losing the endpoint
loses the ticket: a later process cannot reconstruct acceptance from a file.

`begin_validation` requires an exact generated snapshot inside the selected
managed store, verifies it and captures the strict pointer lease under lock.
`finish_validation` repeats exact candidate/store identity and lease checks
under exclusive lock before identical atomic promotion. It has no timers or
process ownership: the reviewed endpoint remains solely responsible for its
existing bounded launch, safe inputs, silence, exact-PID cleanup and reaping.
The literal acceptance boolean is a trusted endpoint declaration, not proof
created by this tool or a security boundary against arbitrary Python callers.

An external private candidate must first be frozen, without promotion, into the
managed store with its explicit provider quartet. Verify identical full payload
mapping/digest before starting acceptance; retain the original candidate and
evidence. Do not silently rebind an already pinned graphical protocol, copy
unreviewed bytes, promote a checksum-only result or waive its final receipt.
Existing pending graphical preparations are unchanged until their separate
integration, file materialization and runtime approvals.

Explicit `promote` remains available for approved rollback/preview workflows:

```sh
python3 tools/ci/linux_playable_snapshot.py promote \
  --snapshot "$approved_snapshot" --store "$store"
```

For a bounded non-graphical candidate check, use a fresh private profile outside
the original-assets root and pass the explicit headless option:

```sh
env -u DISPLAY -u WAYLAND_DISPLAY \
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software \
  timeout --signal=TERM --kill-after=30s 20s \
  tools/new-horizons-launch.sh \
  --assets /path/to/original-Complete-installation \
  --profile /path/to/fresh-private-profile \
  --client "$candidate/new-horizons" --resources "$candidate" -- \
  --testmap 'Maps/All for One.h3m' --headless --disable-video --savefrequency 0
```

Dummy SDL alone is not headless mode. Without `--headless`, `--testmap` can
enable the spectator battle UI, which is a different execution path from this
non-graphical smoke. Keep `--savefrequency` and `0` as separate arguments.
Retain the command and private logs, inspect actual map/AI progression and
diagnostics, and confirm the exact owned client has exited and the profile lock
is released. A timeout exit alone does not prove graceful cleanup or acceptance.
This check does not establish rendered UI or manual gameplay acceptance.

The default `play-new-horizons-linux.sh` verifies and launches only the snapshot
named by `current.json`. It never falls back to the mutable build tree or another
stage. `freeze` leaves that pointer unchanged; `validate` couples acceptance to
guarded atomic selection, while explicit `promote` verifies all payload
checksums before atomically replacing it. The pointer records the previous
promoted snapshot for review.

Keep the playable store outside disposable build directories. The default is
`${XDG_DATA_HOME:-$HOME/.local/share}/new-horizons/playable-snapshots`;
`NH_PLAYABLE_STORE` overrides it. The creature-preview wrapper uses the sibling
`cabir-wisp-preview-snapshots` store, configurable with
`NH_CREATURE_PREVIEW_STORE`. Use those external stores for new freeze/promote
commands. Existing snapshots can be relocated intact after stopping launches,
verifying all payloads and retaining their current/previous pointers.

Snapshots are intentionally not pruned automatically. A live launch uses
temporary runtime symlinks into its selected snapshot, and an interrupted process
can leave those links in a managed profile. To bound disk use manually, stop all
New Horizons runs first, inspect the `runtime.*` entries under each managed
profile, and retain every snapshot they target along with the current and
previous snapshots named in `current.json`. Remove only older
`snapshot-<sha256>` directories in this helper's dedicated store after confirming
they are not referenced. Leave unknown files and directories untouched.
