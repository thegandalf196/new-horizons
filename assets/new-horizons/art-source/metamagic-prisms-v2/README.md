# Metamagic prism rank family — provisional

Basic and Advanced reuse the previously requested HoMM3 Art skill concepts.
Expert uses a targeted built-in image edit to repair its missing leather
background while preserving the prism, gold arcs, three gems and three rays.
The exact prompts are retained alongside each master. References named in the
prompt records identify private working inputs, not shipped file dependencies.
All input artwork was original generated art; no game/template pixels were used.

The raw input drafts remain outside the product tree. Repository masters remove
only the caBX metadata carrier, retaining exact image chunks and decoded pixels.
Original and sanitized master hashes are recorded in manifest.json. Advanced
and repaired Expert are opaque RGB. Basic retains its original continuous
RGBA alpha range 224–250: it has no transparent holes but is slightly translucent.
That limitation is preserved honestly rather than thresholding its artwork.

The HoMM3 skill's `export_art.py` reduced each master to square 32,44,82,58px
exports using LANCZOS. `../export_metamagic_prisms.py` only assembles those native
exports: 32/44px slots are unchanged squares, 82×93 places an82px square at(0,5),
and58×64 places a58px square at(0,3). The unused top/bottom margins are transparent;
the whole painted square, including its leather backdrop, remains intact.
No stretch, crop, recolor, rank pips or invented frame is applied. The distinct
prism mounts and one/two/three outgoing rays communicate rank progression.

New `NH_metamagic_prism_*` names separate these assets from the historical
single-master color-adjustment exporter. The old files and manifest are retained
as historical assets; the canonical skill config selects this new family.

Root inspected the masters, native32/44 reductions and enlarged comparisons.
All artwork and bindings remain Provisional, not final user-approved art.
An in-game visual review of all four slot types is still required.
