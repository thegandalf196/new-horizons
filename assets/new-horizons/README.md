# Original New Horizons provisional UI artwork

These are newly authored geometric/vector medallions, **not** final commissioned
illustrations. They are not traced from Heroes assets or the supplied concept
images. Colors and symbolic geometry were chosen for this implementation; no
purchaser palette or pixels enter the generator or exports.

## License and editable sources

The new artwork, editable SVGs and `generate_icons.py` are dedicated under
**CC0-1.0**: https://creativecommons.org/publicdomain/zero/1.0/legalcode .
To the extent possible under law, the creators waive copyright and related rights
in these new assets. No rights in Heroes III artwork or user concept art are
asserted or transferred by this dedication. VCMI product source retains its GPL.

`svg/` contains editable vectors. The original shape definitions also live in
`generate_icons.py`; running it regenerates SVGs and PNGs, overwriting manual SVG
changes. Artists may instead edit SVGs and export them with an SVG renderer.

Run `python3 assets/new-horizons/generate_icons.py` from the repository root.
It requires Pillow, does not invoke the game or any GUI, and reads **no original
asset input**. Raster exports are 4x supersampled then reduced with Lanczos to
8-bit/channel RGBA PNGs. The mod's `Images/` mount is registered by the integrator
at `SPRITES/`; no purchaser files are copied into this module.

## Runtime names and states

Outputs: `Mods/new-horizons/Images/NH_*`.

- Charge, Hold the Line, Advance, Aggressive, Defensive, Spells and Cancel:
  `NH_<id>_button.json` references 64x64 states in VCMI Button order:
  **0 normal, 1 pressed, 2 blocked/disabled, 3 highlighted/hover**.
- `NH_hero_actions_entry.json`: same four states, **48x36**, matching the measured
  original battle cast-button canvas. Used only in battles with new command rules.
- `NH_hero_actions_back.png`: 640x520 original dialog background.
- Six schools (`light`, `nature`, `sorcery`, `havoc`, `shadow`, `chaos`):
  - `NH_<school>_header.png`: **160x96**, transparent top 28 pixels; artwork below.
  - `NH_<school>_bookmark.json`: two **80x60** frames, **0 selected, 1 unselected**.
    These are not normal/pressed/disabled button states.
  - `NH_<school>_icon.png`: **64x64** medallion suitable for future hero/school UI.
- Hero display glyphs: `NH_hero_<id>_<size>.png`, with matching editable SVGs,
  at **32x32** and **64x64**. IDs: `attack`, `defense`, `power`, `knowledge`,
  `mana`, `leadership`, `movement`, `morale`, `luck`, `siege`, `growth`, `mastery`,
  `core`, `elite`, `champion`. These are display-only medallions, not button
  states or implemented attribute/mastery controls. The mastery glyph denotes
  branching development; it does not assert any particular mastery effects.
  Tier glyphs do not reclassify creatures. Pair glyphs with readable UI labels;
  neither color alone nor these symbols should be the only source of meaning.
- Six real school secondary-skill image families:
  `NH_<school>Magic_<rank>_<size>.png`, plus matching editable SVGs. Schools use
  the six IDs above; rank is `basic`, `advanced` or `expert`. Sizes follow
  `config/schemas/skill.json` and `CSkill::registerIcons`:
  **small32x32**, **medium44x44**, **large82x93**, **scenarioBonus58x64**.
  One/two/three lit markers distinguish these existing skill ranks, not future
  three-choice masteries. These are static image files, not animation/button
  states. Build owns skill definitions, rank effects and registration; reuse of
  our original CC0 school geometry introduces no purchaser or concept pixels.
- Live hero-development entry: `NH_hero_growth_entry.json`, **24x24**,
  four normal/pressed/disabled/highlighted frames and matching editable SVGs.
  Uses our original growth motif. CHeroWindow gates it on actual saved development
  views, never a hero-type template or installed defaults alone. Separate growth/
  capability GUI evidence is recorded in `docs/NH_FRONTEND_HANDOFF.md`.
- Future post-Expert Artillery mastery options: `NH_mastery_<option>_<size>.png`
  with matching editable SVGs, **32x32** and **64x64**. Option keys supplied by
  Runtime: `artilleryVolley`, `artilleryPrecision`, `artilleryRepair`; full IDs
  have the `new-horizons:` scope. Explicit iconKey is `NH_mastery_<option>`.
  Original fan-projectile, target-sight and mechanical-wrench geometry distinguishes
  the choices without text, copied art or a fourth skill-rank badge. These are
  static display icons, not button-state sheets. Selection/hover/keyboard feedback
  must come from real UI controls; effects and eligibility remain authoritative.
  This art is not mastery implementation, activation or rendered acceptance.
- All new images use RGBA transparency, not the original indexed palette.

