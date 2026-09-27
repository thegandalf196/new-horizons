# New Horizons UI and asset status register

Last audited: 2026-09-27

This is the maintained UI/art status index for the current New Horizons working tree. It records live bindings and remaining visual/UI work. It does not replace gameplay specs, source configs, runtime manifests, or acceptance records. The row-level index is [NH_UI_ASSET_INVENTORY.csv](NH_UI_ASSET_INVENTORY.csv).

The audit used canonical registries and current source bindings rather than counting files. It covers 70 active combat-school spells across the six New Horizons schools plus the five Neutral Adventure Spells, all 31 registered secondary skills, all 310 perk definitions (68 active and 242 planned), all eight Order bindings, and the scoped hero, spellbook, convenience, creature-info, and ranked-recruitment UI surfaces below. Active means enabled in source configuration, not necessarily promoted to the playable build. The inactive Clone roster entry is excluded. The unregistered Magic Missile and Spell Lock definitions are noted separately as non-live.

This is a binding inventory, not a full visual audit or a product completion claim. No game was launched and no GUI review was performed for this register. A resource path, generated manifest, native-size file, or implemented code path does not by itself establish final art or accepted UI.

## Status meanings

Every row has separate Implementation and Art columns. Each column uses only these three values:

- Not done — the live UI behavior/binding is absent, or the artwork is missing or a mismatched stand-in.
- Provisional — a source binding or usable art exists, but visual approval or the remaining integration/layout check is not evidenced.
- Final — this item has explicit user art approval, or uses an exact, suitable classic Heroes III art binding under the user-authorized reuse policy in [NEW_HORIZONS_MVP.md](NEW_HORIZONS_MVP.md). Final art does not mean the UI integration has passed graphical acceptance.

Implementation is Final only when the intended UI behavior and its presentation have been accepted. The CSV's Remaining field explains each classification and points to current sources.

## Current status summary

Grand Metamagic no longer has an activation button in the battle strip or
spellbook. Its automatic continuation uses the existing Spell Action count;
no new artwork or permanent skill-resource slot is introduced. This control
removal does not change the provisional approval status of the perk icon.

