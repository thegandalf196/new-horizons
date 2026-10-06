# Magi runtime palette treatment — first review

These authored RGB instructions reference purchaser-supplied DEF resources;
they contain no original sprite pixels. The originals remain external and
read-only. This is a selective engine palette treatment, not generated artwork.
HoMM3 Art's material/value guidance informs review; no image generation is
claimed for these numerical mappings.

`maps.json` is the source for the six JSON aliases emitted by
`tools/create_new_horizons_magi_palette_aliases.py`. CAMAGE preserves all 18
groups and 133 frames; PMAGEX preserves nine projectile frames; the large
portrait references TWCRPORT group 0, frame 37. Creature-specific aliases also
select CPRSMALL frame 37 and AvWattak frames 70/71 without modifying the shared
source palettes. AVWmagx0 retains its 30 map-animation frames and 64x64 canvas;
its creature template retains the original 2x2 anchor footprint and approaches.

The Arch Mage robe core and shadow entries were selected from the original
indexed standing frame and palette census. The four confirmed battle staff
glow entries become red. Gold/skin indices and the already-red portrait orb
remain unchanged. The base Mage robe is untouched. Projectile green entries
become red without changing their shading, dimensions or transparency.

Status: provisional. Native tests and rendered holding/shooting/projectile/large
portrait inspection pass. Full animation motion and in-game approval are open.
Single-frame semantic evidence does not prove that every palette index is
exclusive to cloth throughout the animation. Small portraits and adventure
map creature sprites now have source bindings and passing native pixel/template
checks. Selected private previews show grey robe cores and red staff accents;
playable delivery and full motion approval remain pending. Their resource-specific maps
leave uncertain skin, brown trim and pale highlights unchanged. Never label this
treatment final merely because its descriptors validate.
