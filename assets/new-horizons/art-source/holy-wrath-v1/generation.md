# Holy Wrath icon source

Created 2026-09-28 with the built-in ImageGen tool for the New Horizons Holy Wrath spell.

- Provenance: original AI-generated artwork; no game files or extracted proprietary art were used.
- Style reference: none. The requested late-1990s Heroes III painted fantasy direction was described in text only.
- Master: `master.png`, preserved byte-for-byte by the HoMM3 art export helper; 1254×1254, RGB.
- Runtime reductions: LANCZOS exports at 44×44 and 32×32; the 30×30 effect export is in `effect-30-export/`. The main comparison sheet and checksums are in this directory. These sizes follow New Horizons' existing 44×44/32×32 icons and VCMI's existing 30×30 direct PNG effect icon.
- Status: provisional spell art, not user-approved final artwork. The 44×44
  export is bound to `iconBook` and `iconScroll`; the 30×30 export is bound to
  `iconEffect` and `iconImmune` in the New Horizons spell data. These bindings
  still require native in-game visual review.

`NH_holyWrath_32.png` is retained as a small alternate. No 58×64
scenario-bonus variant was made; the spell still uses the unrelated
`SPELLBON.def:0:15` placeholder for `iconScenarioBonus`. Confirm display
sizing in the actual spellbook and battle UI before final art approval.

## Exact generation prompt

Use case: stylized-concept
Asset type: square original spell icon master for a turn-based fantasy game, later reduced to small spellbook and battle-effect icons
Primary request: Holy Wrath — a concentrated strike of divine light hitting one dark skeletal demon, instantly communicating sacred damage against undead or demonic foes
Scene/backdrop: a compact, dark, smoky umber battlefield atmosphere kept subdued behind the impact
Subject: one close-up horned skeletal demon as the sole target, its blackened bone and cracked dark armor catching a brilliant ivory-gold shaft of holy light; the beam visibly strikes its brow and throws a few restrained sparks and ash from the impact
Style/medium: original late-1990s PC fantasy game illustration, richly modeled painted forms with the dramatic lighting and material definition of a classic painted spell icon; not a copy of any existing game asset
Composition/framing: square high-resolution master; tight centered composition with the target and descending beam filling most of the square while every major shape remains inside the edges; strong simple silhouette readable at 44×44 and 32×32
Lighting/mood: theatrical chiaroscuro; a small intense ivory-white and warm-gold impact, deep charcoal shadow around the skeletal target
Color palette: warm sacred ivory and restrained gold against charcoal, bone-black, muted umber
Materials/textures: modeled dry bone, charred iron, smoky ash, painted light; soft contours defined by values rather than outlines
Constraints: one target only; original artwork; no text, lettering, symbols, interface frame, border, watermark, photorealism, flat vector shapes, cartoon outlines, neon bloom, or excessive particles
