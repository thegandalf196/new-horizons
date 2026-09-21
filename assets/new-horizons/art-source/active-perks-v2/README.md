# Active perk paintings, provisional v2

This directory contains three original provisional New Horizons perk paintings
generated with the host's built-in image generator on 2026-09-20. The subjects
are the newly active perks that were missing runtime art:

| Perk | Runtime key | Subject |
| --- | --- | --- |
| Discipline — Inspirational Leader | `NH_perk_inspirational_leader` | Gold command standard, baton, and steel helmets |
| Sylvan Luck — Wild Chance | `NH_perk_wild_chance` | Emerald summoning seed wrapped in a flowering vine |
| Sylvan Luck — Perfect Moment | `NH_perk_perfect_moment` | Gold arrow aligned to the center of a celestial target |

The masters are square RGB/RGBA PNGs and are intentionally kept separate from
earlier art families. The `exports/` subdirectories were produced with the
`homm3-art` `scripts/export_art.py` helper and contain the exact master copy,
44x44 and 32x32 LANCZOS reductions, labeled comparison sheet, and per-export
manifest. Native-size previews were inspected before installation; all three
subjects retain distinct major silhouettes at both sizes.

`export.py` reproduces the mechanical export and the four runtime states. It
never overwrites an existing source or installed file. The runtime states are
brightness/color transforms of the 44x44 normal reduction only; they do not
change the source painting. `generation.json` records the exact prompts,
generation references, dimensions, and source hashes. The artwork is
provisional and contributed under CC0-1.0 for the New Horizons fork.

No final-art approval is implied by this directory. Existing masters and
runtime art families remain untouched.
