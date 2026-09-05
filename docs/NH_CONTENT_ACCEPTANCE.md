# New Horizons — content and ordinary-game acceptance

Status: read-only source inspection at `a9b6d945e`; **not runtime acceptance**.
Contract: [NEW_HORIZONS_MVP.md](NEW_HORIZONS_MVP.md). No game/GUI launch,
installation, build, or proprietary-asset inspection was performed for this note.

## Required content and dependencies

| Layer | Required for the curated edition |
| --- | --- |
| Purchaser content | Original **Heroes III Complete** `Data`, `Maps`, and `Mp3` directories, outside Git. Upstream also supports SoD, but that is not this fork's Complete acceptance baseline. HD Edition and native LOKI Linux data are not substitutes. No original executable is needed to play through VCMI. |
| Graphics/text | Original bitmap and sprite LODs, including Complete's AB content. Preserve the installation's localized patch archives rather than assembling a minimal hand-picked collection. |
| Audio/video | Original sound archives, MP3 soundtrack, and video archives for the full presentation/campaign journey. Missing music/video may not block scenario startup; startup alone is not full-content acceptance. |
| Scenarios/campaigns | Purchaser's original `.h3m` maps and original RoE/AB/SoD campaign resources (`.h3c`, including archive-contained resources). No downloaded custom map or fan campaign is needed. |
| Engine resources | Ship the matching upstream `config/`, `scripts/`, and **`Mods/vcmi`** resources with their licenses/notices. `vcmi` is “VCMI essential files,” not the optional `vcmi-extras` download. Virtual `core` contains base configuration/content; retain the internal mod/dependency loader. |
| Runtime/build | Matching client and `vcmi` facade with simulation library and enabled AIs; SDL2 or SDL3 plus image/mixer/ttf, Boost, compression libraries, TBB, and LuaJIT/Lua. Lua and shipped combat scripts are required by this checkout's full-game build (including `scripts/damage/damageCalculator.lua`). FFmpeg supports video. Qt is for launcher/editor, not an original-content asset requirement. Use the build owner's exact resolved dependency list; do not install packages for this inventory. |

Archive discovery is defined by `config/filesystem.json`: `H3ab_bmp.lod` then
`H3bitmap.lod` and applicable localization patches supply `DATA/`;
`H3ab_spr.lod` then `H3sprite.lod` supply `SPRITES/`. Sounds include
`H3ab_ahd.snd`, `Heroes3.snd`, and `Heroes3-cd2.snd`; video candidates include
`H3ab_ahd.vid`, `Heroes3.vid`, and `video.vid`. These are supported installation
variants, **not a requirement that every named archive exist in every edition**.
The configured `H3psprit.lod` entry is deliberately disabled upstream. Keep that
behavior. Loose Data/Sprites/Sounds/Video files can override archive resources.

No original assets, executables, installers, extracted files, saves, or map copies
are to be committed or redistributed. Preserve VCMI GPL notices, attribution and
resource-specific licensing; GPL coverage is not permission to distribute Heroes
III content. Naming/distribution rights remain separately reviewable.

## Discovery and contamination checks

- Native Linux: `lib/VCMIDirs.cpp` selects installed data roots (`M_DATA_DIR`,
  reversed `$XDG_DATA_DIRS` plus `/vcmi`, or standard share paths, also
  `/usr/share/games/vcmi`). Development mode uses `.` instead of system roots.
  `lib/filesystem/Filesystem.cpp::createInitial` then adds user data, normally
  `$XDG_DATA_HOME/vcmi` or `~/.local/share/vcmi`. Saves are under its `Saves/`;
  settings normally use `$XDG_CONFIG_HOME/vcmi` or `~/.config/vcmi`.
- Resource names are case-insensitive; later sources win. Record **all effective
  roots**, not just the intended asset directory. Existing user Mods, loose
  overrides and presets can silently contaminate a supposedly clean baseline.
  Arrange an approved isolated runtime profile later without deleting or modifying
  the purchaser's data or other VCMI installations; this note creates none.
- Launcher `FirstLaunchView::heroesDataDetect` checks `DATA/GENRLTXT.TXT` and
  `DATA/TENTCOLR.TXT` (with a demo exception). This is only a rough presence check.
  `StartGameTab::refreshGameData` probes town backgrounds, AB campaigns,
  `Music/MainMenu`, and `Video/H3Intro`. Its missing-files/campaign flags currently
  start false and use `&=`: absence of those warnings does **not** prove completeness.
- Windows additionally has launcher registry-based installation suggestions;
  actual resource roots remain controlled by `VCMIDirs`/filesystem configuration.
  Do not infer that a neighboring original installation is automatically mounted.

## Optional content and default traps

