# Cleave provisional art v1

This original Offense perk icon was generated with the built-in image generator
through the `homm3-art` workflow on 2026-09-27, without input images. The exact
prompt is preserved in [`prompt-cleave.txt`](prompt-cleave.txt). It is
provisional and has not been designated definitive artwork.

The selected 1254×1254 RGB master, deterministic 44×44 and 32×32 LANCZOS
reductions, labeled comparison sheet, and helper manifest are retained under
[`exports/cleave/`](exports/cleave/). `export-runtime.py` derives four
44×44 VCMI button states using brightness and color adjustments only. The
states and descriptor are retained under [`runtime/`](runtime/) and copied as
runtime files under `Mods/new-horizons/Images/`. The live perk binding uses
`NH_perk_cleave`; both copies' hashes are recorded in
`runtime-manifest.json`.

The heavy notched axe follows through from a shattered red shield toward a
second adjacent blue shield. The paired shields, trail of splinters, and single
axe make the automatic secondary melee strike legible at perk-icon size. The
generated source art and deterministic export code are contributed under
CC0-1.0. Gameplay activation and the client binding are maintained separately
from this reproducible source-art package.
