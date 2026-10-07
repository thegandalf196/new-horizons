# Local Linux playable snapshots

## Isolated Cabir / Wisp art preview

`play-new-horizons-creature-preview-linux.sh` selects the separately checksummed
`build/new-horizons-linux/cabir-wisp-preview-snapshots` store and uses its own
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
ignored `build/`; tracked source contains only tools and the opt-in map exporter.

## Ordinary playable snapshots

`linux_playable_snapshot.py` is a development-only helper. It copies the
currently built `vcmiclient` and neighboring `libvcmi.so`, plus `config`,
`scripts`, `Mods/vcmi` and `Mods/new-horizons`, into a checksum-verified,
read-only snapshot. It is separate from `stage_linux_client.py`, which prepares
audited distribution packages.

After a successful build, create a candidate without changing the default:

```sh
candidate=$(python3 tools/ci/linux_playable_snapshot.py freeze --no-promote \
  --client build/new-horizons-linux/bin/vcmiclient \
  --resources build/new-horizons-linux/bin \
  --store build/new-horizons-linux/playable-snapshots)
```

Validate that exact candidate with the managed launcher and a private profile.
Pass `--client "$candidate/vcmiclient" --resources "$candidate"` explicitly,
so validation cannot accidentally select the currently promoted snapshot. After
the required headless new-game validation succeeds, promote the same candidate:

```sh
python3 tools/ci/linux_playable_snapshot.py promote \
  --snapshot "$candidate" \
  --store build/new-horizons-linux/playable-snapshots
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
  --client "$candidate/vcmiclient" --resources "$candidate" -- \
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

Snapshots are intentionally not pruned automatically. A live launch uses
temporary runtime symlinks into its selected snapshot, and an interrupted process
can leave those links in a managed profile. To bound disk use manually, stop all
New Horizons runs first, inspect the `runtime.*` entries under each managed
profile, and retain every snapshot they target along with the current and
previous snapshots named in `current.json`. Remove only older
`snapshot-<sha256>` directories in this helper's dedicated store after confirming
they are not referenced. Leave unknown files and directories untouched.