| Component | Implementation | Art | Evidence and remaining work |
| --- | --- | --- | --- |
| Skill-provided combat resources | Provisional | Provisional | Optional skill metadata declares a typed read-only status provider and localized help. Generic entries reuse the learned rank's existing icon and show current/maximum values separately from round Actions. Metamagic and Bloodrage are the first two typed Skill providers; Counterspell and Warcasting use the same row renderer for non-Skill state. Actual visible height controls panel placement, without a reserved Metamagic slot. Source review, metadata tests and focused UI guards pass; native client CI and graphical acceptance remain pending, including compact stack overlays and sticky panels on/off. |
| Magic Arrow Overcharge outcome comparison | Provisional | Provisional | The selected-target dialog compares damage and estimated casualties at zero and selected Overcharge, refreshing on amount changes through the shared effect forecast. Resistance chance is explicit; unavailable forecasts do not block legal casts. The compact 320×250 layout reuses the game's leather texture and standard bordered-window frame, replacing the flat 420×340 panel. The client rebuild and source checks pass; rendered layout and interaction acceptance remain pending. |
| Learned-skill perk browser | Provisional | Provisional | Left-click opens saved-registry perks grouped by Basic, Advanced and Expert, with names, existing icons, learned state and implementation status. Right-click uses shared perk help and the parent-skill component. Native client build and focused source guard pass; rendered layout, long names and pointer interaction remain unverified. Reuses the existing leather dialog and perk art; fallback icons remain Not done in their individual inventory entries. |
| Six custom-school bookmark pairs | Provisional | Final | The user has explicitly classified the six schools' selected/unselected bookmark art as Final. All six descriptor bindings exist in [mod.json](../Mods/new-horizons/mod.json). [CSpellWindow.cpp](../client/windows/CSpellWindow.cpp) uses frames 0/1 and aspect-preserving 68×51 compact rendering for six schools in the small book. Runtime layout and state behavior still lack a recorded GUI acceptance pass. |
| Six school headers and spell-mastery borders | Provisional | Provisional | All six headers and four-frame border descriptors are bound in mod.json. The header is page-one-only and borders follow school proficiency 0–3. User approval of these assets is not recorded. Review header transparency, all four corners/levels, spell overlap, and book layouts. |
| School emblems and school button states | Not done | Provisional | Image files/descriptors exist under Mods/new-horizons/Images, but SpellSchoolHandler currently reads only schoolBorders, schoolBookmark, and schoolHeader. No live consumer for the additional emblem/button family was found in the current school config/code. Keep these outputs provisional and do not count them as integrated school UI. |
| NH-authored secondary-skill rank families (17) | Provisional | Provisional | All 17 families have live image slots. Metamagic's rank family is explicitly Provisional. The custom families have no recorded final visual approval; verify the full rank/size grid in product UI. |
| Exact classic secondary-skill art (10) | Provisional | Final | Ten retained skill identities use matching original rank art under the authorized original-art reuse policy. Runtime layout remains unreviewed. |
| Reworked/renamed classic rank bindings (4) | Provisional | Provisional | War Machines, Discipline, Command and Spellcraft borrow original frames, but semantic fit and user visual approval are not established; review or replace the bindings. |
| Existing core spell icons | Provisional | Final | The 63 live core combat-school spell IDs retain their matching classic spell identity/art; the five Neutral Adventure Spells are inventoried separately below. The user-authorized art policy allows suitable exact original art to be referenced in place. The in-game presentation remains unreviewed. |
| Five Neutral Adventure spell icons | Provisional | Final | Summon Boat, Water Walk, Town Portal, Fly, and Dimension Door are registered at fixed Mage Guild levels and retain their exact original spell graphics by identity. Verify the guild/book and once-per-day presentation in GUI. |
| Five New Horizons spells using Magic Arrow frame 15 | Provisional | Not done | Counterspell, Disintegrate, Focus Magic, Phantom Army and Time Stop use the Magic Arrow icon frame for book/scroll/bonus art. Focus Magic is live with provisional status feedback; Time Stop's live binding is verified independently below. Purpose-made spell art remains missing. |
| Two New Horizons spells reusing related core frames | Provisional | Provisional | Master Chain Lightning reuses Chain Lightning frame 19 and Transfigure Matter reuses Remove Obstacle frame 64. Both await visual approval. |
| Purpose-made active perk icon families (68) | Provisional | Provisional | Every currently active perk has a distinct named art binding and four-state runtime descriptor. The newest HoMM3 Art workflow set closes Encirclement, Pursuit, Cleave, Arcane Acquisition, revised Spell Buffer, Stormcaller, Intelligence, Illusionist, Spellbinder, Logistics, Recruitment, and Demonic Gating. Cleave's original source, prompt, 1254×1254 master, 44×44/32×32 reductions, comparison sheet, and four provisional 44×44 runtime states are retained at `assets/new-horizons/art-source/cleave-v1/`; in-game review and user approval remain outstanding. Pursuit uses its selected v2 retained master and provisional runtime states under `assets/new-horizons/art-source/pursuit-v2/`. No final visual approval is implied. |
| Active perk icon gaps (0) | Provisional | Provisional | The active-perk source/runtime completeness and uniqueness gate passes. Future perks must add purpose-made art before becoming active; planned perks remain outside this count. |
| Planned perk definitions (242) | Not done | Not done | All remaining planned entries are inventoried individually. They are not active features and have no role-specific runtime binding; neutral fallback is not artwork. Reassess remaining art and binding as each effect moves to active. |
| Eight Order icons and action controls | Provisional | Provisional | Eight action-window descriptors and order icons are present and referenced by BattleHeroActionWindow.cpp. No user-final art approval or recorded in-game review is present. |
| Hero attributes, capabilities, growth/mastery and category icons | Provisional | Provisional | NH images are present and bound from hero/growth/town UI source. Their actual rendered layouts have not been accepted in a GUI pass. Leadership, Siege and Movement are called out in the CSV. |
| Split/transfer dialog owner indicators | Provisional | Final | [GUIClasses.cpp](../client/windows/GUIClasses.cpp) places built-in `PortraitsLarge` hero portraits and `CREST58` player crests below both creature panels; bounded name fallbacks cover missing or ambiguous markers, with full text available on hover/right-click. The generated 298×440 frame in [AssetGenerator.cpp](../client/render/AssetGenerator.cpp) preserves the installed `GPUCRDIV` top, side rails and ornate lower edge while extending the leather panel; it is player-colored and expands/recenters the window. The tallest fallback-layout button ends at y391, with the lower frame beginning at y403. Source guard checks containment and fallback bounds; actual game-scale rendering and interaction remain unverified. No extracted artwork is included. |
| Buffer spell-points UI | Provisional | Not done | [SpellPointPresentation.h](../client/windows/SpellPointPresentation.h) supplies total/maximum, Buffer annotation and explanatory tooltips. The earlier shared yellow-markup fix was promoted. UP-002 corrects the separate adventure/town compact cards: two bounded lines, yellow Buffer, exact right-click help and actual capacity for accessible heroes only. Enemy Visions retains hidden capacity. Client build and seven focused native tests pass. This follow-up is not promoted; rendered legibility checks remain pending. The battle sidebar is a separate consumer. Sparkle artwork is not bound; a preview mockup is not a runtime asset. |
| Fortress/Conflux ranked recruitment presentation | Provisional | Provisional | [CCastleInterface.cpp](../client/windows/CCastleInterface.cpp) contains ranked recruitment cards and a contained-portrait path for compact Conflux cards. That is source evidence only; no GUI acceptance is recorded. The 14-row Conflux category mapping remains a proposal and Firebird's Champion category is explicitly provisional in [NH_CONTENT_ACCEPTANCE.md](NH_CONTENT_ACCEPTANCE.md). The town-card composition and roster/category presentation still need review. |
| Tower construction hall relocated cards | Provisional | Provisional | Replaces the vanilla bitmap's baked card area with referenced leather and per-card frames for the New Horizons Tower hall. This addresses the mismatch between relocated Library/dwelling cards and old background slots. Title/footer and other towns remain unchanged. Source guards and independent review pass; the newer Windows build and graphical verification remain pending. |
| Extra Mage Guild levels for Castle, Stronghold and Fortress | Provisional | Provisional | Owner-supplied source packages are retained under `assets/new-horizons/Mage Guilds/`; live Castle V, Fortress IV/V and Stronghold I–V hall/town bindings are integrated. Focused asset tests pass. Rights remain with their owners, and actual faction-screen placement and animation still require visual acceptance. |
| Quick-save/load controls and creature-status icons | Provisional | Provisional | The mod contains button states and ten new status icons under Mods/new-horizons/Images. These are user-authorized UI additions but no final-art or graphical acceptance evidence is recorded here. See [NH_USER_FEEDBACK.md](NH_USER_FEEDBACK.md) and the source files linked by the CSV. |

