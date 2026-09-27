# Vengeance provisional art v1

This purpose-made Offense perk icon was generated through the built-in image
generator using the `homm3-art` workflow on 2026-09-27, without input images or
style references. The exact prompt is preserved in
[`prompt-vengeance.txt`](prompt-vengeance.txt). It remains provisional and has
not been designated definitive artwork.

The 1254×1254 RGBA master, deterministic 44×44 and 32×32 LANCZOS reductions,
labeled native-size comparison, and export manifest are retained under
[`exports/vengeance/`](exports/vengeance/). The shield catches the incoming
strike while a visibly separate sword returns the blow; the two distinct blade
directions preserve the block-then-riposte idea at perk-icon size.

`export-runtime.py` derives the four 44×44 VCMI button states using brightness
and color adjustments only. Source runtime states and descriptor are retained
under `runtime/` and mirrored into `Mods/new-horizons/Images/`. The client
binding uses `NH_perk_vengeance`; file hashes are recorded in
`runtime-manifest.json`. Gameplay activation and final art approval are tracked
separately. The generated source art and deterministic export code are
contributed under CC0-1.0.
