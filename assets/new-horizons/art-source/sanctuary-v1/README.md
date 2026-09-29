# Sanctuary spell icon — v1

Status: **Provisional original concept art**. The spell config/runtime binding
and in-game visual review are pending; no final-art approval is recorded.

The concept is an ivory arched refuge inside a pale-gold ward, with one small
steel arrowhead turned aside. It suggests a protected target without implying
immunity to area effects. The artwork was generated as an original square
painting and reduced with LANCZOS; no classic game art, template, or supplied
image was used as an input.

## Files

- Master: [`exports/sanctuary/master.png`](exports/sanctuary/master.png),
  1254×1254 RGB.
- Exact prompt and generation provenance: [`prompt-sanctuary.txt`](prompt-sanctuary.txt)
  and [`generation.json`](generation.json).
- 44×44, 32×32, and 30×30 exports, comparison, and generated export manifest:
  [`exports/sanctuary/`](exports/sanctuary/).
- Proposed runtime copies: [`runtime/`](runtime/) and
  `Mods/new-horizons/Images/NH_spell_sanctuary_{44,32,30}.png`.
- Copy hashes: [`runtime-manifest.json`](runtime-manifest.json).

Native-size inspection found the arched doorway and enclosing ward remain
readable at all three requested sizes. The helper comparison includes the
44×44 and 32×32 samples; the 30×30 export was inspected separately at 1:1.
These raster checks do not establish spell-slot correctness or rendered UI
acceptance. The art remains unbound until the spell owner completes config
integration.
