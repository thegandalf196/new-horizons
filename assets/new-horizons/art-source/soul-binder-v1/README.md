# Soul Binder perk icon — v1

Status: **Provisional original concept art**. No final-art approval or in-game
visual review is recorded.

## Generation and exports

- Generator: host built-in `image_gen`; no API or CLI fallback.
- Style reference: none supplied or used. The art follows the local `homm3-art`
  direction for modeled late-1990s fantasy still-life objects.
- Selected master: [`exports/soul-binder/master.png`](exports/soul-binder/master.png),
  1254×1254 RGB. The exporter retained the selected generated image unchanged.
- Export method: `homm3-art/scripts/export_art.py`, LANCZOS reduction to
  44×44. The comparison sheet and raster manifest are in
  [`exports/soul-binder/`](exports/soul-binder/).
- Runtime states: 44×44 normal, pressed, disabled, and highlighted images use
  brightness/color-only transformations of the 44×44 source export.

## Visual review

- At native 44×44, the armored fist and exactly three violet-centered rings
  remain distinct; the linked cluster separates this perk from the reliquary
  and converging chains used for Soul Chain.
- The hand, iron rings, and chain attachments read as one grasping composition.
  Fine chain links and gauntlet seams merge at native size.
- The master uses a subdued umber ground, modeled blackened steel, localized
  Shadow-violet light, and no frame, text, or interface elements.

## Limitations

The ring cluster sits above the fist, so the grip is conveyed by short chain
attachments and the raised gauntlet silhouette. Review its final placement and
contrast in the actual perk UI; no in-game rendering has been performed.
