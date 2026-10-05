# New Horizons Academy art handoff

This directory preserves the user-supplied generated masters, prompts, safe
native-size exports, and the package's registration/provenance metadata. The
author-supplied art remains under its author's rights; this directory does not
assign it a CC0 or other license. The VCMI code and import tooling retain the
repository's existing licensing.

The archive is a visual-art handoff, not an installable mod or compatibility
certificate. Its own `VALIDATION.json` and
`integration/export-validation.json` report that VCMI/in-game testing was not
performed. The package's `integration/vcmi-tower.json` was byte-identical to
the repository's current `config/factions/tower.json`; that duplicate gameplay
file is intentionally not imported or used as the source of Tower rules.

## Pixel provenance and exclusions

The imported masters are the generated Academy art authored for this handoff.
The native siege exports use source-game transparency silhouettes as geometry;
the puzzle pieces use source-game piece masks/positions. Those sources supply
shape/registration only, not original game colors. The supplied Tower landscape
is an authored edit of the source town background; prompts and registration
metadata are retained so its derivative provenance is explicit.

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
