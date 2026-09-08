# School art completeness — border omission corrected

## Source/asset investigation,2026-09-08

User asked whether the remaining school art comprises mastery borders, bookmarks
and a selected-school splash. The earlier20-image inventory omitted the separate
school-specific spell mastery borders. Correct total for the defined school pack
is24 image outputs, excluding individual spells and JSON descriptors. This is a
checklist correction, not authorization to broaden the current12-skill-art request.
The full school family remains open; no output/import is asserted here.

## Actual bindings

- `Mods/new-horizons/mod.json`: Sorcery currently has schoolBorders="SplevA",
  schoolHeader="NH_sorcery_header.png", schoolBookmark="NH_sorcery_bookmark.json".
  Thus borders currently reuse original Air art, not original Arcane art.
- `lib/spells/SpellSchoolHandler.cpp`: loads schoolBorders as AnimationPath.
- `client/windows/CSpellWindow.cpp`, SpellArea::setSpell: chooses frame
  `schoolLevel` from getSpellSchoolLevel. All tab uses the effective selected
  school reported for that spell; a specific tab uses that tab's school border.
- Same source, setSchoolImages: custom bookmark frame0 selected/frame1 unselected;
  custom header at117+offL,74+offT on page0 only. Later pages have no school header,
  since it would cover spell slots. This is a school-header illustration/vignette,
  not a full-screen splash or a background repaint for every book page.

## Original border resource facts

Read-only private extraction of SpLevA/E/F/W.def from purchaser archives. Four
frames each, group0, full canvas78x65. Local inventory, hashes and native contact
sheet are under HoMM3-art/references/spell-school-borders/, never product Git.
The existing research DEF format1 decoder was reused; palette0 is transparent in
these diagnostic previews. This is offline/static evidence, not a native UI pass.

| Runtime frame | Proficiency | Decorated corners observed |
|---|---|---|
|0|No school skill|bottom-left|
|1|Basic|bottom-left +top-left|
|2|Advanced|bottom-left +top-left +top-right|
|3|Expert|all four, adding bottom-right|

The progression adds decorated CORNERS, not just more decoration in the same
corner. It is school proficiency0..3, distinct from New Horizons mastery talents.
Do not omit frame0 merely because the skill portraits have three ranks.

Supply four transparent78x65 overlays with the spell-image center unobstructed,
consistent native offsets and preserved corner regions. A shared authored corner
set can be assembled deterministically into four frames; it need not be four
independent image generations or simple mirrored copies. Use restrained Arcane
blue aether decoration consistent with the school, not copied original Air pixels.
Check overlays against light/dark spell artwork and all four proficiency states.

Proposed resource names are NH_sorcery_spellBorder_{none,basic,advanced,expert}.png
with NH_sorcery_spellBorders.json mapping group0 frames0..3. These are a suggested
future binding, not existing files. Frontend/Build must wire schoolBorders to the
reviewed descriptor; images alone will not replace the SplevA reuse.

## Complete requested school package

| Family | Image outputs |
|---|---:|
|Basic/Advanced/Expert skill x32/44/82x93/58x64|12|
|Spell mastery overlays78x65, none/basic/advanced/expert|4|
|School bookmarks80x60, selected/unselected|2|
|School header illustration160x96, top28 transparent|1|
|Existing school emblem64x64|1|
|Existing button states64x64, normal/pressed/disabled/highlighted|4|
|Total, excluding descriptors and spell-specific art|24|

After all12 skill images are complete,12 school outputs remain in the full pack.
The specifically named spellbook components are7 of those:4 borders +2 bookmark
frames +1 header. The other5 are the existing project's emblem/button family,
not five additional mandatory spellbook widgets. These assets can share approved
motifs and deterministic state transformations;24 files need not mean24 concepts.
Bookmarks also need compact68x51 review in the current six-school small book.

Individual spell illustrations/scroll/status/immunity/bonus graphics are separate
per-spell families. Casting animations, projectiles, cursors, sound, All-schools
button, page turns and generic book background are not new per-school deliverables
without a separate requirement. Do not copy the school emblem as every spell icon.

## Remaining acceptance

Complete manifest includes all frames/ranks/sizes and config descriptors; actual
alpha/corner/margin/native checks; original/user-owned foreground provenance;
selected-template rights/attribution classification; user visual approval;
Frontend/Build integration; independent in-game default/compact/layout/page and
proficiency/All-versus-selected-tab checks. GUI work stays with Content's guarded
lane. Technical counts, extracted references or this documentation do not prove
visual acceptance, permission to redistribute templates or full school completion.