## UI surface coverage

The companion CSV has grouped rows for the main New Horizons surfaces requested for review: level-up skill/perk selection; the hero skill-odds pane; the parent-skill context shown by perk help; creature Speed, Initiative, Leadership requirement/capacity and stack-size readout; Orders selection/background/read-only tooltips; hero active effects and the typed Hero/Spell/Order action panel; battle log; Demonic Reserve and Gate indicators; and the main spellbook. Those implementation classifications are source-only, Provisional pending rendered review unless specifically marked Not done. The parent-skill help currently provides a text identity, not a separate custom parent-skill icon binding.

Legacy mastery/artillery UI references are not counted as live New Horizons 1.0 surfaces: [NH_VERSION_1_0_SCOPE.md](NH_VERSION_1_0_SCOPE.md) supersedes the earlier post-Expert mastery organization and scopes Siege as a rating, not a spendable Artillery resource. Existing historical code or files are not evidence of a requested live feature.

## Verified spellbook bindings

### Pending replacement-art integration

The table distinguishes installed source bindings from still-uninstalled
HoMM3-skill drafts. Existing live Provisional icons must not be mistaken for
the replacements that remain Not done.
The `output/homm3/` paths are local working outputs, not shipped assets.

| Requested replacement | Implementation | Art | Local evidence and remaining work |
| --- | --- | --- | --- |
| Metamagic Basic/Advanced/Expert | Provisional | Provisional | Canonical skill data selects the twelve `NH_metamagic_prism_*` assets. Basic/Advanced reuse the HoMM3-skill rank paintings; Expert has a targeted opaque-leather background repair. Masters, prompts, hashes and slot details are retained under `assets/new-horizons/art-source/metamagic-prisms-v2/`. Native32/44/82/58 reductions preserve shape; rectangular slots add transparent margins rather than stretching. Basic's slight original translucency is disclosed. In-game review and final user approval remain pending. |
| Hero Movement | Provisional | Provisional | The HoMM3-skill winged riding boot is bound as `NH_hero_movement_44` in the native 44px attribute slot and `NH_hero_movement_painted_32` in the growth window. Retained master, exact prompt and export hashes are under `assets/new-horizons/art-source/hero-attribute-replacements-v1/`. In-game visual acceptance remains pending. |
| Hero Leadership | Provisional | Provisional | The HoMM3-skill command banner/gauntlet is bound as `NH_hero_leadership_44` in the main attribute pane and `NH_hero_leadership_24` in the compact legacy layout. The older `NH_capability_leadership` family has no current runtime consumer; creature and Fort Leadership use the separate crown below. Native exports are inspected; in-game visual acceptance remains pending. |
| Creature Leadership crown | Provisional | Provisional | The retained HoMM3-skill crown is bound as `NH_creature_leadership_20` in both the creature Leadership Cost row and ranked Fort cards. Source master, exact prompt and hashes are under `assets/new-horizons/art-source/creature-stat-glyphs-v1/`. Native export checks pass; in-game presentation remains unverified. |
| Creature rank staircase and stat row | Provisional | Provisional | The v2 stair-step glyph is bound as `NH_creature_rank_20` to a horizontal MainSection Rank row instead of a separate category section. Generated panels allocate the extra row only for categorized creatures; legacy uncategorized geometry is unchanged. Fort and recruitment surfaces intentionally use text headings/badges rather than this glyph. Source and art remain provisional pending rendered review. |
| Skill-probability information control | Provisional | Provisional | `HeroSkillOddsInfoMark` code-draws a transparent gold circled serif `i` over the four-state `NH_hero_growth_entry` control; it has no separate raster master or export. Left-click opens the skill-odds pane. Source placement checks pass, but centering, contrast, scaling, click area and tooltip behavior remain pending rendered/input review. |

