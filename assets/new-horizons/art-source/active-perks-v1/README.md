# Active perk and capability paintings, provisional v1

Original built-in image generation, 2026-09-20. These are provisional, not
user-approved definitive paintings. Four original 1536x1024 atlases supply
22 missing active-perk subjects and Leadership/Siege capability paintings.
The existing Bone Collector and all approved school/skill art are preserved.
War Drums changes presentation only; no Bloodrage mechanics are changed.

`generation.json` retains row-major cell identity and measured crop rectangles;
verbatim generation requests and complete masters remain in the external
authoring archive. No input image references were supplied. Atlas row boundaries
vary; rectangular crops are explicitly contained on square dark-umber canvases,
without stretching, before the retained standalone helper performs LANCZOS reduction.
States use only brightness/color adjustment of the 44px normal reduction.

`export.py` requires complete external atlas inputs and an absolute path to the
retained standalone `export_art.py` helper supplied via `--export-helper`;
no plugin installation or activation is required. Public recipe metadata alone
cannot recreate missing masters. It reproduces mechanical exports into a clean
destination and refuses existing outputs. runtime-manifest.json pins installed files.
The reproducible per-cell crops and helper comparisons are omitted from source
control to avoid duplicating master pixels. The historical external
`native-review.png` review covered 44px, 32px and enlarged 44px images in
atlas/cell order; it is not a shipped public preview. All 24 subjects were inspected
at those sizes: distinct major silhouettes and value groups remain identifiable.
Fine ornament is intentionally lost at native size. No native game-window
acceptance is claimed by this audit.

Runtime binding uses client/windows/NewHorizonsPerkIcons.h for all active perk
IDs, including the preserved Bone Collector. Unknown/planned IDs retain the
neutral fallback. Capability art is shown in the new hero layout and legacy
strip; Siege still describes saved capability ranks, not spendable points.

Artwork and mechanical export source: CC0-1.0.
