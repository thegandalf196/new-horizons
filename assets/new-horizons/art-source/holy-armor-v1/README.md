# Holy Armor spell icon — v1

Status: **Provisional original concept art**. No final-art approval or in-game
visual review is recorded.

## Generation and exports

- Generator: host built-in `image_gen`; no API or CLI fallback.
- Style reference: none supplied or used. The art follows the local `homm3-art`
  direction for modeled late-1990s fantasy still-life objects.
- Selected master: [`exports/master.png`](exports/master.png), 1254×1254 RGB.
- The selected generator output is retained in this repository as
  [`exports/master.png`](exports/master.png); no machine-local path is required
  to reproduce the raster exports.
- Export method: `homm3-art/scripts/export_art.py`, LANCZOS reductions at
  44×44, 32×32, and 30×30. The exporter manifest and comparison sheet are in
  [`exports/`](exports/).
- Runtime purpose: 44×44 spellbook/scroll, 32×32 scenario bonus, and 30×30
  battle effect/immunity icon, matching recent custom spell conventions.

## Exact selected generation prompt

```text
Use case: stylized-concept
Asset type: original Heroes III inspired game spell icon, high-resolution square master for reduction to 44×44, 32×32, and 30×30
Primary request: create a compact, luminous pale-gold protective breastplate encased in a restrained sanctified shield aura, clearly communicating magical armor and protection.
Scene/backdrop: subdued dark earthy umber and deep olive-brown texture, low detail behind the focal object.
Subject: one unoccupied medieval torso breastplate in a slight three-quarter frontal view, layered pale antique-gold plates, darker bronze recesses, narrow steel-shadow seams, broad shoulders and a distinct chest curve. A thin upright translucent golden shield-shaped aura closely surrounds the breastplate; it is soft concentrated light, not a second physical shield. The armor remains dominant.
Style/medium: original late-1990s Western PC high-fantasy illustration; miniature modeled fantasy still-life painting, pre-rendered physical volume with painterly fantasy-book character, materially credible worn polished metal rather than photorealism.
Composition/framing: close-up centered emblematic still-life, breastplate and aura fill most of the square while fully inside a narrow safe inset; simple complete silhouette and large coherent forms.
Lighting/mood: theatrical chiaroscuro, warm pale-gold hero light on one side and deep shadow opposite; a small concentrated sanctified glow along the shield aura, no broad bloom.
Color palette: pale gold, antique bronze, subtle steel shadows against dark earthy umber and olive.
Materials/textures: modeled polished metal with restrained age and fine surface variation; smooth translucent light aura.
Constraints: original artwork; no body or wearer; no additional props; designed to remain identifiable at 44×44, 32×32, and 30×30; no lettering, runes, symbols, interface, or frame.
Avoid: separate hand-held shield, helmet, sword, visible person, cross or heraldic emblem, clutter, ornamental border, UI chrome, text, neon glow, abundant particles, flat vectors, cartoon outline, intentional pixel art, dithering, plastic gloss, modern mobile-game aesthetic, watermark.
```

## Visual review

- At native 44×44 and 32×32, the torso armor remains distinguishable inside the
  upright shield-shaped glow; the complete silhouette stays within the canvas.
- At native 30×30, the breastplate and protective aura still read, while small
  plate seams merge into the central gold mass.
- The master has a strong light/shadow split, dark earthy background, no frame,
  text, logo, or visible wearer. The aura is intentionally brighter than a
  purely subdued halo so the protection meaning survives reduction.
- These checks cover exported raster images only. The icon is not wired into a
  spell config and has not been reviewed in the game UI.

## Limitations

The gold armor and gold aura share a narrow palette, so the 30×30 effect icon
has less material separation than the master. A future integration review
should check contrast against the actual spellbook and battle backgrounds.
