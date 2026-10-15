# Windows x64 player-package acceptance

Audit the actual downloadable archive, not only staging/workflow text. Record source revision, CI run/artifact identity, inner player ZIP bytes/hash, toolchain/configuration, and evidence limits privately. An API digest for an unretained outer artifact envelope is not independently verified by hashing its inner ZIP.

## Static gates

1. ZIP CRC and full inventory pass. Reject traversal, absolute/drive/UNC/alternate-stream paths, unsafe links, duplicates and case collisions. Verify one ordinary extraction/launch root.
2. `new-horizons.exe` and `VCMI_lib.dll` are matching AMD64 PE outputs. Audit normal/delay imports recursively, compatible runtime architectures, app-local CRT/media/Lua/TBB providers and documented Windows system imports. Static closure does not prove dynamic loading.
3. Client contains authoritative internal single-player execution and ordinary enabled AI. The facade aggregates core/Lua/AI; separate AI DLLs/server executable are not required. Exclude tests, synthetic maps, developer tools, caches and unrelated launcher/editor/lobby payload.
4. Compare all config/scripts/curated mods/resources to the declared source revision and submodules; verify required custom bindings, bootstrap mounts and the complete NHART inventory. No private overlays or loose fallbacks.
5. Preserve GPL/attribution and applicable dependency licenses/notices/corresponding sources, including static/header-only graph nodes and recipe patches. Metadata, a LICENSE setting or an upstream-only URL is not sufficient source proof.
6. Exclude purchaser executables, original archives/maps/music, saves/settings/logs/screenshots and authoring materials. Audit complete inventory/provenance plus ASCII/UTF-16 binary strings and text for private paths/credentials. Preserve legitimate third-party attribution; report raw privacy findings privately.
7. Inspect supplied platform-icon resources/provenance and actual PE metadata. Unsigned distribution and OS-shell appearance remain separate qualifications.

Use the maintained workflow/package validators and focused `test_windows_pe_audit.py`, `test_windows_dependency_notices.py`, `test_binary_privacy.py`, `test_git_source_snapshot.py`, `test_windows_notice_overlay.py` and `test_mingw_runtime.py` as applicable. Resolve the verified absolute interpreter for the environment; do not assume a developer path.

## Ordinary player route

Extract the full player ZIP into a writable folder; select a purchaser-owned Heroes III Complete installation (not HD Edition). Original assets remain external/read-only. The documented player route needs no Git, Python, Conan, SDK, administrator redist installation, optional mod store or separate server setup. Preserve isolated profile/saves, F8/F9 semantics, cancellation and actionable asset/path errors. Never recommend disabling security software or blanket execution-policy bypass.

## Runtime acceptance is separate

On an authorized native Windows machine, exercise clean non-admin startup, asset selection, ordinary new game/load, movement/town/turn/combat, save→Quit→relaunch→load, cancellation/errors and profile boundaries. Record actual source/package/process identity and transitions.

Wine surface/bootstrap, Linux rendering, CI compilation and static package PASS do not certify native Windows gameplay, pixels, animation, media, performance or installation. [Integration gaps](NH_INTEGRATION_GAPS.md) records remaining breadth; the withdrawn 0.85 preview is not a current release.
