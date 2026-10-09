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
When replacing an existing candidate, add
`--retain-resources-from /absolute/path/to/verified/previous/snapshot` to `freeze`.
This verifies the previous snapshot and refuses missing curated resource paths
before copying. Updated files and binaries are permitted; old gameplay data is
never copied automatically. Historical private-overlay candidates required
explicit artwork assembly before freezing.
This catches optional artwork being silently lost in a newer build, including
the historical school-casting/Guild omission. Current normal candidates use the
committed NHART package and bootstrap mounts, not undistributed private overlays.

Pass `--client "$candidate/new-horizons" --resources "$candidate"` explicitly,
so validation cannot accidentally select the currently promoted snapshot. After
the required headless new-game validation succeeds, promote the same candidate:

```sh
python3 tools/ci/linux_playable_snapshot.py promote \
  --snapshot "$candidate" \
  --store "$store"
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
stage. `freeze` leaves that pointer unchanged; `promote` verifies all payload
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
