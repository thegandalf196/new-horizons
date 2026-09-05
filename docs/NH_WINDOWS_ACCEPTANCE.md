# New Horizons Windows x64 archive acceptance

## Status and authority

Independent archive audit assigned to Tester. **No Windows archive supplied or
Windows gameplay verified yet.** CI compilation, native tests, static inspection,
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

Final archive verdict: **PENDING artifact delivery and independent inspection.**
