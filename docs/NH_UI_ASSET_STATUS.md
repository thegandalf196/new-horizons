# New Horizons UI and asset status register

Last audited: 2026-09-24

This is the maintained UI/art status index for the current New Horizons working tree. It records live bindings and remaining visual/UI work. It does not replace gameplay specs, source configs, runtime manifests, or acceptance records. The row-level index is [NH_UI_ASSET_INVENTORY.csv](NH_UI_ASSET_INVENTORY.csv).

The audit used canonical registries and current source bindings rather than counting files. It covers 69 active combat-school spells across the six New Horizons schools plus the five Neutral Adventure Spells, all 31 registered secondary skills, all 310 perk definitions (60 active and 250 planned), all eight Order bindings, and the scoped hero, spellbook, convenience, creature-info, and ranked-recruitment UI surfaces below. Active means enabled in source configuration, not necessarily promoted to the playable build. The inactive Clone roster entry is excluded. The unregistered Magic Missile and Spell Lock definitions are noted separately as non-live.

This is a binding inventory, not a full visual audit or a product completion claim. No game was launched and no GUI review was performed for this register. A resource path, generated manifest, native-size file, or implemented code path does not by itself establish final art or accepted UI.

## Status meanings

Every row has separate Implementation and Art columns. Each column uses only these three values:

- Not done — the live UI behavior/binding is absent, or the artwork is missing or a mismatched stand-in.
- Provisional — a source binding or usable art exists, but visual approval or the remaining integration/layout check is not evidenced.
- Final — this item has explicit user art approval, or uses an exact, suitable classic Heroes III art binding under the user-authorized reuse policy in [NEW_HORIZONS_MVP.md](NEW_HORIZONS_MVP.md). Final art does not mean the UI integration has passed graphical acceptance.

Implementation is Final only when the intended UI behavior and its presentation have been accepted. The CSV's Remaining field explains each classification and points to current sources.

## Current status summary

