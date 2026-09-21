# Sorcery original-art studies — NOT APPROVED

Two original AI-assisted vector/material studies, authored using Python string
geometry, Inkscape CLI and Pillow. No human commission, generative image service,
model training, downloaded texture/font or purchaser pixels used in these assets.
These remain procedural concepts, not definitive illustration or accepted gameplay.

## Review now

- `meridian/native-review.png`: A — cradled glass orb, tilted brass meridian,
  indigo cloth. Strong circular silhouette and broad directional glass highlight.
- `aperture/native-review.png`: B — suspended oblique lens within a folded brass
  spatial aperture. Angular silhouette distinguishes it from A and elemental art.
- Each sheet shows full native160x96 header (top28 clear), both native80x60
  bookmarks and compact68x51 resamples, all12 skill sizes/ranks, emblem and four
  button states. `nearest-2x-review.png` is supplementary nearest-neighbor only.
- The first sheet labels have a small overlap near Basic; artwork itself is at
  native scale. Individual PNGs are authoritative, sheets are not runtime imports.

Both directions currently cover the full20-PNG contract to permit a meaningful
family comparison. This is NOT a decision to integrate two families. Select one,
refine against concrete review, then import exactly20 approved files. Neither is
user-approved. The gradients currently read cleaner/smoother than the reference;
further material/pixel finishing and selected/unselected contrast need review.
At32px the ivory rank rail is intentionally simplified/countable; larger ranks
also gain physical silver fittings. Rank recognition requires independent review,
not merely counting SVG elements. No color-only rank encoding.

## Editable sources and deterministic route

`<direction>/svg/*.svg` are standalone editable sources; no external references,
embedded raster, fonts, scripts or proprietary data. Named gradient definitions
separate brass, glass, velvet, silver and recessed field. Shape groups separate
object from ground. `export.py` contains original curve definitions and size/rank
composition overrides, not imports from the shared generator.

From repository root:

```
python3 assets/new-horizons/sorcery-art/export.py
# After editing SVGs directly, preserve edits:
python3 assets/new-horizons/sorcery-art/export.py --render-only
```

Requires Inkscape CLI and Pillow. Initial successful environment: Inkscape1.4.2
(1:1.4.2+202510271547+ebf0e940d0), Pillow12.1.1. Native SVG rasterization followed
by Pillow RGBA PNG encoding. No randomness, timestamps or original-asset reads.
Only this isolated directory is written. Repeated byte-identical export has NOT
YET been checked: Build requested another quiet lease immediately after the first
successful export. Initial validation failed on near-transparent skill-frame
corners; increased the authored frame inset rather than stripping alpha pixels.
Final export passed size/RGBA/exterior-transparency/header-margin checks for40.

`import-manifest.json` provides each exact source/draft/runtime filename, dimensions,
state/rank/alpha and SHA256. Manifest also binds exporter bytes. SVG hashes bind
editable delivery sources; runtime hashes are proposed replacement PNG hashes,
NOT evidence those bytes have been imported. Review-sheet hashes will be provided
with the repeatability audit after the quiet window.

## Integration ownership and mandatory gates

Frontend reserved all20 live Sorcery PNGs and matching replacement SVG integration.
Artist must not edit those paths, shared generation, descriptors or configuration.
Build alone imports a reviewed family and adapts generation/audits with Frontend
so future shared generation cannot overwrite approved art. Preserve existing
prototypes and all frozen925/891 candidates. No product edits/commits by Artist.

Keep existing descriptors unchanged in meaning:
- `NH_sorcery_bookmark.json`: group0 frame0 selected, frame1 unselected.
- `NH_sorcery_button.json`: group0 frame0 normal,1 pressed,2 disabled,3 highlighted.
- `config/newHorizonsSchools.json`: existing header/bookmark names.
- `config/newHorizonsSkills.json`: Basic/Advanced/Expert32x32,44x44,82x93,58x64.

CSpellWindow places header117,74 plus offsets on first filtered page; top28 rows
must remain clear. Native80x60 bookmarks scale to68x51 in compact six-school mode.
Emblem/button exports preserve existing resource contract, but their existence is
not evidence of a currently displayed Sorcery button. No invented UI wiring.

Content alone reviews composed game UI on a separately frozen candidate with
Build's quiet-run coordination. Need normal school/All navigation, first/other
pages, selection readability, tooltip alignment, skill hero/level-up/campaign
contexts at actual size, missing-resource logs and source/payload identity.
User must explicitly approve direction and final family. Technical checks, static
sheets, integration ACKs and silence cannot substitute for those gates.

## Offline reference provenance (private pixels excluded)

Actually read original `contact-full.png`, Schools.def group0 frames0..3,
SPELTAB.def group0 frames0..4, SpelBack.pcx and current prototype header through
image reader in purchaser-only ignored research. Schools is160x96 with visible
160x68@0,28; SPELTAB is83x294 whole selection strip, not separate button states.
Observed warm gold edge wear, clustered highlights/creases, coloured fabric folds,
richly illustrated fields. Our scenes do not reproduce faces, waves, trees or
sun compositions. General conventional lens/orb forms are newly drawn.

Used existing local offline LOD/DEF decoder (read and executed an in-memory
adaptation; no original tooling modified), restricting resource names and frames,
writing only `build/new-horizons-linux/research/sorcery-art/references/`.
Actually read resulting native contact pixels for SECSK32.def32x32,
Secskill.def44x44 and SECSK82.DEF82x93. CSkill::registerIcons maps frame
`2 + level + 3*skillID`; enum AIR_MAGIC15 gives48/49/50; SORCERY25 gives78/79/80.
Air ranks show scroll/open book/orb with vapour; original Sorcery shows blue hat,
then candle/skull, then added flask/vapour. No such hat/skull/flask is reused here.
Initial extra45..47/75..77 sample was Fire Magic/Intelligence, NOT Sorcery; corrected
by actual enum and frame mapping before design. References show real composition
progression and shaded material changes, not just abstract badge recolouring.
Private inventory contains archive/resource hashes. No extracted art, archive,
private absolute path or original executable belongs in distributable sources.

## License

All newly authored artwork, SVG sources, preview sheets and `export.py` in this
directory are dedicated under CC0-1.0; see LICENSE. No rights in Heroes III,
reference art or other workers' work are asserted. VCMI code licensing is unchanged.
