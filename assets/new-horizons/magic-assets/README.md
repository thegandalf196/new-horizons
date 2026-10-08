# Standalone hero casting glows

The user explicitly authorized redistribution of the supplied new symbols and
glows on 2026-10-08 (UP315). That authorization is the publication basis; visual
similarity or inclusion in a supplied archive is not itself a rights clearance.

This import contains only the 864 standalone transparent casting glow overlays:
18 native hero animation keys, six schools, and eight frames per school. They
are byte-identical to the supplied corrected v6 `glow_overlays` PNGs imported
by `tools/import_new_horizons_magic_assets.py`, retained in the private v2
import and previously delivered snapshot b26652d8. The source collection archive
SHA256 is `1d5448d3f5996c50ec93865beb865dd45f9d93164009c5f6c72c6cddceb699e1`.

Runtime images are under
`Mods/new-horizons/Images/NH_magic_assets/casting/`; the builtin resolver
`config/newHorizonsMagicAssets.json` contains only `castingGlows`. No original
hero DEF, base animation frame, purchaser-original scenery, clean plate, or
private Mage Guild background composite is included. The separate guild
composites remain private and are not covered by this import.

All imported files are RGBA PNGs at 150x175, with transparent canvas pixels.
The set totals 632271 bytes; 72 entirely transparent fade frames are retained
unchanged. No frame has an opaque canvas, and the largest nonzero-alpha region
occupies less than 2.724% of its canvas. These structural checks complement
the standalone-overlay source provenance; they do not grant rights to other art.

No raster editing or new image generation was performed. Native rendering and
normal playable delivery are separate acceptance gates.
