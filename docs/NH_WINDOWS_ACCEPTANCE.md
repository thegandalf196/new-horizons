# New Horizons Windows x64 archive acceptance

## Status and authority

Independent archive audit assigned to Tester. **Actual Windows archive inspected;
publication blocked by missing dav1d notices/source inventory. Windows gameplay
remains unverified.** CI compilation, native tests, static inspection,
Linux acceptance and Wine do not establish Windows playability. Build owns the
workflow, downloadable artifact and release; Frontend owns Windows asset setup
and launch instructions. Tester owns this document and coordinated test helpers.
No GUI launch, software installation or modification of the Linux manual-preview
or saved profiles is authorized by this audit.

## Required evidence from Build

- Exact downloadable GitHub run/artifact or release URL, source commit and build
  configuration/toolchain/target Windows minimum. Distinguish source/evidence
  commits from the commit actually compiled into the binaries.
- Archive SHA-256, size and complete file manifest; dependency versions/provenance,
  build-option record, and corresponding-source download for the distributed build.
- Audit the **downloaded archive**, not only the staging folder or workflow text.
  Record inspected archive identity here before assigning PASS/FAIL.

## Archive gates

1. **Safe, ordinary extraction.** ZIP opens without errors; no absolute paths,
   traversal, drive/UNC paths, NTFS alternate streams, unsafe links, duplicate or
   case-colliding names. One clearly documented launch root; no accidental nested
   artifact wrapper that conceals the playable ZIP. Windows filenames/path lengths
   must permit ordinary Explorer extraction to a user-writable directory.
2. **Runnable x64 payload.** `VCMI_client.exe` and `VCMI_lib.dll` must be genuine
   AMD64 PE files from the same build. Recursively inspect normal and delay-load
   imports: each dependency must be supplied with compatible architecture or be a
   documented Windows system component. Include required SDL/media/Lua/TBB/other
   enabled runtimes and app-local redistributable runtime components as applicable;
   do not rely on a CI runner's Visual Studio, Conan or globally installed DLLs.
   Debug-runtime requirements or mandatory admin redist installation fail the
   no-admin portable requirement. A static import check cannot prove dynamic load
   paths or runtime initialization; retain that limitation.
3. **Internal authority and ordinary AI.** Build evidence must show the client
   links its authoritative simulation and uses the internal single-player route;
   no separate server executable is required/shipped for this curated preview.
   `VCMI_lib.dll` is the facade containing core, Lua and enabled AI—not a requirement
   to ship separate AI DLLs. Verify ordinary Nullkiller2/BattleAI/StupidAI build
   settings and default configuration, not merely filenames. No tests, synthetic
   maps, test executables, benchmark tools, compiler caches, PDB/debug packages,
   standalone lobby/server, or unrelated editor/launcher payload in the play ZIP.
4. **Complete open engine resources.** Match `config/`, `scripts/`, and
   `Mods/vcmi/` against the declared source revision/curated installation manifest;
   checking three directory names is insufficient. Include JSON schemas/defaults,
   AI configuration, Lua scripts and essential mod content/dependencies. Verify
   referenced files and preserve required original-compatible handlers. No optional
   third-party mod installation flow is needed for this edition.
5. **Licensing and source.** Include VCMI GPL license and preserved attribution,
   upstream/embedded resource notices and licenses for each distributed dependency.
   A CPack license setting is not evidence that a ZIP contains the license text.
   Provide corresponding source for this fork/build, including build/packaging
   scripts and applicable dependency source obligations; an upstream-only link or
   binary-only release is insufficient. Verify supplied source URLs are reachable
   and match the recorded versions. Naming/distribution-rights review is separate.
6. **No purchaser/private content.** Inventory every member and compare against
   tracked open resources and documented dependency payloads. Exclude original
   Heroes executables/installers, LOD/SND/VID archives, purchaser maps/campaigns,
   music, saves, screenshots, logs, profiles and local settings. Do not blindly
   reject all images/audio: legitimate essential VCMI resources require provenance.
   Inspect text plus ASCII/UTF-16 binary strings for credentials, tokens, keys,
   personal usernames/home paths and debug/source paths; report findings privately,
   never paste secrets into committed evidence. Extension/string scans alone cannot
   prove absence—combine with complete inventory/provenance and clean CI staging.

## Extract → select assets → play review

- A non-developer downloads the named Windows x64 play ZIP (not GitHub's source
  ZIP), extracts it fully to a writable folder, then opens the documented entry.
- No Git, Python, Conan, CMake, SDK, administrator prompt, developer-mode symlinks,
  machine-wide execution-policy change or security-software disabling is required.
  State unsigned-preview warnings honestly; do not recommend blanket bypasses.
- Select an existing purchaser-owned Heroes III Complete directory containing
  `Data`, `Maps`, `Mp3`; distinguish Complete from the incompatible HD edition.
  No original executable launch/download is needed. Explain any private local
  copying, storage cost, and exact write locations; never modify source assets.
- Quote and safely handle paths containing spaces/non-ASCII/shell metacharacters.
  Cancellation, missing/incorrect assets, unwritable destination and incomplete
  archive produce visible actionable errors, not an unexplained disappearing window.
- Keep saves/config/logs isolated from upstream VCMI and Linux/manual profiles.
  Document their location, repeat launch, asset reselection, upgrade and removal
  behavior without deleting user saves. Never package generated personal paths.
- Ordinary menu → New Game → Single Scenario → Begin, or Load Game; no host,
  address, port, optional mod-store or separate server setup instructions.

