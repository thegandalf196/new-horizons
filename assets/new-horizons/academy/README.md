# New Horizons Academy art handoff

This directory preserves selected-runtime receipts, rights notices and the
package's registration/provenance metadata. Exact prompts, authoring notes,
historical validation reports, loose masters, exports, composites and review
images are preserved privately outside the checkout, not published as
development inputs. Selected runtime bytes ship only in
`Mods/new-horizons/NewHorizons.nhart`; ordinary builds and tests use that pack
without a private overlay or source-image fallback. Historical source paths
below identify private authoring inputs, not files promised in a fresh checkout.
The author-supplied art remains under its author's rights; this directory does not
assign it a CC0 or other license. The VCMI code and import tooling retain the
repository's existing licensing.

Authoring is opt-in: `import_new_horizons_academy_assets.py` requires an explicit
external `--root`; portrait exporters require external `--private-root`.
Use a private project-shaped workspace containing the recorded inputs and
metadata. These tools reject checkout-local destinations, including `--check`.
Never regenerate the shipping pack from incomplete local inputs. Public tests
verify actual NHART payloads and use minimal disposable synthetic images for
importer pin, geometry and safe-install tests; they do not claim to reproduce
private masters. Retained hashes preserve provenance independently of runtime
selection or visual approval.

The original archive is a visual-art handoff, not an installable mod or
compatibility certificate. Its historical dimension/export reports were not
engine acceptance. `handoff/PACKAGE-MANIFEST.json` is an unchanged inventory of
that external archive, not a list of files available in a fresh checkout.
Historical prompt paths and digests in revision manifests identify external
authoring inputs; retained digests do not grant access or new licensing.
Authoring checks require the complete private project-shaped inputs, not merely
this checkout or the package inventory. The package's Tower gameplay snapshot
is not imported or used as the source of Tower rules.

## Pixel provenance and exclusions

The imported masters are the generated Academy art authored for this handoff.
The native siege exports use source-game transparency silhouettes as geometry;
the puzzle pieces use source-game piece masks/positions. Those sources supply
shape/registration only, not original game colors. The supplied Tower landscape
is an authored edit of the source town background; its derivative provenance
and registration remain explicit even though its prompt is private.

The importer deliberately does not copy these mixed package exports into this
directory or the runtime module:

- `native/siege/sgtwdrw1.png`, `sgtwdrw2.png`, `sgtwdrw3.png`, and `sgtwdrwc.png`
  retain the original wood gate pixels. Siege gate assets continue to resolve
  from the user's external original-game installation.
- `native/adventure/avctowr0.png`, `avctowx0.png`, and `avctowz0.png` restore
  original semitransparent cast shadows. Runtime map bodies are registered
  mechanically from the generated masters; shadow/ownership layers are
  generated from the user's external original DEF frames at runtime. The clean
  192x192 body exports are not copies of the mixed package-native frames.
- The four `native/ui/icons/*-built.png` files retain the original built badge.
  Those mixed exports are not copied. Each built-icon resource has a
  byte-identical normal-art fallback (no badge pixels) so content validators
  can resolve every configured path; the client prefers its runtime-generated
  composition of normal art plus only the original normal/built badge
  difference. No original badge pixels are committed.
- Package previews, including the original-comparison preview, are not runtime
  art and are not imported. The archive's copy of the Tower gameplay JSON is
  also not imported.

`tools/import_new_horizons_academy_assets.py` is the reproducible importer and
registration recipe. It validates ZIP paths/types, refuses to overwrite
unexpected existing files, registers every Town building frame (including the
animated frame ranges), replaces all 44 Tower hall-icon frames while retaining
the semantic 33/34 and 40/41 swaps, and writes an ignored all-built comparison
preview under `build/new-horizons-academy/`. That offline preview is useful for
layout review only; it does not prove in-game compatibility.

The active art-only faction patch is
`Mods/new-horizons/Content/config/factions/academyArt.json`. It keeps
`core:tower`, creature/building rules, costs, requirements, entrances, and map
blocking geometry intact. Academy uses sand as its native terrain as explicitly
selected by the user. Original gate references remain external.

## Town-icon revision v2

`icon-revisions/v2/manifest.json` identifies the separately generated
fort/village masters and four selected native exports. They replace the
distant sandy icon compositions with closer ivory architecture, teal roofs and
clear light/shadow separation. Large faction-selection icons remain 58×64;
small adventure-list icons remain 48×32 with the native one-pixel black frame.
Original and revised authoring images remain private; selected runtime images
are read from the committed pack.

The importer explicitly pins the reviewed v2 manifest and installs only its
hashed exports, including byte-identical built-slot fallbacks. Reimporting the
original handoff cannot silently revert this revision. Runtime built markers
still come only from the purchaser's external DEF frames. Mechanical reductions
use `tools/export_new_horizons_academy_icon_v2.py --check` with its required
external workspace and complete pinned inputs.
Faction-selection names are white; the existing activated border marks selection.

## Bounded town revisions

The Village Hall revision mechanically crops the generated master's alpha bounds
and fits its visible content into `(0,2)-(177,75)` on a 177×75 transparent canvas.
The revised raster is not claimed to retain the original facade pixels or alpha
contour. Keep the original scene placement and independently registered click
and hover masks; artwork transparency is not an interaction mask. The revision
remains provisional, not final-art or rendered-game acceptance.

The background revision reduces its generated master to 800×374 and replaces
only `(0,254)-(177,335)`, with right/bottom bounds exclusive. Every pixel outside
that rectangle remains unchanged from the retained native baseline. It removes
a painted duplicate hall without changing the separate hall's placement, layer
order, sprite or interaction masks. Registered mechanical composites are useful
for alignment review but are not game screenshots. The background and hall
exporters require explicit external `--private-root` inputs even in check mode.

## Registration and verification contract

- Keep the compatible `core:tower` identity. Academy's selected sand terrain is
  a gameplay property, not a reason to repaint old maps or change unrelated
  faction rules. Existing creature costs, requirements, classes and mechanics
  are not overridden by an art handoff.
- Preserve adventure entrances, blocked tiles, anchors, draw order and terrain
  eligibility separately from PNG alpha. Verify ownership colors, dynamic flags
  and shadows against external original resources rather than bundled originals.
- Town layers require independent build-stage visibility, z-order, click/hover
  masks and upgrade replacement. The joined guild/wall preview does not itself
  define every Mage Guild stage. Keep puzzle piece positions/reveal order and
  construction/campaign frame mappings, including intentional repeated frames.
- Static illustrations do not certify complete animation coverage. Register
  frames deliberately; do not accidentally mix revised first frames with old
  snowy later frames. Test siege gate/wall damage states, shooter occlusion,
  seams and mine visuals without changing trap mechanics or ordinary battlefields.
- Public tests verify real selected pack bytes, dimensions, masks, descriptors,
  external-pixel exclusions and safe installation. Disposable synthetic images,
  prompts and ZIP metadata exercise importer tamper/missing-input refusal;
  those fixtures are not reproduced authoring art or acceptance receipts.
- Source hashes, offline previews and schema checks do not establish rendered
  town/map/siege behavior, save/reload, campaigns, player-color coverage or
  playable delivery. Verify those separately before claiming acceptance.
