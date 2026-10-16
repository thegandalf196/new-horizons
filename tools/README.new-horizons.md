# New Horizons Linux launch profile

## Selected Git credential prevention

Linux x86-64 CPython 3.14 tooling inspects explicit Git index/commit blobs, never
untracked or working-tree payloads. R1/R2 conversation framing remains the
separate `ci/publication_content.py` entrypoint with its existing finite selector.
Credential selection includes unknown regular text names/extensions, PHP, HCL,
Terraform and config files; selected non-UTF8/NUL fails closed. It is not a
history, archive-content or exhaustive privacy inspection.

Use a locally supplied Gitleaks archive matching `ci/gitleaks.lock.json`:

```sh
"$NH_PYTHON" -B tools/ci/credential_archive.py --verify "$NH_GITLEAKS_ARCHIVE"
"$NH_PYTHON" -B tools/ci/credential_content.py --root . --revision "$NH_REVISION" \
  --gitleaks-archive "$NH_GITLEAKS_ARCHIVE"
```

`--staged` replaces `--revision` for explicit index inspection. The strict CLI
returns 0 for clean selected text, 1 for findings and 2 for issues. It deliberately
refuses inherited JKS inputs. `--diagnostic-inherited-jks` may inspect supported
text only after exact two-path/mode/OID/count/alias and ancestor metadata checks;
the two stores are never read. That diagnostic always remains ISSUE/exit2 with
`gate_pass=false`, retaining both uninspected binary issues.

CI uses `ci/credential_text_gate.py` with the same root/revision/archive arguments.
It accepts only an in-process complete, clean, nonempty supported-text transaction
with consistent evaluated counts, exact policy/provenance and exactly those two
uninspected issues. Arbitrary exit2/report files, new denials, drift, incomplete
inspection or unresolved findings cannot pass. Its exit0 means supported-text
prevention only; global binary status remains ISSUE. It is not approval to add
key stores.

`reviewed_nonsecret_findings` retains the original rule/blob/path/line metadata
and disposition for exact reviewed runtime-resource identifiers declared in
`ci/credential_disposition.py`. Classification uses only those identity tuples,
not a value, path or suffix ignore. All other records remain unresolved in
`findings`; `finding_counts` accounts for detected, reviewed and unresolved records.
Changed tuples or duplicate records cannot pass. Raw detector finding metadata
remains reviewable; this limited disposition is not exhaustive credential clearance.

Explicit media/artifact suffix exclusions include PSD, ICNS and JAR/ZIP archives;
their contents remain uninspected. Four exact path/mode/OID binary bindings cover
two recruitment sprites, the macOS disk-image metadata and the neutral palette.
Changed/renamed/duplicate aliases refuse; no blanket DEF, PAL, BIN or extensionless
exemption exists. ENC and other credential-container suffixes are manifest denials;
PKCS12 is presence-only. Machine scope/counts distinguish exclusions, known
uninspected binaries and evaluated text. None establishes binary safety.

The pinned archive checksum is verified before extraction/execution; the detector
version and positive/negative witnesses are checked per transaction. Only safe
OID/applicability filenames enter one fresh private batch with an empty tool cwd;
no project scanner config/ignore files or original paths are passed. Detector
network syscalls are denied. Output contains fixed codes/digests, not matches or
raw subprocess diagnostics. MIT scanner attribution is in `ci/LICENSE.gitleaks`.
CI downloads only the exact locked HTTPS release URL and verifies cached bytes;
these inspection tools never download or install anything themselves.

Limits: one cooperative 240-second transaction (witness10-second detector /
12-second transport caps), 10000 entries, 16MiB per original object, 128MiB unique
and physical representation bytes, bounded reports/findings/stderr. GOMAXPROCS=1,
soft GOMEMLIMIT and sampled RSS aborts are not hard system-wide memory caps;
ownership covers direct children, not arbitrary descendant trees. Default content
allowlists/entropy/finite fragments and external Gitlink/history exclusions remain.

Synthetic controls require the verified archive but no game/private inputs:

```sh
NH_CREDENTIAL_TEST_ARCHIVE="$NH_GITLEAKS_ARCHIVE" "$NH_PYTHON" -B -m unittest \
  discover -s tools/tests -p 'test_credential_*.py'
```

## Manual Linux profile

`new-horizons-launch.sh` is a manual launch helper, not a game test harness or
sandbox. Requires Bash, GNU coreutils/findutils, and `flock` (util-linux), already
available on the supported native Linux host. It does not install anything.

Source exporters share the reviewed contributor-document list in
`ci/source_export_policy.py`. Revisions containing the tracked, regular
`NH_DOCUMENTATION_POLICY.md` export those maintained guides; historical revisions
retain the earlier privacy exclusions. Unknown `NH_` documents and the deploy key
remain excluded. Review that list when moving a contributor contract; do not add
coordination diaries to make a source-export test pass.

The default client and resources come from the checksum-verified immutable
snapshot selected in the local playable store. Follow the build and delivery
procedure in [`NH_DELIVERY_PIPELINE.md`](../docs/NH_DELIVERY_PIPELINE.md).
The build owner must supply a matching Linux/XDG client,
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
To use a VCMI diagnostic entry point without bypassing this managed profile,
place a conventional `--` after the launcher's options; every following argv is
passed verbatim to `vcmiclient` after the mandatory `--nointro`, for example:

```sh
tools/new-horizons-launch.sh --assets '/path/to/purchased Heroes III Complete' \
  --profile '/path/to/new NH profile' -- \
  --testmap 'Maps/Arrogance Allied.h3m' --disable-video
```

`--testmap` is VCMI's built-in debug scenario route: it starts the selected map
through the normal in-process server and avoids fragile main-menu coordinates.
It is a diagnostic route, not a replacement for ordinary-input acceptance.
The example above can open spectator battle UI. For non-graphical checks,
also pass `--headless`; dummy SDL drivers alone do not disable that UI.
To validate an unpromoted Linux snapshot, follow the explicit candidate/profile
recipe in [LINUX_PLAYABLE_SNAPSHOT.md](ci/LINUX_PLAYABLE_SNAPSHOT.md).
Do not launch the original binary directly and assume this profile is applied.

Run the bounded gameplay smoke test with:

```sh
bash tools/tests/new-horizons-scenario-smoke.sh
```

It must load a real map, advance AI turns, and emit no server rejection,
capability-rule failure, or battle hero access-check spam. Set
`NH_SMOKE_SECONDS`, `NH_SMOKE_MAP`, `NH_SMOKE_MINIMUM_TURNS`, or
`NH_KEEP_SMOKE_LOG=1` to override its defaults.

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
  `data/new-horizons/Saves`, `config/vcmi`, and `cache/vcmi`. The original installation is
  never the user-data root. Profile overlap with inputs is rejected.
  Earlier `data/vcmi/Saves` files remain untouched. With the game closed, copy
  their save files to the new directory without overwriting conflicting names;
  retain the originals until the copied games load successfully.
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