Installation requires recorded provenance, retained source art, correctly sized
runtime exports, binding checks, and rendered review. Draft generation alone
does not complete any of these replacement requests.

The creature rank/crown integration passed the Linux `vcmiclient` build,
three glyph provenance/export tests, the creature rank and Initiative source
guards, and the recruitment-category source guard. Independent review found
no blocking issues. Source `df7e2cae6` was rebuilt with its committed identity,
frozen and promoted as snapshot
`ca6afa103919effdcb38e63d22105200d744cf6ac963732d5b3368977b28c8a0`.
The exact candidate completed 74 AI turns in a bounded 45-second headless
All for One smoke run (configured seed 1284510375; maximum completed turn
3823ms), with no checked crash, unsupported-rules or command-rejection errors.
The run ended at the planned timeout and left no client process. The normal
launcher's verify-only check passed. This confirms candidate startup/AI play,
not the creature panel's rendered appearance; GUI acceptance remains pending.

The hero Movement/Leadership replacement integration passed the Linux client
build, the focused hero-attribute binding/dimension guard, two master/export
provenance tests, and independent review. The broader level-up/art audit still
now passes its active-perk completeness gate: all 66 active perk IDs have named,
unique normal art and complete four-state descriptors. These
hero replacement assets remain provisional pending in-game visual review.
Source `359c0b509` was rebuilt, frozen and promoted as snapshot
`3ddeb77330ced93d2b8fb981fbe318068271ac7c100e7ca6f421e788da1d28b1`.
Its bounded 35-second headless All for One run completed 57 AI turns
(configured seed 1284510375; maximum completed turn 3960ms), with no checked
crash, unsupported-rules or command-rejection errors. Planned timeout completed
without a remaining client process; launcher verify-only passed. This is
startup/AI smoke evidence, not visual acceptance of the new icon bindings.

