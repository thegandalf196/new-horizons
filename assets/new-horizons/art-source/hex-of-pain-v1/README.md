# Hex of Pain provisional spell art v1

Original purpose-made artwork for the New Horizons Shadow spell Hex of Pain,
generated with the host's built-in image generator on 2026-09-29 without input
images or style-reference images. It is provisional, not final user-approved
artwork. The retained 1254×1254 master, exact prompt, portable generation
provenance, and deterministic raster exports are in this directory.

The master reduces to 44×44 spellbook, 32×32 scroll/scenario-bonus, and 30×30
battle-effect/immunity icons with the `homm3-art` export helper and Pillow's
LANCZOS resampling. To reproduce the reductions, run the installed
`homm3-art/scripts/export_art.py` with an absolute Python interpreter that has
Pillow, using `exports/hex-of-pain/master.png` as input, a new output directory,
`--name hex-of-pain`, and `--sizes 44,32,30`. The 30×30 nearest-neighbor review
enlargement is `exports/hex-of-pain/hex-of-pain-30-nearest-review.png`; it is
for inspection only and is not a runtime icon.

Runtime mapping: `NH_hex_of_pain_44.png` is the spellbook icon;
`NH_hex_of_pain_32.png` serves scroll and scenario bonus; and
`NH_hex_of_pain_30.png` serves battle effect and immunity.

The artwork and prompt are dedicated to the public domain under CC0-1.0. No
purchaser or third-party game artwork is embedded.
