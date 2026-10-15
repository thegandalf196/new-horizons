# Shock Assault provisional art

This original provisional painting was generated with the built-in image generator
on 2026-09-21. Its recorded generation used no external image as a pixel or style
reference. The subject is an armored cavalry lance breaking a shield, bound to
`new-horizons:offense.shockAssault` and `NH_perk_shock_assault`; the illustration
does not define or change the perk's mechanics.

`generation.json` retains original source hashes, dimensions, concept, prompt
filename, generator/reference provenance and the recorded CC0-1.0 contribution.
The exact prompt and square master remain in the complete external authoring
archive. Historical source/export paths do not promise files in this checkout.
The artwork is provisional, not final user-approved artwork, and this notice
does not select another source or confer rights on unrelated inputs.

## Mechanical authoring export

Set `NH_PYTHON` to a verified absolute Pillow-enabled interpreter, `NH_ART_ROOT`
to a complete external project-shaped workspace, and `NH_EXPORT_HELPER` to the
absolute path to the retained standalone `export_art.py` helper (no plugin
installation or activation is required). Then use:

```sh
"$NH_PYTHON" assets/new-horizons/art-source/active-perks-v3/export.py \
  --staging-root "$NH_ART_ROOT" --export-helper "$NH_EXPORT_HELPER"
```

The helper supplies 44×44 and 32×32 LANCZOS reductions plus source previews.
The exporter does not generate or alter the painting. Its four 44×44 runtime
states are brightness/color transforms of the normal reduction: group 0 frames
0–3 are normal, pressed, disabled and highlighted. Existing helper exports must
be complete; existing states, descriptor, review and runtime manifest are not
overwritten. Keep original inputs and prior revisions unchanged.

These outputs belong to the external staging mirror, not the installed module.
Current selected resources ship through the committed NHART pack and unchanged
resource mappings, as described in the [source index](../README.md). Export
success does not establish visual approval or rendered-game acceptance.