| Component | Implementation | Art | Evidence and remaining work |
| --- | --- | --- | --- |
| Learned-skill perk browser | Provisional | Provisional | Left-click opens saved-registry perks grouped by Basic, Advanced and Expert, with names, existing icons, learned state and implementation status. Right-click uses shared perk help and the parent-skill component. Native client build and focused source guard pass; rendered layout, long names and pointer interaction remain unverified. Reuses the existing leather dialog and perk art; fallback icons remain Not done in their individual inventory entries. |
| Six custom-school bookmark pairs | Provisional | Final | The user has explicitly classified the six schools' selected/unselected bookmark art as Final. All six descriptor bindings exist in [mod.json](../Mods/new-horizons/mod.json). [CSpellWindow.cpp](../client/windows/CSpellWindow.cpp) uses frames 0/1 and aspect-preserving 68×51 compact rendering for six schools in the small book. Runtime layout and state behavior still lack a recorded GUI acceptance pass. |
| Six school headers and spell-mastery borders | Provisional | Provisional | All six headers and four-frame border descriptors are bound in mod.json. The header is page-one-only and borders follow school proficiency 0–3. User approval of these assets is not recorded. Review header transparency, all four corners/levels, spell overlap, and book layouts. |
| School emblems and school button states | Not done | Provisional | Image files/descriptors exist under Mods/new-horizons/Images, but SpellSchoolHandler currently reads only schoolBorders, schoolBookmark, and schoolHeader. No live consumer for the additional emblem/button family was found in the current school config/code. Keep these outputs provisional and do not count them as integrated school UI. |
| NH-authored secondary-skill rank families (17) | Provisional | Provisional | All 17 families have live image slots. Metamagic's rank family is explicitly Provisional. The custom families have no recorded final visual approval; verify the full rank/size grid in product UI. |
| Exact classic secondary-skill art (10) | Provisional | Final | Ten retained skill identities use matching original rank art under the authorized original-art reuse policy. Runtime layout remains unreviewed. |
| Reworked/renamed classic rank bindings (4) | Provisional | Provisional | War Machines, Discipline, Command and Spellcraft borrow original frames, but semantic fit and user visual approval are not established; review or replace the bindings. |
| Existing core spell icons | Provisional | Final | The 63 live core combat-school spell IDs retain their matching classic spell identity/art; the five Neutral Adventure Spells are inventoried separately below. The user-authorized art policy allows suitable exact original art to be referenced in place. The in-game presentation remains unreviewed. |
| Five Neutral Adventure spell icons | Provisional | Final | Summon Boat, Water Walk, Town Portal, Fly, and Dimension Door are registered at fixed Mage Guild levels and retain their exact original spell graphics by identity. Verify the guild/book and once-per-day presentation in GUI. |
| Four New Horizons spells using Magic Arrow frame 15 | Provisional | Not done | Counterspell, Disintegrate, Phantom Army and Time Stop use the Magic Arrow icon frame for book/scroll/bonus art. Time Stop's live binding is verified independently below. |
| Two New Horizons spells reusing related core frames | Provisional | Provisional | Master Chain Lightning reuses Chain Lightning frame 19 and Transfigure Matter reuses Remove Obstacle frame 64. Both await visual approval. |
| Distinct active perk icon families (44) | Provisional | Provisional | These active perks have distinct named art bindings and runtime state descriptors; no final visual approval is recorded. |
| Active perk icon gaps (16) | Provisional | Not done | Fifteen active perks, including the newly enabled Intelligence, resolve to the neutral fallback; Stormcaller uses the Havoc skill glyph. Bind role-specific art and state frames. |
| Planned perk definitions (250) | Not done | Not done | All planned entries are inventoried individually. They are not active features and have no role-specific runtime binding; neutral fallback is not artwork. Reassess as each effect moves to active. |
| Eight Order icons and action controls | Provisional | Provisional | Eight action-window descriptors and order icons are present and referenced by BattleHeroActionWindow.cpp. No user-final art approval or recorded in-game review is present. |
| Hero attributes, capabilities, growth/mastery and category icons | Provisional | Provisional | NH images are present and bound from hero/growth/town UI source. Their actual rendered layouts have not been accepted in a GUI pass. Leadership, Siege and Movement are called out in the CSV. |
| Buffer spell-points UI | Provisional | Not done | [SpellPointPresentation.h](../client/windows/SpellPointPresentation.h) supplies total/maximum, Buffer annotation and explanatory tooltips. The user reported literal colour markup in the compact hero readout and approved yellow text. The shared formatter now uses ordinary yellow emphasis; this follow-up has compiled, passed its native presentation tests, and been promoted. Rendered confirmation remains pending. Separate narrow-panel Buffer labels retain their blue colour. Long-value clipping remains unverified; hidden maximum Mana is not queried. The requested sparkle artwork is not bound; a preview mockup is not a runtime asset. |
| Fortress/Conflux ranked recruitment presentation | Provisional | Provisional | [CCastleInterface.cpp](../client/windows/CCastleInterface.cpp) contains ranked recruitment cards and a contained-portrait path for compact Conflux cards. That is source evidence only; no GUI acceptance is recorded. The 14-row Conflux category mapping remains a proposal and Firebird's Champion category is explicitly provisional in [NH_CONTENT_ACCEPTANCE.md](NH_CONTENT_ACCEPTANCE.md). The town-card composition and roster/category presentation still need review. |
| Extra Mage Guild levels for Castle, Stronghold and Fortress | Provisional | Provisional | Current mod.json describes reuse of the existing final town-screen guild structure as a presentation placeholder for newly added levels. Confirm the actual faction screens and labels during UI acceptance; do not treat the placeholder as final presentation. |
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
| Metamagic Basic/Advanced/Expert | Not done | Provisional | `output/homm3/metamagic-v1/` contains separate rank masters and 32/44px exports. Resolve the reported Expert-background concern, prepare every runtime size, and replace the existing `NH_metamagic_*` bindings only after checking exports. |
| Hero Movement | Not done | Provisional | `output/homm3/hero-movement-v1/` contains master and 20/32/44px exports. The hero window still uses `NH_hero_movement_32`. |
| Hero Leadership | Not done | Provisional | `output/homm3/hero-leadership-v1/` contains master and 20/32/44px exports. The hero window still uses the older `NH_capability_leadership` family. |
| Creature Leadership crown | Provisional | Provisional | The retained HoMM3-skill crown is bound as `NH_creature_leadership_20` in the creature Leadership Cost row. Source master, exact prompt and hashes are under `assets/new-horizons/art-source/creature-stat-glyphs-v1/`. Native export checks pass; in-game presentation remains unverified. |
| Creature rank staircase and stat row | Provisional | Provisional | The v2 stair-step glyph is bound as `NH_creature_rank_20` to a horizontal MainSection Rank row instead of a separate category section. Generated panels allocate the extra row only for categorized creatures; legacy uncategorized geometry is unchanged. Source and art remain provisional pending rendered review. |

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

