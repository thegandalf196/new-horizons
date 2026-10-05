# Vengeful Vines — transparent spell symbol

UP230 replaces the opaque square painting with a standalone spell emblem.
Created through the built-in image generator using the Heroes III art skill,
editing the original project-owned Vengeful Vines master. The earlier v1 source
is preserved. Original project artwork, GPL-2.0-or-later. Status: Provisional;
native export inspection is not in-game acceptance or final user approval.

The skill's export_art.py copies the generated RGBA master and mechanically
reduces it to44/32/30px with LANCZOS, preserving transparency. Exact dimensions
and hashes are in vengeful-vines-symbol-manifest.json. The native guild parchment
remains a separate UI surface, not part of this emblem.

## Exact edit prompt

Use case: background-extraction. Edit target: the supplied Vengeful Vines master. Create a Heroes III combat spell SYMBOL ONLY for placement on the game's existing parchment. Preserve the same twisting S-shaped brown woody vine, broad emerald leaves and ivory thorns, material lighting and late-1990s modeled fantasy illustration identity. Remove ALL earth, soil, roots on ground, rectangular painted scenery, umber background, cast background shadows and atmospheric haze. The entire area outside the vine, its leaves and thorns MUST be genuinely transparent including the holes between its bends. Keep a compact complete unclipped silhouette with a modest transparent safe inset on every edge. No paper, frame, card, box, border, lettering, scenery, unrelated ornaments or new subject. Spell-symbol art rather than a secondary-skill square painting. High-resolution square RGBA master with clean semitransparent antialiased edges; not pixel clusters or a white/checkerboard painted background.

Edit reference: ../vengeful-vines-v1/master.png (original project artwork).