## Metamagic prism integration validation

The three rank paintings now supply all twelve skill image slots and the Basic
specialty images for Halon and Serena. The focused content, faction-art,
skill-entity, binding and pixel-export suite passes 39 tests; module regeneration
check and independent review pass. Square artwork is not stretched to fill the
rectangular slots. Expert's missing background is repaired; Basic retains its
original slight translucency.

The broader `nh-new-art-audit.py` still rejects unrelated unclassified PNG
families at its SVG/PNG inventory comparison. Its exact twelve-file prism
inventory check passes; this change does not relax the remaining inventory
requirement. Build, snapshot promotion and in-game visual acceptance are not
implied by these source checks. The artwork remains Provisional.

Source `e03005ae9` subsequently built successfully and was promoted as snapshot
`184dec2d8cb728229089ec477894a435e37e937ffdc545aa2ae413f39f07b3ad`.
The exact frozen candidate completed 57 AI turns in a 35-second isolated
headless check (configured seed 1284510375, maximum completed turn 3878ms).
No checked crash, unsupported-rule or command-rejection errors appeared;
the planned timeout left no client process. AI path-node allocation warnings
remain, so this is not a general performance clearance or visual acceptance.
The normal launcher's verify-only path check passes for this snapshot.

## Spellbook binding details

### Specialty and Fort binding correction

Hero `specialtyLarge` images feed the 44×44 `UN44` atlas, not the 82×93
secondary-skill slot. All 21 authored New Horizons specialty bindings now use
their native 44×44 medium artwork; the four Necromancy specialties use the
corresponding classic `SECSKILL` frame. Skill-window large images are unchanged.
Fort Leadership Cost uses the same monochrome crown as the creature window,
retaining its existing row geometry. The 27 focused content/binding tests and
Fort source guard pass; independent review found no blockers. Native build and
playable promotion are tracked separately; these checks are not rendered acceptance.

The current mod.json binds, for each of Light, Nature, Sorcery, Havoc, Shadow, and Chaos:

- NH_<school>_bookmark.json, with selected/unselected frames.
- NH_<school>_header.png, a 160×96 page-one illustration, not a full-book splash.
- NH_<school>_spellBorders.json, four spell mastery frames: none/basic/advanced/expert.

The old school-art completeness note documented an earlier Sorcery border reuse. The current live mod.json now binds all six custom border descriptors; use the current source for this inventory. The additional emblem/button outputs described by the school-family specification are not registered in SpellSchoolHandler today.

