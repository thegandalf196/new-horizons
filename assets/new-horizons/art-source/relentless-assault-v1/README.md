# Relentless Assault provisional art v1

Purpose-made Offense perk art generated with the built-in image generator and
the `homm3-art` workflow on 2026-09-27. No source image or style reference was
used. The exact prompt and generation provenance are retained in
[`prompt-relentless-assault.txt`](prompt-relentless-assault.txt) and
[`generation.json`](generation.json).

The 1254×1254 master, deterministic 44×44 and 32×32 LANCZOS reductions, labeled
native-size comparison, and raster export manifest are in
[`exports/relentless-assault/`](exports/relentless-assault/). Three successive
sword impacts converge on one battered shield, using an increasingly bright
spark and deeper gouge to suggest escalating attacks against the same target.
The objects remain recognizable in the 44px and 32px reductions, though the
smallest version loses some fine damage detail.

`export-runtime.py` derives four 44×44 VCMI button states using brightness and
color adjustments only. The generated states and descriptor are retained in
`runtime/` and mirrored into `Mods/new-horizons/Images/`. The client binding
uses `NH_perk_relentless_assault`; hashes are recorded in `runtime-manifest.json`.
The artwork is provisional and has not been approved as definitive.
