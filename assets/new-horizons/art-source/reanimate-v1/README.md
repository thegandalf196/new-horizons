# Re-animate provisional art v1

Purpose-made original artwork for the New Horizons Re-animate Shadow spell.
The concept presents a species-neutral, intact armored combatant returning to
the current battle; it avoids undead-only imagery. The exact generation prompt,
generator output identifier, master hash, and export metadata are retained in
`prompt-reanimate.txt`, `generation.json`, and `exports/reanimate/`.

The 1254×1254 master is reduced with the `homm3-art` export helper into
44×44, 32×32, and 30×30 LANCZOS PNGs. The helper comparison shows actual native
44px and 32px samples plus nearest-neighbor enlargements; the 30px export was
also inspected directly at native size. The closed helm, concentrated amber
glow, shield, and purple magic remain recognizable at the smallest size.

`export-runtime.py` installs the unchanged three derivatives under
`runtime/` and `Mods/new-horizons/Images/`, refusing to overwrite differing
files. SHA-256 hashes are in `runtime-manifest.json`. Status is provisional:
these assets are not yet bound to UI/configuration or approved in-game.