Time Stop is in the active roster at Mods/new-horizons/mod.json under settings.magic.newHorizons.spells. Its current definition in Mods/new-horizons/Content/config/spells/newHorizons.json sets iconBook to SPELLS.def:0:15 (and matching spellbook/bonus icon frames). config/spells/offensive.json identifies Magic Arrow as spell index 15, and lib/constants/EntityIdentifiers.h assigns MAGIC_ARROW = 15. This verifies the current borrowed binding; it does not assert a live screenshot or any other runtime behavior. Counterspell, Disintegrate, Focus Magic, and Phantom Army also use frame 15. A dedicated, role-appropriate icon is still required for all five spells.

The combat-spell settings contain 71 entries, of which 70 are active after excluding inactive `core:clone`. The other new spell icon slots are source-confirmed: Master Chain Lightning uses frame 19, matching the original Chain Lightning index; Transfigure Matter uses frame 64, matching Remove Obstacle. Both remain Provisional as New Horizons spell art until visually approved. The present register does not treat the unregistered Spell Lock or Magic Missile definitions as live spells.

Perk and skill manifests preserve source files, runtime hashes, and export bindings. Relevant files include [NH_skill_art_manifest.json](../assets/new-horizons/art-source/NH_skill_art_manifest.json), [faction_skill_icon_manifest.json](../assets/new-horizons/art-source/skill-icons/faction_skill_icon_manifest.json), and the runtime manifests for [active-perks-v1](../assets/new-horizons/art-source/active-perks-v1/runtime-manifest.json), [active-perks-v2](../assets/new-horizons/art-source/active-perks-v2/runtime-manifest.json), [active-perks-v3](../assets/new-horizons/art-source/active-perks-v3/runtime-manifest.json), [active-perks-v4](../assets/new-horizons/art-source/active-perks-v4/runtime-manifest.json), [active-perks-v5](../assets/new-horizons/art-source/active-perks-v5/runtime-manifest.json), [Sylvan Luck v1](../assets/new-horizons/art-source/sylvan-luck-perks-v1/runtime-manifest.json), [Sylvan Luck v2](../assets/new-horizons/art-source/sylvan-luck-perks-v2/runtime-manifest.json), [Warcasting](../assets/new-horizons/art-source/warcasting-perks-v1/runtime-manifest.json), [Battle Meditation](../assets/new-horizons/art-source/battle-meditation-v1/runtime-manifest.json), and [Chain Gate](../assets/new-horizons/art-source/chain-gate-v1/runtime-manifest.json). These are technical/provenance records; they do not imply visual approval. The eight per-Order art manifests are under [orders-v1](../assets/new-horizons/art-source/orders-v1/).

## Maintaining this register

Update the CSV from these sources when bindings or active status change:

- Spells and school assignments: Mods/new-horizons/mod.json; spell graphics: Mods/new-horizons/Content/config/spells/newHorizons.json and config/spells/*.json.
- Skill/rank bindings: Mods/new-horizons/mod.json; skill/perk catalogue and active/planned effect status: config/newHorizonsPerks.json.
- Perk icon lookup: client/windows/NewHorizonsPerkIcons.h; authored perk runtime files/manifests under assets/new-horizons/art-source/.
- Orders: Mods/new-horizons/mod.json and client/battle/BattleHeroActionWindow.cpp; outputs under Mods/new-horizons/Images and assets/new-horizons/art-source/orders-v1/.
- Spellbook tabs/borders/header: Mods/new-horizons/mod.json, lib/spells/SpellSchoolHandler.cpp, and client/windows/CSpellWindow.cpp.
- Hero/recruitment/convenience UI: source links recorded in the corresponding CSV rows.

This register is documentation only; it creates no artwork. Any future new or revised HoMM3 raster art, including provisional concepts, must use the `$homm3-art` skill and follow [NH_APPROVED_ART_WORKFLOW.md](NH_APPROVED_ART_WORKFLOW.md); the workflow's process approval is not blanket asset approval.

Keep the three status values exact. Record implementation evidence separately from art approval. Preserve existing Final classifications when unrelated art families change.
