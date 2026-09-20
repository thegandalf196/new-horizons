# Active perk and capability paintings, provisional v1

Original built-in image generation, 2026-09-20. These are provisional, not
user-approved definitive paintings. Four original 1536x1024 atlases supply
22 missing active-perk subjects and Leadership/Siege capability paintings.
The existing Bone Collector and all approved school/skill art are preserved.
War Drums changes presentation only; no Bloodrage mechanics are changed.

generation.json records exact prompts, row-major cell identity and measured
crop rectangles. No input image references were supplied. Atlas row boundaries
vary; rectangular crops are explicitly contained on square dark-umber canvases,
without stretching, before the homm3-art skill helper performs LANCZOS reduction.
States use only brightness/color adjustment of the 44px normal reduction.

export.py reproduces mechanical exports into a clean destination with the
homm3-art scripts/export_art.py helper supplied via --export-helper.
It refuses existing outputs. runtime-manifest.json pins installed files.
The reproducible per-cell crops and helper comparisons are omitted from source
control to avoid duplicating master pixels. native-review.png presents 44px,
32px and enlarged 44px images in atlas/cell order. All 24 subjects were inspected
at those sizes: distinct major silhouettes and value groups remain identifiable.
Fine ornament is intentionally lost at native size. No native game-window
acceptance is claimed by this audit.

Runtime binding uses client/windows/NewHorizonsPerkIcons.h for all active perk
IDs, including the preserved Bone Collector. Unknown/planned IDs retain the
neutral fallback. Capability art is shown in the new hero layout and legacy
strip; Siege still describes saved capability ranks, not spendable points.

Artwork and mechanical export source: CC0-1.0.
