# Shadow Plague provisional spell icon

- Generator: Codex built-in image generation.
- Style reference: none supplied; followed the local `homm3-art` style guide.
- Source master: `master.png`, 1254 × 1254 RGB PNG, copied byte-for-byte by the export helper. The manifest records the original generated-image path and source hash.
- Export method: the HoMM3 art skill's `export_art.py`, using LANCZOS reductions to 44 × 44, 32 × 32, and 30 × 30 PNG.
- Status: provisional original artwork; not extracted from Heroes III. No in-game visual review or final-art approval is recorded.
- Runtime handoff: `NH_plague.json` describes the provisional spell icon sizes and is metadata only; it is not referenced by spell configuration.

Exact generation prompt:

```text
Use case: stylized-concept
Asset type: square game spell icon, original provisional Heroes III-inspired secondary-spell artwork
Primary request: A supernatural contagion spell represented by one cracked black-iron reliquary censer, distinct from a biological poison. The vessel emits a few restrained violet ash tendrils that split and curl outward like a spreading curse.
Scene/backdrop: Dark worn umber and charcoal, subtly textured and close behind the object.
Subject: A compact, heavy blackened-iron reliquary censer with a clear closed vessel silhouette, a cracked lid and narrow vents; muted violet ash and two or three fine branching wisps emerge from it.
Style/medium: Miniature modeled fantasy still-life painting in the late-1990s Western PC fantasy illustration language of Heroes III; physical pre-rendered volume with painterly character, not photographic realism.
Composition/framing: Centered close-up, one dominant object filling most of the square with a narrow safe inset; clear large shapes and strong silhouette that will remain readable at 44×44 pixels.
Lighting/mood: Theatrical chiaroscuro, one warm bright hero-side highlight on worn iron, deep shadow side; small localized violet glow only around the vent and wisps.
Color palette: Black iron, subdued umber, muted violet and a little ash-lilac; no green.
Materials/textures: Pitted black iron, worn seams, a few crisp metallic edges, fine dusty ash.
Constraints: Original provisional art; no frame or border; no text, lettering, readable runes, UI, extra props, skulls, bones, plants, liquid venom, green poison, flat vector shapes, cartoon outlines, intentional pixel art, dithering, modern glossy mobile-game effects, neon bloom, or a large particle cloud.
```
