# New Horizons Heroes III UI style guide

Use this guide for every New Horizons dialog, panel, control, and UI review,
including provisional UI. It governs visual construction, not gameplay rules.
Preserve behavior, state handling, data flow, transfer logic, and interaction
semantics unless the task explicitly calls for changing them.

## Visual principles

- Background materials should appear continuous and intentional.
- Use ornamentation sparingly. Heroes III is ornate, but its ornament belongs
  primarily to frames, corners, buttons, and important focal areas. Do not
  decorate empty space merely to fill it.
- Preserve a dense, economical layout rather than modern generous whitespace.
- Establish hierarchy through size, depth, framing, contrast, and spacing,
  rather than modern cards, flat boxes, or excessive labels.
- Controls should feel tactile: pressable buttons, recessed fields, inset
  sliders, framed portraits, and visually distinct selections.
- Keep typography consistent with the original game's scale and hierarchy.
  Avoid oversized text or controls, excessive padding, and uniformly centered
  layouts unless that treatment belongs to the original interface.
- Align important content to a clear grid. Repeated elements need consistent
  dimensions, framing, padding, and treatment. Use symmetry where the
  interaction is symmetrical, not where it harms usability.
- Preserve the late-1990s prerendered/pixel-art aesthetic. Do not introduce
  contemporary flat design, glassmorphism, vector-clean geometry, mobile-app
  conventions, or modern-style gradients. Painted lighting and shading that
  match the original game are appropriate.

## Creature art and motion — Cabir correction

User direction,2026-10-06: Cabir must fit the original low-resolution
prerendered-3D creatures, not look like smooth HD illustration. Greg Fulton
explicitly identifies the original game's sprites as derived from prerendered
3D models in [Fanstratics newsletter43](https://heroes.thelazy.net/index.php/Greg_Fulton/Fanstratics_Newsletters/43).
That is production evidence, not proof that arbitrary pixelation recreates its
style. Our practical art direction: simplified modeled volume, restrained
material highlights, broad legible shadows, coarser surface detail and a
native-sized raster silhouette; avoid smooth modern illustration and excessive
lava filigree. Evaluate beside existing creatures at actual battle/map size.
Walking must alternate support/advancing legs and show a real stride; different
PNG hashes or four nearly identical poses do not satisfy animation acceptance.
Selection feedback follows the creature silhouette, not its rectangular canvas.

## Spell-symbol presentation

User direction2026-10-04: new spell icons follow original Heroes III spells:
display only the spell's identifying symbol, with transparent space around and
through its silhouette. Do not use the opaque leather/scenery painting suitable
for a secondary Skill icon as a spell emblem. The Mage Guild's rolled parchment
and the spellbook's page are independent UI surfaces; neither belongs inside
the spell raster. Preserve symbol identity and readability at actual30/32/44px
roles. Inspect alpha and native exports, not just a high-resolution painting.

Center independent spellbook PNG frames inside the actual school-border
canvas; do not inherit vanilla DEF offsets for smaller standalone symbols.
Preserve native DEF placement when slots change spells. Both Mage Guild and
House of Wisdom scrolls use the same parchment composition policy: complete
scroll sprites remain untouched, standalone symbols sit on the native blank
scroll, and parchment is never duplicated. Spell casting-choice and
confirmation panels are viewport-centered, not cursor-anchored; ordinary
tooltips retain their normal cursor-relative behavior.

Mage Guild Adventure Spell access uses the existing exterior illustration as
a clickable, gold-highlighted hover region, not a separate icon/button. Reuse
the configured faction/tier image bounds and position. Guild level names use
the ordinary Arabic1–5 convention consistently across factions.

## Build from the outside inward

Before placing individual sprites, decompose the UI as:

`outer frame → primary panel → secondary recessed regions → content frames → controls → text/icons`

For every major region, ask whether it is part of the main surface or a
recess, whether it needs a frame, what material it suggests, what its visual
depth is, how it joins its neighbors, and whether its spacing matches repeated
elements nearby.

Inspect available project UI assets before inventing new ones. Identify which
assets are meant for tiling, stretching, corners, borders, buttons, recesses,
and backgrounds. Reuse authentic game components when they join cleanly. Do
not place an asset merely because it exists if its geometry, lighting, bevel,
or texture is wrong for that position. Prefer a few reusable structural assets
over independent decorative cutouts. If existing pieces cannot assemble into
a coherent surface, recreate the treatment coherently instead of forcing an
obvious sprite collage.

Treat a supplied mockup as a guide to content placement and interaction. If
its own construction is artificial or unattractive, do not reproduce that
construction literally.

## Review checklist

At the game's actual resolution, compare the result against relevant original
Heroes III dialogs as well as the supplied reference. Check for:

- pasted-on separators or floating controls;
- inconsistent margins, repeated-element sizes, or alignment;
- excessive empty space or weak hierarchy;
- mismatched frame thickness or bevel direction;
- discontinuous leather, stone, wood, or other background textures;
- asymmetrical repeated controls without an interaction reason;
- oversized buttons, labels, or padding;
- controls insufficiently distinguished from their background;
- decorative clutter or regions resembling independent pasted images.

The result should look as though the original Heroes III UI team designed it.
As a construction test, mentally remove the text and icons: a coherent game
panel should remain. If what remains is a pile of unrelated lines and cutouts,
redesign the panel structure, framing, proportions, depth, alignment, and
material continuity rather than adding more decorative sprites.
