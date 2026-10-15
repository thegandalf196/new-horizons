# Sorcery study provenance and technical contracts

This directory retains software and provenance for historical Sorcery art studies,
not a production request or current approval ledger. The procedural vector/material
studies were originally authored with Python geometry, Inkscape and Pillow, without
purchaser pixels, downloaded textures or fonts. Later external raster submissions
have separate provenance and rights; they do not inherit the procedural dedication.
Preserve source studies, raw prompts, reference material, alternatives and execution
reports privately. Their historical paths do not promise inputs in a fresh checkout.

The original vector studies used standalone SVG geometry without embedded raster,
fonts, scripts or external references. `export.py` contains their curve/material
and size/rank composition definitions, not imports from a shared generator.
`import-manifest.json` binds recorded source/export names, dimensions, rank/state,
alpha and hashes, including exporter and editable-source identities. Its proposed
runtime filenames and output hashes are not evidence of an actual import.

Ordinary builds use selected resources from `Mods/new-horizons/NewHorizons.nhart`
and current configuration, not study exports or an authoring overlay. Selection,
visual approval, provenance and rendered verification are separate. Neither a
review sheet nor an exporter success selects artwork or certifies gameplay.

## Current consumer contracts

The [school artwork contract](../../../docs/NH_SCHOOL_ART_COMPLETENESS.md) owns
the complete 24-image family, including four proficiency overlays. Historical
20-image studies omitted those overlays and are not a current completeness rule.
The twelve Skill rank/size images alone do not complete a school family.

Keep the current dedicated school-border, header and bookmark bindings; an old
request's inherited Air-border fallback is not current configuration. Bookmark
descriptor group 0 is selected/unselected frames 0/1; school-button group 0 is
normal/pressed/disabled/highlighted frames 0–3. Exported button files alone do not
establish that a button is displayed by a particular interface.

The header is 160×96 with its top 28 rows clear, displayed on page 0 rather than
every page. Native 80×60 bookmarks also require review at compact 68×51. Preserve
the actual consumer placement and offsets instead of treating an original
whole selection strip as separate button states. Exact role geometry, rank/state
ordering and school/All-tab behavior belong in the linked consumer contract and
[asset specifications](../../../docs/NH_ART_ASSET_SPECIFICATIONS.md).

Original secondary-Skill DEF frames use `2 + level + 3 * skillID`; verify internal
IDs before identifying a reference rank. Reference pictures inform native
readability but do not authorize copying source-game pixels or compositions.
Rank recognition and selected/unselected contrast require actual native review,
not color-only distinctions, element counts or a luminance difference alone.

## Alpha, reproducibility and provenance

Preserve complete original masters and sidecars unchanged. Empty `Pillow.info`
does not prove a PNG contains no metadata: chunk-level inspection may reveal a
caBX/C2PA carrier. Its presence alone authenticates neither generator history,
pixel originality nor redistribution rights. `review_external_master.py` records
chunk bounds/CRC/trailer and limits its technical derivative chunks to
IHDR/IDAT/IEND; clean derivatives do not clear the original's rights.

A flattened opaque painting is not a separated foreground. Do not threshold away
dark material, fill real apertures, retain background halos or silently discard
translucent details. Native composition uses actual alpha, premultiplied-alpha
filtering and explicit placement while preserving protected template pixels;
see the [template contract](../../../docs/NH_TEMPLATE_ART_WORKFLOW.md).

Transparent corners do not prove a clear exterior rim or unclipped silhouette.
Correct the source geometry/inset rather than clearing clipped pixels after
rendering. Test dimensions, modes, alpha, margins, distinct ranks/states, source
hashes and deterministic processing separately from native visual quality.
Repeatability binds the actual tools/settings/inputs tested, not all renderers.
No source-image regeneration is part of an ordinary build or this notice.

## Rights and integration boundary

Keep [LICENSE](LICENSE) installed unchanged. It dedicates the contributors'
original procedural artwork/software within its stated scope under CC0-1.0,
not externally supplied raster art, third-party templates, proprietary Heroes III
references or VCMI code. Apply per-file source terms and legitimate attribution;
neither a CC0 utility nor private composition permission relicenses its inputs.
Do not ship purchaser reference sheets or infer clearance from generated output.

Authoring requires complete explicitly configured external inputs and reviewed
tools, separate from current selected runtime bytes. Do not regenerate or replace
selected resources from incomplete study sources. This notice authorizes no new
art batch, import, final approval or rendered/playable acceptance.
