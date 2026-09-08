# Role-specific art delivery contracts

## User clarification —2026-09-08

The automatic art workflow must deliver the correct family for each entity:
secondary-skill ranks, spell icons, school bookmarks and the selected-school
illustration have different requirements. Do not treat all requests as one square
icon. The user provides artistic intent; the agent derives technical deliverables
from actual product bindings. No manual masking/setup from the user.

The external HoMM3-art workspace now has ASSET_SPECIFICATIONS.md, linked from
ART_WORKFLOW.md and AGENTS.md. It expands short requests into a per-request
manifest. This document preserves the product-side source of that contract.
See [NH_APPROVED_ART_WORKFLOW.md](NH_APPROVED_ART_WORKFLOW.md) for generation/QA.

## Verified current Sorcery skill/school slots

Existing custom PNG dimensions were checked in `Mods/new-horizons/Images/`,
with runtime notes in [NH_ARTIST_SORCERY.md](NH_ARTIST_SORCERY.md). The newly
identified spell-border dimensions instead come from private original DEF-frame
inventory and source bindings, not existing custom border PNGs; see
[NH_SCHOOL_ART_COMPLETENESS.md](NH_SCHOOL_ART_COMPLETENESS.md).

| Role | Family | Native contract |
|---|---|---|
| Secondary skill |Basic/Advanced/Expert x4 sizes =12|small32x32, medium44x44, large82x93, scenarioBonus58x64|
| Spell mastery borders |none/basic/advanced/expert =4|78x65 RGBA, frames0..3; corner progression; currently reuses SplevA|
| School bookmarks |selected/unselected =2|80x60 RGBA; also inspect actual compact68x51 render; frames0/1 respectively|
| Selected-school header |1|160x96 RGBA, top28 transparent, visible160x68|
| School emblem |1|64x64 RGBA|
| School button states |normal/pressed/disabled/highlighted =4|64x64 RGBA, preserve descriptor bindings/order|

Complete school plus associated skill:24 images, excluding individual spells.
The earlier20 count omitted four spell mastery borders; see
[NH_SCHOOL_ART_COMPLETENESS.md](NH_SCHOOL_ART_COMPLETENESS.md) for actual bindings,
private original-frame evidence, corner ordering and remaining integration gates. Skill-only:12 images. Names remain NH_sorceryMagic_<rank>_<size>.png and
NH_sorcery_<component>.png as currently configured. User terminology Arcane is not
authorization to rename internal sorcery identifiers.

Ranks must have meaningful recognizable progression within one visual identity,
not three identical paintings with tiny badges or hue changes. Review every rank
at32px as well as larger sizes. Use role-specific compositions: the selected-school
"splash" is the existing wide header, NOT a new full-book layout or stretched skill
icon. Selected/unselected bookmark states are not generic button states. Original
SPELTAB83x294 strips cannot be imported as independent custom bookmarks.

## User-directed magic-skill rank grammar

Magic-school secondary skills use **Basic = scroll; Advanced = book (open or
closed); Expert = orb** as the shared physical rank progression. Schools retain
distinct effects, palette and supporting materials. This default does not apply
to every nonmagic skill, individual spell, bookmark or header. Preserve rank
silhouettes at32px; do not obscure them with effects. Record this as the user's
art direction, not an independently verified universal historical-image claim.

For the next Sorcery/Arcane skill family, user accepted pure magic represented
by blue aetheric flow: partly unfurled scroll with a small current; open book with
a controlled arc; blue orb in restrained aged brass with circulating aether. Use
pale highlights/subtle violet depth and warm parchment/brass; avoid neon cyan,
lightning/weather imagery and confusion with Air Magic. This supersedes spindle/
armillary motif exploration for this request, not the proven RGBA workflow.
Concept direction is accepted; actual generated ranks and their12 native outputs
still require user review. No live resource rename or import is authorized here.

## Spell graphics: separate role set

Evidence: `config/schemas/spell.json` graphics fields and conditional required
lists; `lib/spells/CSpellHandler.cpp` graphics loading; `CSpell::registerIcons`
in `lib/spells/CSpell.cpp`.

- iconBook -> SPELLS: spellbook illustration. Pinned blank template67x48.
- iconScroll -> SPELLSCR: scroll art. Pinned blank83x61; small43x34 is a separate
  candidate only where an actual consumer requires it.
- iconEffect -> SPELLINT: battle effect/status icon. Pinned blank48x36.
- iconScenarioBonus -> SPELLBON: schema explicitly58x64; matching bonus template.
- iconImmune: schema's separate spell-immunity bonus icon. Not another frame in
  CSpell::registerIcons; current consumer dimensions/margins must be inspected.

Template dimensions are verified inventory facts, not proof of every runtime
layout. Before final spell exports Frontend must confirm actual consumer placement,
required fields for that spell type and any additional variants. Do not silently
invent an immunity-icon size. Do not invent Basic/Advanced/Expert spell paintings
merely because gameplay effects scale with mastery. Cast animations/projectiles/
cursors/audio have separate contracts and are not completed by static icons.

## Request expansion, staging and manifest

"Create Sorcery secondary skill" means12 skill outputs. "Create Sorcery school
art" means the corrected24-image school/skill set; individual spells are separate.
"All school art including spells" adds the currently configured spell roster,
not a guessed spell list. Specific bookmark/header requests remain component-only.
Other orders/doctrines/creature-ability/menu/portrait roles require their own
consumer-backed slot manifest, not the skill template by default.

Create a manifest before generation: entity ID/scope; role; rank/state; dimensions;
resource name/consumer; template/hash; alpha/margins; source concept; placement;
expected review route and status. Mark planned/draft/technical-pass/user-approved/
integrated/in-game-verified separately. Unknown bindings cannot be marked complete.
No actual manifest/output is claimed created by this documentation update.

Approve one representative visual direction before expanding a complete coherent
family. The staged concept gate does not narrow or close the full requested scope.
Review a rank-by-size grid, both bookmark states at native/compact sizes, the header
with its transparent reserved region, button-state strip and spell-role rows.
Check missing/duplicate/wrong-size items, frame order, margins and actual rank/state
legibility. The agent handles technical work; only real artistic ambiguity needs
a user question.

Artist owns isolated production; Frontend confirms bindings/integrates approved
art; Content checks independent evidence and selected-file rights; Build commits/
packages. No asset import, runtime rename, full-family approval or template-rights
clearance follows from this specification. Product development stays independent.
