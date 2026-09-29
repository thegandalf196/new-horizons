# Plaguebearer provisional perk icon

- Generator: Codex built-in image generation.
- Style reference: none supplied; followed the local `homm3-art` style guide.
- Source master: `master.png`, 1254 × 1254 RGB PNG, copied byte-for-byte by the export helper. The manifest records the original generated-image path and source hash.
- Export method: the HoMM3 art skill's `export_art.py`, using LANCZOS reductions to 44 × 44 and 32 × 32 PNG.
- Runtime states: `NH_perk_plaguebearer_normal.png` is the 44 × 44 reduction. Pressed is the source reduced icon multiplied by 0.95; highlighted is multiplied by 1.03; disabled is grayscale multiplied by 0.90. These are restrained UI-state treatments of the same art.
- Status: provisional original artwork; not extracted from Heroes III. No in-game visual review or final-art approval is recorded.

Exact generation prompt:

```text
Use case: stylized-concept
Asset type: square game perk icon, original provisional Heroes III-inspired secondary-skill artwork
Primary request: Plaguebearer, a distinct perk emblem for contagion spreading one additional time: a single armored gauntlet carrying a small cursed vessel whose violet wisps branch outward.
Scene/backdrop: Dark oxblood and umber, subtly textured, close behind the emblem.
Subject: A worn steel-and-leather gauntlet rises from the lower center, palm cupped around a palm-sized cracked black-iron vial; the vessel is clearly visible above the fingers. A few restrained violet ash wisps branch outward from the vial like a contagion spreading.
Style/medium: Miniature modeled fantasy still-life painting in the late-1990s Western PC fantasy illustration language of Heroes III; physical pre-rendered volume with painterly character, not photographic realism.
Composition/framing: Compact centered close-up; the gauntlet and vessel form one integrated emblem filling most of the square with a narrow safe inset. Make the vessel and gripping fingers remain distinct at 44×44 pixels.
Lighting/mood: Theatrical chiaroscuro, warm bright highlights on the gauntlet's steel knuckles and a deep shadow side; small localized violet light around the vessel and branching wisps.
Color palette: Aged steel, dark leather, subdued oxblood and umber, muted violet and ash-lilac; no green.
Materials/textures: Worn polished steel, creased dark leather, pitted iron, dusty ash.
Constraints: Original provisional art; no frame or border; no text, lettering, readable runes, UI, extra props, skulls, bones, plants, liquid venom, green poison, censer, flat vector shapes, cartoon outlines, intentional pixel art, dithering, modern glossy mobile-game effects, neon bloom, or a large particle cloud.
```