The five command buttons and Spells/Cancel are wired to the real hero-action
chooser and existing authoritative spell-action request path. Original school
resources remain by-reference. The six-school art is prepared for the existing
custom-school registry/header/bookmark hooks; **a six-school casting migration
and hero-screen wiring are not completed or claimed by this art delivery**.
No fake spells/masteries or inert controls are added to pretend otherwise.
Some hero glyphs now appear in independently tested growth/development views;
others, including creature-category glyphs, remain artwork only. The Frontend
handoff records the exact integrated scope; artwork alone does not prove mechanics.
They are original geometry authored by the AI-assisted New Horizons frontend
implementation, under the same CC0 dedication above. Before the school-skill increment, exports contained
118 SVGs, 118 PNGs and 20 animation JSONs. Adding the 30 hero glyph variants
preserved all 196 previous outputs byte-for-byte; local static evidence resides
under ignored `build/new-horizons-linux/research/hero-art/`. This checks outputs,
not the future screen's rendering, readability or runtime attribute formulas.

The subsequent72 school-skill PNGs/72 SVGs preserve all256 earlier outputs
byte-for-byte. That checkpoint contained190 SVGs,190 PNGs,20 animation JSONs.
Local schema/dimension/hash evidence and generated contact sheet are under ignored
`build/new-horizons-linux/research/skill-art/`. Actual skill registration, rank
progression, rendered hero/level-up/campaign use and old-save semantics require
integrated tests; these exports alone do not prove those mechanics.

The later hero-growth entry adds4 PNGs/4 SVGs/1 JSON while preserving all400
previous hashes. That checkpoint contained194 SVGs,194 PNGs,21 animation JSONs.
No new mastery/leadership/siege mechanics are implied by this display entry.

The post-Expert Artillery option artwork adds6 PNGs/6 SVGs and preserves all409
previous hashes. **Current complete exports:200 SVGs,200 PNGs,21 animation JSONs**.
All421 outputs were reproduced byte-identically in an isolated temporary tree;
blank/opaque/wrong-size PNG controls reject. SVGs contain only original geometric
primitives, no embedded images, fonts or external links. Local audit/contact evidence
is under ignored `build/new-horizons-linux/research/mastery-art/`. The three effects,
post-Expert query lifecycle, AI, saved choices and Windows/UI acceptance require
separate integrated work; fixed891 release files are not changed by this future art.

## Actual installed artwork inspection (metadata only)

Purchaser `H3sprite.lod` was inspected offline using its archive table, zlib where
needed, and a local DEF decoder following `client/render/CDefFile.cpp`. Pillow
produced local review images. No original executable was run. Extracted pixels,
archive hashes and the inspection script/report remain only under the ignored
`build/new-horizons-linux/research/spellbook-source/` directory. Never stage that
folder. Independent Tester reviewed the decoded pixels and source lookups.

- `Schools.def`, interface type 0x47, group 0, frames **0..3**, full canvas
  **160x96** each, cropped **160x68**, margin **(0,28)**, compression format 1.
  Source order AIR/FIRE/WATER/EARTH. Observed pixels: blowing bearded cloud face;
  golden sun face with rays; breaking blue wave; tree/mountain landscape.
  Fire's sun face is an observed installed image, not an assumption from its name.
- `SPELTAB.def`, type 0x47, group 0, five **83x294** full-strip frames. Each frame
  changes which school bookmark is selected: AIR/FIRE/WATER/EARTH/ANY. They are
  **not five independent tab icons or four button states**. Crops/margins:
  frame0 83x289@(0,0); frame1 83x283@(0,6); frame2 80x283@(0,6);
  frame3 83x283@(0,6); frame4 82x287@(1,6). Format 1.
- These DEF files contain 256 RGB palette entries. Index 0 is cyan (0,255,255),
  made transparent by SDLImageLoader's color key. CDefFile initially assigns
  opaque alpha to all palette entries. Do not apply an invented palette remap.
- `SpelBack.pcx` is H3 indexed PCX, **620x595**. `SpelTrnL.pcx` is **33x39**;
  `SpelTrnR.pcx` is **29x32**. These are engine resource references, not shipped
  extractions.
- `CSpellWindow`: legacy header at **(117,74)** and full strip at **(524,88)**,
  plus large-book offsets. Legacy hit regions start **(549,94)**, 45x35, ordered
  vertically Air/Earth/Fire/Water/Any at offsets 0/57/116/176/236.
- Existing custom-school hooks already use `getSchoolHeaderPath()` and
  `getSchoolBookmarkPath()`. Headers share (117,74) and appear on the first page
  only. Custom bookmarks use frame0 selected/frame1 unselected, x15 (small book)
  or x0 (large); vertical placement is computed from school count and 62px span.
  No parallel spellbook is necessary for the new six-school artwork.
- Actual `ICM005.def` battle cast button has a **48x36** canvas; the new entry
  animation preserves that footprint instead of putting a 64px icon over neighbors.

This metadata is not a claim of rendered new-UI acceptance. Build and the sole
Tester must verify actual fonts/layout, states, clicks, cancellation, authority
readback and battle/save lifecycle on a frozen candidate.