## Later Windows runtime gate (not performed)

On an explicitly authorized real Windows x64 system: test clean non-admin launch
without build tools, asset selection, original scenario start, movement/town/turn,
combat, save/quit/relaunch/load, errors/cancellation, and isolated profile writes.
Observe exact process identity/children and owned TCP/UDP independently. Record
Windows version, archive hash, evidence windows and limits; do not upgrade a
packaging PASS to gameplay PASS before this occurs.

## Source inspection findings before artifact delivery

- Root `CMakeLists.txt` installs scripts/config/Mods, and calls Conan runtime
  installation. Audit actual staged/install output for omissions and surplus files.
- `libFacade/CMakeLists.txt` names the Windows facade `VCMI_lib.dll` and absorbs
  enabled AI/Lua objects; absent separate AI DLLs is expected.
- Upstream Windows installation documentation describes a Qt launcher, optional
  recommended mods and a shared Documents profile. That is **not** the curated
  preview's user contract. Upstream developer build/admin instructions must not
  become the player's setup guide.

## Actual downloaded artifact audit — 2026-09-06

Run `34005089136` succeeded, artifact `9981596987`, source
`82a4e00b29485cb068a43616363bd26c3ab3844b`. GitHub reports an unexpired
326168082-byte artifact. Audited Build's downloaded four files, not CI staging;
only the play ZIP was extracted, into ignored Tester evidence storage. No GUI,
installation, binary execution or preserved-preview/profile writes occurred.

All three distributed archive checksums match `SHA256SUMS.txt`:

| Archive suffix after `New-Horizons-Windows-x64-82a4e00b2948` | Bytes | SHA-256 |
|---|---:|---|
|`.zip`|23687246|`8ca4668fb5d98ced4314ce019583430acd47ffb610e73691266fb7526a0306c1`|
|`-source.tar.gz`|16101678|`4c33a98794748bae525e4d0add92090221f1c9f71726b266e6b9af1f7d203d6f`|
|`-dependency-sources.tar.gz`|286378071|`84bf98933c79f4ac7ce8a4d6b52505b140f68d3c4a7c9fa78f9bd6ede77fc1e8`|

Passed inspected gates:

- ZIP CRC, safe names, no links/case collisions, one launch root:758 files,
  73503783 uncompressed bytes. All757 manifest-listed payload hashes match;
  manifest coverage is exhaustive except its own checksum file. Longest path
  below launch root is103 characters; use a reasonably short extraction path.
- Independent PE parser:26 AMD64 binaries, including one client and25 DLLs.
  Normal/delay/forwarded imports agree with `PE-IMPORTS.json`; non-bundled imports
  are Windows system/API components, not debug CRTs. Static closure does not
  prove dynamic initialization or codec playback.
- Client SHA-256 `7a87fa6497bf6bf71c2603fee295cab83c8d5598edd38c043c5f60f12cfeed09`;
  facade `c04a846a0aa74850a73fcb92936bbc4e09a9740326a328b37f40881079096822`.
  Build identity records SDL3, MSVC19.29, ordinary AI enabled, standalone server,
  tests and performance experiments OFF. No server/test/editor executable ships.
- All683 engine resources match the declared Git tree, allowing only checkout
  CRLF normalization and the intentional Windows `config/dirs.json` override.
  The three player helpers match that revision; setup tests are not shipped.
- Both source archives pass lexical path/link containment checks:4071 fork
  members;111782 dependency members,12 contained links. All23 listed non-system
  dependencies have source roots/recipes, but the list is incomplete (below).
- Root GPL/attribution, xBRZ terms and Microsoft terms are present. Both pinned
  Microsoft DOCX hashes and OOXML integrity match their provenance records.
- No purchaser archives/maps/saves, profiles, logs, test payload or PDBs found in
  the play ZIP. Targeted private-path/token checks found no player-payload hits.
  Source-only scanner positives were reviewed: two Boost CVE synthetic strings
  and tracked macOS PSD raster bytes are not credentials. Corresponding source
  retains the exact upstream `TerrainViewTest.h3m` test fixture, not a purchaser
  map; it is absent from the play ZIP. Generic CI Conan paths are embedded in
  FFmpeg; these are not purchaser identity. Scans are not exhaustive secret proof.

**Publication blocker:** `MEDIA-RUNTIME.json` records static
`dav1d/1.5.4#001b758cfd88fd816b286e09e686dd5c`, but `DEPENDENCIES.json`, bundled
notices and dependency-source roots omit it. Actual `avcodec-63.dll` contains the
VideoLAN dav1d decoder and configure flags enabling libdav1d, so this is not just
an unused recipe option. Add exact corresponding notices/attribution and inventory
(and source for the promised complete dependency bundle), then re-audit the
replacement archives/hashes. Check other skipped static transitive dependencies
rather than treating PE closure as license closure. Runtime/Build were notified.

Also requested: copy FFmpeg's applicable full LGPL text into its play-ZIP notice
folder; that folder currently contains only `LICENSE.md`, which refers readers to
`COPYING.LGPLv2.1`. Source-companion terms are not missing merely because this
play-ZIP folder is incomplete; retain this distinction when repairing notices.

Local derived evidence: `testing/windows-audit-82/download-checks.json`,
`independent-pe.json` and `source-audit.json` under the Linux build root.

Final archive verdict: **FAIL publication gate pending notice/source repair.**
This is a packaging verdict, not a finding that Windows gameplay fails. Actual
Windows launch/gameplay/media behavior remains **UNVERIFIED**.
