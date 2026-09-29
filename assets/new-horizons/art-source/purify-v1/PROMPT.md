# Purify spell icon — v1

Original provisional spell icon generated for the New Horizons mod. The selected master is `master.png` (1254×1254, RGB). No style or content reference images were used. The second built-in image-generation call was an edit of the first result; it removed a hanging chain that touched the crop while preserving the censer and cleansing effect.

## Initial generation prompt

```text
Use case: stylized-concept
Asset type: original square fantasy spell icon for a Heroes III-inspired game mod
Primary request: depict Purify as the cleansing of curses and poison through a strong, readable physical object.
Scene/backdrop: subdued dark umber and aged leather texture, close and uncluttered.
Subject: a single dominant, heavy silver-and-bronze medieval hand censer, three-quarter view, filling most of the square with a narrow safe inset. From its open pierced lid, a compact plume of sickly green poison vapor and black thorn-like curse wisps is being burned away and transformed into a small, clear pearl-white smoke curl; make the transition unmistakable but keep it visually integrated with the censer. No people.
Style/medium: original late-1990s Western PC fantasy miniature still-life painting, modeled pre-rendered volume with painterly fantasy-book character, rich believable materials, soft value-defined contours.
Composition/framing: square close-up icon, censer silhouette fully inside the image, one focal object, no empty margins.
Lighting/mood: theatrical chiaroscuro; warm ivory highlight on one side of the metal, deep shadow on the other, restrained concentrated glow at the censer mouth.
Color palette: tarnished silver and aged bronze against dark umber, with small controlled accents of poisonous green, charcoal, and clean pearl white.
Materials/textures: worn hammered metal, soot-dark recesses, subtle scratches and aged patina; smoke should look soft and organic.
Constraints: major shapes must stay legible when reduced to 44×44, 32×32, and 30×30 pixels. This is an original asset inspired by Heroes III art direction, not an extracted game asset.
Avoid: text, letters, readable runes, border, frame, UI, logo, watermark, extra objects, hands, people, neon bloom, excessive particles, modern glossy mobile-game look, flat vector art, cartoon outlines, imposed pixel art, dithering, plastic surfaces.
```

## Focused edit prompt

```text
Edit this original square fantasy spell icon. Remove the hanging metal chain entirely. Keep the censer, its open pierced lid, the thorn-like curse wisps and sickly green poison vapor being burned away into a compact pearl-white smoke curl. Preserve their established shapes, placement, materials, colors, lighting, painterly late-1990s Western PC fantasy illustration style, dark umber background, and overall composition. Ensure the complete censer and lid have a narrow dark-background safe inset on every side, with no part of the censer touching or crossing the image edges. Do not add anything. No text, runes, border, frame, interface, people, or watermark.
```

## Provenance

- Tool: built-in `image_gen` generation, followed by a built-in `image_gen` edit; no CLI/API fallback.
- Initial generation identifier: `exec-3416c160-f3d2-4698-af6f-f59a9b5d6809`.
- Selected edit identifier: `exec-c579dab2-ead7-46ae-a35a-96e54ed818cd`.
- The selected edited image was copied without painting or procedural modification to `master.png`.
- Raster derivatives use the HoMM3 art workflow's `export_art.py` helper with LANCZOS reduction. See `exports-final/NH_spell_purify-manifest.json` for dimensions, methods, and hashes.
- `exports/` contains the first-pass reductions; the selected and installed files come from `exports-final/`.
