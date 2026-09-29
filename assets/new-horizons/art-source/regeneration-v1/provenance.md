# Nature Regeneration icon provenance

Status: **Provisional**. This is an original concept asset for integration review; it is not final-art approval.

## Generation and selection

- Tool: host built-in `image_gen` (no API/CLI fallback).
- Taxonomy: `stylized-concept`.
- Style reference: none. The first generation was original; its own output was used once as an edit target to correct edge clipping.
- Selected master: [`selected/master.png`](selected/master.png), 1254×1254 RGB.
- First pass retained for provenance: [`draft-initial/master.png`](draft-initial/master.png). It was superseded because the branch ran out through two edges.
- Final generation input: `$HOME/.codex/generated_images/01a0ea4d-f341-7881-8e9d-2d158fc97a66/exec-b3ea18cd-1ae1-41bc-8884-e994cb8d9019.png`.
- Export: `homm3-art/scripts/export_art.py`, LANCZOS reduction at 44×44, 32×32, and 30×30. The selected export manifest and comparison sheet are in [`selected/`](selected/).

## Exact prompts

Initial generation prompt:

```text
Use case: stylized-concept
Asset type: Heroes III inspired game spell icon, high-resolution square master for reduction to 44×44, 32×32, and 30×30.
Primary request: Create an original miniature fantasy still-life painting for the Nature Regeneration spell. A living branch is visibly wounded and actively knitting itself back together.
Scene/backdrop: subdued, dark earthy forest umber with softly textured bark-like atmosphere, low detail behind the focal object.
Subject: one thick, curved living branch filling most of the square; a clear split and ragged wound through its bark is visibly closing as raised wood fibers draw together, bridged by a small seam of fresh emerald sap/light. Include only a few fresh leaves or a budding shoot emerging near the healed wound. Keep the injury and the regrowth unmistakable at tiny icon size.
Style/medium: original late-1990s Western PC high-fantasy illustration, a richly modeled physical object with pre-rendered volume and painterly fantasy-book character; not photorealistic.
Composition/framing: close-up centered emblematic still life, narrow safe inset, large coherent shapes and simple silhouette; branch is the sole dominant object, with no clipping.
Lighting/mood: theatrical chiaroscuro, warm amber hero-light on one bark edge and deep shadow on the opposite side; small concentrated emerald highlights only in the sap seam and new buds.
Color palette: weathered bark browns, muted olive leaves, restrained jewel-green sap against deep earthy umber; keep the green localized, never neon.
Materials/textures: rough cracked bark, fibrous exposed wood, glossy but natural sap, soft living leaf surfaces, all materially distinct.
Constraints: original artwork; detailed high-resolution square master designed to remain legible after LANCZOS reduction to 44×44, 32×32, and 30×30; no readable wound text or symbols.
Avoid: lettering, runes, ornamental frame, border, UI chrome, extra props, visible creatures or hands, neon bloom, abundant particles, vector shapes, cartoon outlines, plastic gloss, pixel art, dithering, intentional pixel clusters, modern mobile-game aesthetic, watermark.
```

Single composition-edit prompt (Image 1 was the first pass as edit target; no style reference):

```text
Use case: stylized-concept
Asset type: Heroes III inspired game spell icon master for reduction to 44×44, 32×32, and 30×30.
Input images: Image 1: edit target; preserve its wounded living bark, healing sap seam, new leaves, material rendering, light, palette, and earthy mood.
Primary request: Make a focused composition revision to Image 1 for Nature Regeneration. Keep the same single living branch visibly knitting its wound closed.
Subject: retain one thick curved branch with raised fibrous bark drawing together around a narrow restrained emerald sap seam and a few fresh leaves/buds.
Composition/framing: reframe the branch as a complete emblematic still-life object wholly inside the square, with both ends and all leaves visible and a narrow safe inset on every side. Let it fill most of the canvas without touching or crossing any edge; keep the wound near the center and recognizable at tiny icon size. Avoid a large empty field.
Style/medium: preserve the same richly modeled late-1990s Western fantasy illustration character, physical bark, fibrous exposed wood, natural glossy sap, and painterly pre-rendered volume.
Lighting/mood: preserve theatrical warm amber light on one bark edge, deep shadow on the other, and small concentrated emerald highlights only at the healing seam and buds.
Constraints: change composition/framing only as needed to contain the branch; keep the same subject and overall color/material treatment; detailed square master designed for LANCZOS reduction.
Avoid: cropped branch, clipped leaves, extra branches, trunk stump, hands, extra props, lettering, runes, border, UI chrome, neon bloom, abundant particles, vector shapes, cartoon outlines, intentional pixel art, watermark.
```

## Visual review

- The wound's split fibers, narrow emerald healing seam, and new leaves remain identifiable in the 44×44 and 32×32 native exports.
- At 30×30 the branch and regrowth remain recognizable, but the healing seam is reduced to a small green notch; verify its contrast in the actual battle/effect UI context.
- The final master keeps the complete branch inside the square with a small safe inset. The work is highly modeled and earthy, with restrained emerald light and no frame, lettering, or watermark.

## SHA-256

| File | SHA-256 |
| --- | --- |
| `selected/master.png` (1254×1254) | `b8d9405792e3f617b7af1d8b1cf05f4cff6ff88ca5ca77ac9fa22f300b80fc6a` |
| `selected/NH_regeneration-44.png` | `8ee6032ae0b8c8fb38090739324d604ffe429dd4a2774427a5687f1a010cf0a1` |
| `selected/NH_regeneration-32.png` | `93ac89308d927fbc98a8aedfc78b8ffab6fa8a7ad0fe09a2c3f41dddd8155861` |
| `selected/NH_regeneration-30.png` | `cd1e4d882ed74c58f640537afd0410cb7f126c0b2322469ea020f350c55387fa` |

The matching installed copies are `Mods/new-horizons/Images/NH_regeneration_44.png`, `NH_regeneration_32.png`, and `NH_regeneration_30.png`; each has the same SHA-256 as its correspondingly sized selected export.
