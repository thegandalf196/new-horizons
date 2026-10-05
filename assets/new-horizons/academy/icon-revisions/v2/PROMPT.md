# Academy town-icon revision v2

Generated 2026-10-05 with the host's built-in image generator, following the
Heroes III Art Workshop skill. These are original replacement illustrations,
not extracted game scenes. The purchaser-installed native faction-icon contact
sheet was a style/framing reference only; it remains in ignored `build/` and is
not included in the deliverable. The existing Academy comparison supplied
architectural identity. Both generated masters are retained intact. Native
derivatives use the recorded mechanical crops and LANCZOS reduction, not painted
overlays. Earlier handoff images are preserved.

## Fort — exact generation prompt

Use case: stylized-concept. Asset type: Academy faction fortified-town icon for a Heroes of Might and Magic III-inspired game. Reference image 1 is the native faction-icon contact sheet: match its tight architectural framing, strongly separated light/dark masses and late-1990s prerendered fantasy look, NOT its specific buildings. Reference image 2 is the current Academy comparison: retain Academy's identity as an ivory desert wizard citadel with teal-topped spires, but correct the washed-out yellow-on-yellow distant aerial composition. Create ONE tall portrait-format original image, approximately 58:64 aspect ratio, no sheet. A close three-quarter view of an imposing pale ivory-stone magical citadel, a broad central tower and two subordinate towers, restrained deep turquoise roofs, thick lower walls, deep dark blue-violet recesses. Architecture fills almost all the frame, foreground walls cut naturally by bottom edge, no huge sand foreground. Cool desaturated blue sky behind the upper silhouette; small warm sandstone foothills low at sides so this is still a desert Academy. Strong crisp lit stone planes and dark openings distinguish it from the background at 58x64 and at a center-cropped 48x32 version. Dense economical composition, authentic miniature modeled fantasy architecture, fine detail secondary to broad readable shapes. No text, border, lettering, interface, glows, built-today badge, gold ornaments around the frame, watermark, flat vectors, or modern mobile-game rendering.

## Village — exact generation prompt

Use case: stylized-concept. Asset type: Academy unfortified-village town icon for a Heroes of Might and Magic III-inspired game. Create ONE portrait image approximately 58:64 aspect ratio. Reference 1 is the native icon sheet for compact architectural framing and strong light/dark silhouette, not buildings to copy. Reference 2 is the new Academy fort for matching ivory stone, restrained teal roof, deep blue-violet shadows and desert identity. Create its modest village counterpart: a close three-quarter view of an ivory stone wizard academy hall, broad dark recessed columned portico, a single low turquoise dome and two small rooftop finials. Building fills most of the frame, with its front facade in the central region so a landscape center crop still identifies it. Cool muted cloudy blue sky in the top third, warm sandstone foothills visible narrowly at sides, negligible empty sand foreground. Dense original late-1990s modeled fantasy architecture, matte worn stone, bright lit planes against deep portico shadow, understated detail. Paint a smoothly modeled high-resolution master; do NOT impose dithering, checkerboards, pixel clusters or mosaic texture. Reduction will create the small raster appearance. No text, borders, lettering, interface, badge, magical glow, watermark or modern mobile-game gloss.

## References and role distinction

- Fort: ignored native ITPT contact sheet (style) and Academy/native comparison
  (existing Academy identity and edit direction).
- Village: ignored native ITPT contact sheet (style) and newly generated fort
  master (consistent material, palette and architectural identity).
- Large exports: faction selection, 58×64. Small exports: adventure town list,
  48×32, intentionally closer landscape crops rather than distorted portraits.
- Lettering belongs to the UI, not these images. The existing activated frame
  retains selection feedback while faction names are rendered white.
- Built-today markers continue to be composed at runtime from the user's
  installed original resources; no original badge pixels are bundled here.

## Explicit export geometry

Both masters are 1194×1317. Large exports use rectangle `(2, 2, 1191, 1314)`
and reduce to 58×64. Small fort content uses `(22, 0, 1172, 750)`; small
village content uses `(22, 180, 1172, 930)`. These exact-aspect crops reduce
to 46×30 inside the original 48×32 canvas. A uniform opaque black one-pixel
perimeter matches the native adventure-town icon framing; it contains no copied
source artwork. `tools/export_new_horizons_academy_icon_v2.py --check` verifies
the mechanical outputs. No geometric stretching or arbitrary sharpening is used.