## Spellbook binding details

The current mod.json binds, for each of Light, Nature, Sorcery, Havoc, Shadow, and Chaos:

- NH_<school>_bookmark.json, with selected/unselected frames.
- NH_<school>_header.png, a 160×96 page-one illustration, not a full-book splash.
- NH_<school>_spellBorders.json, four spell mastery frames: none/basic/advanced/expert.

The old school-art completeness note documented an earlier Sorcery border reuse. The current live mod.json now binds all six custom border descriptors; use the current source for this inventory. The additional emblem/button outputs described by the school-family specification are not registered in SpellSchoolHandler today.

Time Stop is in the active roster at Mods/new-horizons/mod.json under settings.magic.newHorizons.spells. Its current definition in Mods/new-horizons/Content/config/spells/newHorizons.json sets iconBook to SPELLS.def:0:15 (and matching spellbook/bonus icon frames). config/spells/offensive.json identifies Magic Arrow as spell index 15, and lib/constants/EntityIdentifiers.h assigns MAGIC_ARROW = 15. This verifies the current borrowed binding; it does not assert a live screenshot or any other runtime behavior. Counterspell, Disintegrate, and Phantom Army also use frame 15. A dedicated, role-appropriate icon is still required for those four spells.

The active spell list has 69 entries (70 total settings entries with inactive core:clone excluded). The other new spell icon slots are source-confirmed: Master Chain Lightning uses frame 19, matching the original Chain Lightning index; Transfigure Matter uses frame 64, matching Remove Obstacle. Both remain Provisional as New Horizons spell art until visually approved. The present register does not treat the unregistered Spell Lock or Magic Missile definitions as live spells.

Perk and skill manifests preserve source files, runtime hashes, and export bindings. Relevant files include [NH_skill_art_manifest.json](../assets/new-horizons/art-source/NH_skill_art_manifest.json), [faction_skill_icon_manifest.json](../assets/new-horizons/art-source/skill-icons/faction_skill_icon_manifest.json), and the runtime manifests for [active-perks-v1](../assets/new-horizons/art-source/active-perks-v1/runtime-manifest.json), [active-perks-v2](../assets/new-horizons/art-source/active-perks-v2/runtime-manifest.json), [active-perks-v3](../assets/new-horizons/art-source/active-perks-v3/runtime-manifest.json), [active-perks-v4](../assets/new-horizons/art-source/active-perks-v4/runtime-manifest.json), [Sylvan Luck v1](../assets/new-horizons/art-source/sylvan-luck-perks-v1/runtime-manifest.json), [Sylvan Luck v2](../assets/new-horizons/art-source/sylvan-luck-perks-v2/runtime-manifest.json), [Warcasting](../assets/new-horizons/art-source/warcasting-perks-v1/runtime-manifest.json), [Battle Meditation](../assets/new-horizons/art-source/battle-meditation-v1/runtime-manifest.json), and [Chain Gate](../assets/new-horizons/art-source/chain-gate-v1/runtime-manifest.json). These are technical/provenance records; they do not imply visual approval. The eight per-Order art manifests are under [orders-v1](../assets/new-horizons/art-source/orders-v1/).

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