- Baseline active root mod: `vcmi` (`lib/modding/ModManager.cpp` creates this fresh
  preset). Exclude `roe-demo`, `vcmi-test`, third-party expansions, translation
  downloads, HD packs, Chronicles and custom maps from first-gate evidence.
  Retain dependency/version validation rather than bypassing it.
- `Mods/vcmi/mod.json` includes engine fixes, translations and RMG templates,
  including HD-mod/symmetric template families. Do not delete the essential mod
  wholesale in pursuit of “no Mods.” Use an original fixed scenario first;
  any later template-selection curation needs separate review.
- Launcher first-run presets **preselect `vcmi-extras`** and offer HotA, WoG and
  other fan content (`launcher/firstLaunch/firstlaunch_moc.cpp`). These are optional,
  not required original-content dependencies. Translation suggestions can also
  introduce downloads. New Horizons must remove that optional installation journey;
  this document proposes policy, not a completed UI/config change.
- `config/schemas/settings.json` enables repository checks/updates by default.
  Those are launcher services, not prerequisites for single-player. `lastMap`
  defaults to `Maps/Arrogance`: select an actually installed original scenario
  explicitly, especially with localized filenames. `useHdTextures=true` does not
  require a texture pack: renderers check availability and fall back. Intro/video
  defaults do require working original media plus video support for full fidelity.
- Gameplay AI defaults are **Nullkiller2/BattleAI**, not MMAI. Nevertheless root
  CMake defaults `ENABLE_MMAI=ON`, introducing an ONNX Runtime build dependency
  (`AI/MMAI/CMakeLists.txt`). It can be disabled by the build owner for the MVP;
  no ML-content acquisition belongs in the ordinary acceptance route.

## Ordinary full-game route — pending bounded run authorization

Record platform, source revision/dirty state, assigned build/candidate identity,
renderer, effective data/settings/save roots, original edition/language, active
content and selected original map/settings in local evidence. Do not put private
absolute paths or proprietary artifacts in Git. Use normal menu/mouse/keyboard
input, no cheats, direct state mutation, test-only maps or new harness framework.

1. Open the ordinary menu → New Game → Single Player → scenario selection.
   Choose an original scenario with one human, at least one AI, an accessible town,
   resources and reachable combat. Record hero/faction/difficulty; obtain a
   spellbook, learned combat spell and sufficient mana through ordinary play.
2. Start; select a hero, plot and complete multi-step movement, pick up resources
   and verify inventory changes. Enter town, construct a legal building, recruit
   troops and verify costs, army and dwellings. Check graphics, text and audio.
3. Play **at least seven turns**, including AI completion and the week boundary;
   verify income/growth and regained movement. Fight manually and cast a spell;
   check animation, effects, casualties and return to the adventure map.
4. Use the normal Save Game dialog and wait for success. Record day, hero
   position/stats/mana/army/artifacts, resources, town buildings and ownership.
   Quit the application, restart the same candidate/profile, then Load Game →
   Single Player and select that save. Compare recorded state and continue moving,
   visiting town, ending turns and fighting. Also check a normal autosave reload.
5. Continue to a genuine victory or defeat and the result/return-to-menu flow.
   Observe no separate `vcmiserver` child or single-player TCP/UDP listener or
   loopback transport during selection, start, save/load, play and shutdown.
   Record errors, stalls and missing media explicitly; source inspection alone
   cannot establish this condition or graphical usability.

Save/load anchors: `config/mainmenu.json` routes `start single`/`load single`;
`client/lobby/SelectionTab.cpp` enumerates `Saves/`; `CMapInfo::saveInit` reads
headers/options. `CCallback::save` sends `SaveGame` through server validation;
`CGameHandler::save/load` writes/reads `.vsgm1`, game state and server state.
`ActiveModsInSaveList` verifies gameplay-affecting content and rejects missing,
disabled or excessive root mods. Keep the same candidate/content for the baseline:
no promise of importing original Heroes III saves or arbitrary upstream versions.

Campaign continuity is a follow-up original-content regression: New Game → Campaign
→ RoE/AB/SoD, choose scenario/bonus, save/quit, Load Game → Campaign, finish a
scenario and verify unlocks and permitted hero carryover into the next scenario.
Custom campaigns/Chronicles are not prerequisites. The first scenario gate alone
does not prove all original campaigns completable.

## References read

Upstream [Code Structure](developers/Code_Structure.md),
[Serialization](developers/Serialization.md), [Networking](developers/Networking.md),
[Linux build](developers/Building_Linux.md) and
[Linux installation](players/Installation_Linux.md), checked against current source.
Some upstream prose is historical (notably TCP even for single-player and serializer
version details); the New Horizons contract and current implementation take precedence.
