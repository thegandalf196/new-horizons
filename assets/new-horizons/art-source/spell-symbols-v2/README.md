# New Horizons spell-symbol cutouts

UP-230's symbol-only integration. The guild parchment remains a separate
native game surface; these transparent spell symbols replace only the existing
icon files bound to `iconBook`, `iconScroll`, `iconEffect`, `iconImmune`, and
scenario-bonus roles. Spell definitions and gameplay are unchanged.

All 26 inventoried custom spell families now have approved symbol-v2 44×44,
32×32, and 30×30 RGBA exports integrated at their existing runtime paths.
`manifest.json` records all 26 as `converted-provisional`; this does not claim
final-art approval or rendered/native-resolution in-game acceptance. The
runtime spell graphics bindings remain unchanged, including legacy atlas
aliases.

Source-family READMEs retain provenance and the `Exact edit prompt` link target.
Keep complete generation requests, masters and historical exports in the external
authoring archive; a public heading or source filename does not promise those
inputs are shipped. Public contracts and selected runtime resources do not
require that archive for ordinary builds. Revised masters are exported
with the HoMM3 art workflow's deterministic LANCZOS reducer, preserving alpha;
the manifest records master/export/runtime SHA-256 values and the spell's
unchanged live binding names. `tools/tests/test_new_horizons_spell_symbols.py`
checks alpha, dimensions, inventory status, config-role bindings, and equality
between the retained exports and their runtime copies.

The revisions were made from project-owned New Horizons artwork. Their source
READMEs retain the project provenance and identify the revisions as provisional.
No purchaser parchment pixels are copied or redistributed here.
