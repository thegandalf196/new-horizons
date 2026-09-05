# New Horizons Linux launch profile

`new-horizons-launch.sh` is a manual launch helper, not a game test harness or
sandbox. Requires Bash, GNU coreutils/findutils, and `flock` (util-linux), already
available on the supported native Linux host. It does not install anything.

The default client is repository-relative
`build/new-horizons-linux/bin/vcmiclient`, as assigned in
[`NH_BUILD_HANDOFF.md`](../docs/NH_BUILD_HANDOFF.md). Engine resources default to
that binary's directory. The build owner must supply a matching Linux/XDG client,
`libvcmi.so`, `config/`, `scripts/`, and `Mods/vcmi/`. Use `--client` and
`--resources` to select another owner-approved candidate; no binary is probed by
executing it. This does not support Windows, macOS, PortMaster or AppImage layouts.

Safe path-only preflight (replace these example paths):

```sh
tools/new-horizons-launch.sh --verify-only \
  --assets '/path/to/purchased Heroes III Complete' \
  --profile '/path/to/new NH profile'
```

`--assets` and `--profile` are mandatory. Preflight creates nothing and never
launches, even if the client is executable. It checks directories, core resource
sentinels, and original bitmap/sprite archive names, **not** archive integrity,
Complete campaign/media completeness, ownership, binary ABI or graphical behavior.
An existing profile must carry this helper's marker; existing unrelated directories
are rejected rather than imported or overwritten. Client paths containing `:` are
rejected because the Linux library search path uses that separator.

**Manual game launch:** only after separate bounded graphical-run authorization,
invoke the same command without `--verify-only`. No automatic launch or launch
acceptance is implied by these instructions. The helper waits for the client,
returns its status and removes its temporary link directory on ordinary exit.
Do not launch the original binary directly and assume this profile is applied.

## Why these paths isolate inherited content

- `clientapp/EntryPoint.cpp` changes cwd to the directory of `argv[0]` (without
  resolving the executable symlink). The helper calls a profile-local `vcmiclient`
  symlink by absolute path.
- `IVCMIDirsUNIX::developmentMode()` in `lib/VCMIDirs.cpp` detects that directory's
  `config`, `Mods`, and client. In Linux/XDG mode this excludes **all** system data
  roots, including the compiled install root and `/usr/share/games/vcmi`.
  Setting `XDG_DATA_DIRS` alone would not achieve this.
- A fresh temporary root links only engine `config`, `scripts`, `Mods/vcmi`, the
  matching binary/library, and purchaser `Data`, `Maps`, `Mp3` directories (names
  matched case-insensitively). Neither the purchaser's nor engine's whole `Mods`
  directory is linked. Original assets are not copied, patched, chmodded or deleted.
- `lib/filesystem/Filesystem.cpp` mounts these asset directories as non-writable
  loaders. Its writable local saves/config loaders use profile-specific XDG roots:
  `data/vcmi/Saves`, `config/vcmi`, and `cache/vcmi`. The original installation is
  never the user-data root. Profile overlap with inputs is rejected.
- `lib/CConfigHandler.cpp` loads/writes local settings; `ModsPresetState` in
  `lib/modding/ModManager.cpp` initializes a fresh preset with `vcmi` plus implicit
  `core`. We retain that mechanism and the essential resources, not optional user
  presets/settings. Existing NH settings/saves persist. Symlinks in writable
  profiles and extra entries beside `Saves` in user data are rejected on reuse.
  This intentionally excludes saved random maps/custom maps from this first-gate
  helper; use the ordinary original-scenario route.
- Dynamic loading uses only the candidate directory as `LD_LIBRARY_PATH`, not an
  inherited value; `LD_PRELOAD`/`LD_AUDIT` are removed for the child. Desktop display,
  audio and session environment remain available. A profile lock prevents shared
  settings/save writers. After a forced kill, stale `runtime.*` links may require
  manual removal or a fresh profile; no automatic recursive cleanup of old profiles.

This is **not global unmodifiability or OS-enforced read-only protection**. The
purchaser installation and engine candidate must be trusted, unmodified inputs;
loose overrides already inside original `Data` remain visible. The user can edit
engine resources, NH settings, or the helper and can run other clients. It does not
validate every saved setting or defend against concurrent malicious filesystem
changes. GPL recipients retain their modification rights. No assets, profiles or
saves belong in Git or redistribution.

## Non-GUI regression tests

```sh
bash -n tools/new-horizons-launch.sh tools/tests/new-horizons-launch-test.sh
bash tools/tests/new-horizons-launch-test.sh
```

Tests create synthetic empty resource files and an explicit shell-stub client in
a temporary directory. They check preflight/no writes/no execution, missing and
ambiguous inputs, quoted paths, refusal of unrelated profiles, curated link roots,
XDG isolation against inherited Mods, profile reuse/save persistence, symlink and
optional-data rejection, and cleanup. They never invoke the built client, a GUI,
or proprietary content. Passing these tests is not full-game acceptance; follow
[`NH_CONTENT_ACCEPTANCE.md`](../docs/NH_CONTENT_ACCEPTANCE.md) when authorized.
