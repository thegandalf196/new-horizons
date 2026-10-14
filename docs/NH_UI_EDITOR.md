# Offline New Horizons UI layout prototype

Run `python3 tools/new-horizons-ui-editor/serve.py`, then open
`http://127.0.0.1:8766` yourself. The server binds loopback only, serves seven
fixed files, accepts no uploads and launches no browser/game. No dependencies
or remote services are required; a modern browser with ES modules is needed.

Select layers in the canvas or list. Drag to move; drag the bottom-right handle
to resize. Shift bypasses snapping. Arrow keys move a selection; Shift gives
one-pixel steps. Undo/redo, layer order, visibility, locking, anchors, semantic
bindings and JSON import/export are available. Inspector X/Y are anchor offsets.
Re-anchoring preserves the current rectangle. Lock prevents canvas manipulation,
deletion and reordering; inspector edits remain explicit authoring operations.

Leather is a procedural **editor-only placeholder**. Materials repeat a cell
at its declared dimensions; resizing a panel never stretches a whole background.
Canvas-origin tiling keeps adjacent panels aligned; local-origin tiling restarts
inside each panel. Borders, banners and crests are independent layers. Imported
ornaments use aspect-preserving containment, not background stretching.

Private PNG import is memory-only (8 MiB/file, 4096 px/axis, 32 images/32 MiB
per session, at most 16 megapixels total decoded). JSON includes portable resource references only: no PNG bytes,
base64, original asset copies or local filenames. Export before closing.
After reopening, import each private PNG and set its generated reference in the
appropriate material/layer inspector. Resource references are not fetched.
No DEF/PCX/NHART extraction or asset publication is implemented.
Pending PNG imports merge into the latest layout after decoding, preserving
intervening edits. Reset/JSON replacement cancels their admission; removed,
locked or changed targets and active drags refuse completion. Image budgets
are checked again atomically at commit, including concurrent imports. Rejected
session URLs are revoked. Imports never restore an earlier whole-layout copy.
Pending byte/count/pixel reservations also bound concurrent reads and decodes;
reservations are released when each import succeeds or rejects, including when
a reset-invalidated import finishes decoding.

`level-up.example.json` seeds a 640×568 layout from the current native level-up
positions in `client/windows/GUIClasses.cpp` and the 58×64 CHeroArea in
`client/widgets/MiscWidgets.cpp`. Offer rectangles follow the native 108-pixel
row step. Banner/border treatments, tile cell dimensions and button placeholder
sizes are editor scaffolding, not approved final artwork. `core:DiBoxBck` is a
reference, not a copied raster. Semantic bindings are names, never executable
code or gameplay values.

Version 1 JSON is documented by `layout.schema.json`; `model.mjs` additionally
enforces complete rectangles inside the canvas, material existence, unique IDs
and portable references. This is not a runtime adapter or proof of rendered
game behavior. No client/config/package files are written. Final game integration,
font metrics, assets, responsive behavior and native visual review remain separate.

Focused checks: `python3 -m unittest tools.tests.test_new_horizons_ui_editor`.
These exercise the actual model and server admission without launching a browser.

Offscreen verification on 2026-10-13 used installed Chrome 155 with a disposable
profile and loopback server, without a host display. Actual rendering, mouse
drag/resize, undo/redo, JSON file import/export, synthetic PNG ornament import
and tile-size preservation after panel resize were checked. Two editor previews
were captured. The harness exited 1 because it expected a second download filename;
Chrome overwrote the same export filename. The resulting final JSON was independently
checked for the edited geometry, tiled material and reference-only/privacy contract.
The original failed harness result is retained; all observed processes exited and
both owned ports closed. This is scoped editor evidence, not game integration or
final-art approval. A human authoring trial and broader browser coverage remain open.

## Offline native-mapping check

**Check native mapping** produces a read-only diagnostic report in the inspector.
Regenerate it after an edit; it is a snapshot, not live validation. The same report
is available without a server/browser:

```sh
node tools/new-horizons-ui-editor/validate-layout.mjs tools/new-horizons-ui-editor/level-up.example.json
```

Valid layouts return exit0 and a version1 `new-horizons-ui-translation-report`.
Malformed/unreadable/nonregular/oversized input returns exit2 without echoing
private paths or input excerpts. Output goes to stdout; no game/config/art file is
written or loaded. All reports explicitly have `runtimeReady: false`.

The actual level-up consumer is hardcoded `CLevelWindow` in `GUIClasses.cpp`;
`InterfaceObjectConfigurable` is only a reference contract, not an active adapter.
Reports resolve all anchors to validated integer rectangles and identify narrow
portrait/button positions, centered-label positions/widths and canonical primary
icon positions. Known point proposals also retain current approved reference positions and an
equality flag; differing positions are proposals, not automatic UI changes.
Offer rows and combined primary gain/name placeholders are only
group envelopes: native groups/labels cannot be replaced independently from these.
Native skill/perk values, ranks, offer count, callbacks, shortcuts, text/font
metrics, images and dynamic visibility remain unresolved and engine-owned.

Every layer, including hidden and unknown bindings, remains in the report with
its blockers. Duplicate roles and incompatible layer types are flagged. A local
tiled material has a rectangle proposal but unverified native cell dimensions;
canvas-origin tiling is explicitly incompatible with the current configurable
`CFilledTexture` builder's panel-local phase. Private PNG references and procedural
ornaments are never represented as packaged engine assets. These reports are
geometry proposals, **not loadable widget JSON or authorization to change the
approved shipping layout**. Existing JSON import/export is unchanged.
