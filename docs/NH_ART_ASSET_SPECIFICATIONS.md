# Consumer-backed runtime-art contracts

Derive exports from actual product bindings, not from a generic square-icon request.
Generation/review procedure belongs in
[NH_APPROVED_ART_WORKFLOW.md](NH_APPROVED_ART_WORKFLOW.md); visual construction
follows [Heroes III UI style](NH_HOMM3_UI_STYLE_GUIDE.md).
No external plugin or workspace is a prerequisite for the contract.

## Skill and school families

The verified school/associated-Skill family contains 24 role images:

| Role | Family | Native resource contract |
| --- | --- | --- |
| Secondary Skill | Basic/Advanced/Expert × four sizes | 32×32, 44×44, 82×93, scenario 58×64 |
| Spell mastery border | None/Basic/Advanced/Expert | 78×65 RGBA, ordered frames 0–3 |
| School bookmark | Selected/unselected | 80×60 RGBA; also inspect compact 68×51 render |
| Selected-school header | One | 160×96, top 28 transparent, visible 160×68 |
| School emblem | One | 64×64 |
| School button | Normal/pressed/disabled/highlighted | 64×64, descriptor order preserved |

Skill-only is twelve outputs; earlier twenty-image school counts omitted borders.
Inspect current bindings in [school completeness](NH_SCHOOL_ART_COMPLETENESS.md)
before changing a family. Spell paintings are additional roles, not rank duplicates.
Magic Skills use the selected physical grammar Basic scroll, Advanced book,
Expert orb, with recognizable silhouette progression at 32px. Other Skills and
spell/bookmark/header roles do not inherit that grammar automatically.
Sorcery's selected aether identity uses restrained blue flow, parchment and brass,
not weather/lightning imagery or neon cyan; internal sorcery IDs are not renamed
merely because an artistic brief calls the school Arcane.

## Spells, statuses and actual dimensions

Trace `config/schemas/spell.json`, `lib/spells/CSpellHandler.cpp`,
`CSpell::registerIcons` in `lib/spells/CSpell.cpp` and the actual client consumer.
Fields route separately to spellbook, scroll, status/effect, scenario and immunity
roles. Historical templates include book 67×48, scroll 83×61, effect 48×36 and
scenario 58×64, but template/schema canvas sizes are not every consumer's rendered
icon size. Current selected purpose-made families use book44, scroll/scenario32,
effect/immunity30 where the actual consumer requires them. Verify before exporting.

Internal Arcane Breach and Lucidity statuses require effect/immunity icons, not
invented castable book/scroll entries. Cast animations, projectiles, cursors and
audio are independent contracts; static PNG roles do not complete them.
Do not stretch an opaque painting into a transparent emblem or silently invent
an immunity dimension.

## Selection, provenance and acceptance

Manifest each entity/role/rank/state with exact resource mapping, dimensions,
alpha/margins, source concept/template provenance and review status.
Selection, origin, approval and integration/rendered verification are distinct.
Current authority permits suitable provisional purpose-made drafts in game;
Final requires approval evidence. An unrelated borrowed icon remains Not done.

Review native and enlarged families for wrong/missing/duplicate roles, state order,
rank readability, contrast, transparent edges and central-subject clipping.
Transparent corners alone do not prove clear edges or an inset margin.
Confirm actual parchment/dark-status backgrounds and compact scaling.

Selected runtime art, including selected Cabirs, Magi, Guild composites and custom
interface edits, is in NewHorizons.nhart. Preserve local originals/alternatives
and complete inputs; do not publish loose runtime images or private paths.
Packaging does not waive provenance/licensing obligations for original-based art.
Ordinary builds consume the committed pack, not incomplete private inputs.
See [content development](developers/NewHorizonsContent.md) and
[asset status](NH_UI_ASSET_STATUS.md).
