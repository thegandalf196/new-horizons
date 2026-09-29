# Soul Chain spell icon — v1

Status: **Provisional original concept art**. No final-art approval or in-game
visual review is recorded.

## Generation and exports

- Generator: host built-in `image_gen`; no API or CLI fallback.
- Style reference: none supplied or used. The art follows the local `homm3-art`
  direction for modeled late-1990s fantasy still-life objects.
- Selected master: [`exports/soul-chain/master.png`](exports/soul-chain/master.png),
  1254×1254 RGB. The exporter retained the selected generated image unchanged.
- Export method: `homm3-art/scripts/export_art.py`, LANCZOS reductions at
  44×44, 32×32, and 30×30. The comparison sheet and raster manifest are in
  [`exports/soul-chain/`](exports/soul-chain/).
- Runtime purpose: 44×44 spellbook/scroll, 32×32 scenario bonus, and 30×30
  battle effect/immunity icon. The descriptor is handoff metadata; spell
  configuration remains outside this art-only change.

## Visual review

- The native 44×44 and 32×32 exports retain the reliquary as the focal mass;
  both spectral chains remain visible as separate converging forms.
- At 30×30, the core and two chain forms still read, while finer cage bars and
  individual chain links merge into larger metallic and violet shapes.
- The square master has a dark earthy background, modeled steel, localized
  Shadow-violet light, and no frame, text, or interface elements.

## Limitations

The spectral chains are brighter and broader than fine physical links so they
survive reduction. These checks cover the raster exports only; the icon is not
wired into a spell configuration and has not been reviewed in the game UI.
