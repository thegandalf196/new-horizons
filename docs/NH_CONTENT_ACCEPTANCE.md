# Curated content and ordinary-game acceptance

## Required layers

Heroes III Complete purchaser data remains external: localized bitmap/sprite
archives, expansion content, Maps, Mp3, sounds and video for the full journey.
Supported archive names/variants come from `config/filesystem.json`; not every
named variant must coexist. Startup without music/video is not full-content
acceptance. Original executables are not needed to play via VCMI.

Ship matching `config/`, `scripts/`, `Mods/vcmi`, curated New Horizons config
and NewHorizons.nhart with licenses/notices. `vcmi` essential files are not the
optional Extras download. Preserve internal dependency/version checks.
The client links the `vcmi` facade with simulation and enabled AI implementations;
Lua and the damage script are required for this checkout's ordinary full-game route.
Qt launcher/editor and optional MMAI are separate build choices.

## Contamination and admission

`lib/VCMIDirs.cpp` and `lib/filesystem/Filesystem.cpp` determine effective roots.
Later loose/mod resources can override archive content. Record all effective roots,
not only the intended data directory. Use an isolated disposable profile without
deleting purchaser data, ordinary saves or other installations.

Launcher presence probes are not completeness certification. Do not infer working
campaign/media resources from absent warning flags. Curated acceptance must not
depend on fan expansions, translation downloads, HD packs, Chronicles, custom maps,
developer overlays or caches. Resource-specific provenance still applies to selected
Extras equivalents.

## Acceptance route

Use an installed original fixed scenario for the initial journey: menu/setup,
map movement and pickups, town/build/recruit, AI turns, battle/spellcasting,
save/quit/reload/continue, victory or defeat. Exercise normal UI navigation,
cancellation and return paths, not only constructed dialogs. Verify the committed
art pack without loose custom-art fallbacks.

For a Tower-versus-Inferno acceptance milestone, enumerate every building in both
actual town rosters. Check prerequisites, construction, triggered or passive
effects, repeat/visit restrictions, applicable AI use, persistence and player
feedback against the accepted specification. Record evidence and remaining gaps
per building; selected unique-building tests do not establish complete-roster
acceptance.

Separate source/data consistency, native mechanics, package integrity, ordinary
rendered acceptance and platform coverage. Report exact scope and limitations;
do not derive a whole-product percentage from active flags or test totals.
See [content source map](developers/NewHorizonsContent.md),
[delivery](NH_DELIVERY_PIPELINE.md), [integration gaps](NH_INTEGRATION_GAPS.md)
and [regression guards](NH_REGRESSION_GUARDS.md).
