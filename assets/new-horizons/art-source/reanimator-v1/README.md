# Reanimator provisional art v1

Purpose-made original artwork for the New Horizons Reanimator Shadow Magic
perk. A blackened-steel clasp and amethyst stone gather three intact,
species-neutral helms, suggesting practiced control that returns more fallen
combatants rather than raising the Undead. The exact generation prompt,
generator output identifier, master hash, and reduction metadata are retained
in `prompt-reanimator.txt`, `generation.json`, and `exports/reanimator/`.

The 1254×1254 master is reduced with the `homm3-art` export helper to 44×44 and
32×32 LANCZOS PNGs. The comparison sheet includes true native samples and
nearest-neighbor enlargements. At 44px the clasp, violet stone, and three
separate closed helms remain readable. `export-runtime.py` derives normal,
pressed, disabled, and highlighted 44×44 states using brightness/color changes
only, then installs the four images and a VCMI descriptor under `runtime/` and
`Mods/new-horizons/Images/`. Hashes are recorded in `runtime-manifest.json`.

Status is provisional: this art is not yet bound to UI/configuration or
approved in-game.
