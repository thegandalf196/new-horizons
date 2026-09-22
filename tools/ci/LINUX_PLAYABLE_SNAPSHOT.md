# Local Linux playable snapshots

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
