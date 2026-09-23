# Warcasting perk art v1

Three original provisional icons were generated with the OpenAI built-in image
generator on 2026-09-23, without reference images. The exact prompts are in
[`prompts.md`](prompts.md). They are not final user-approved artwork.

The 1254x1254 RGB masters and LANCZOS-reduced 44x44 and 32x32 previews are kept
under `exports/`; each has a labeled comparison sheet and export manifest.
`export-runtime.py` derives the VCMI 44x44 `normal`, `pressed`, `disabled`, and
`highlighted` button states from each 44x44 preview using brightness and color
adjustments only. `generation.json` records source provenance, prompt anchors,
master dimensions, and SHA-256 hashes. Runtime output hashes are in
`runtime-manifest.json`.

The generated source art and deterministic export code are contributed under
CC0-1.0, consistent with the project's other original perk-art sets.
