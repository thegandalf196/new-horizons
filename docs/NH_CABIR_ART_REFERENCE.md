# Cabir artwork reference baseline

Updated: 2026-10-07. Read before any Cabir generation or rig-proportion work.
This is an art-production reference, not gameplay authority. Gameplay remains in
`design-sources/New Horizons.md`; work and acceptance remain UP253/UP265.

## Selected direction

The user's latest selected reference is `https://i.imgur.com/m7qkfJk.png`.
Base: red-brown reptilian scales, orange eye, swept dark crest, long heavy curved
tail, blue/brass Academy collar, blue apron and belt tools. Master adds gold-scale
armor, reinforced cuffs and a blue/gold tabard. Both use bare hands and shoot fire.
No carried pot, trousers or all-over charcoal/lava skin. The user also requires
coarser late-1990s prerendered creature treatment at native game scale.

The selected design is not approval of every generated draft. All new artwork,
including provisional poses, still uses HoMM3-Art. Original reference provenance
is unresolved; keep closely derived pixels private pending that review.

## Matching private standing drafts

Paths are repository-relative under ignored output; they are not runtime assets.

| Role | Path | SHA-256 |
| --- | --- | --- |
| Base master | `output/homm3/cabir-reference-v4/standing-master.png` | `f7a56d3be1309a0c8664dadea7a735ef67643cecf3a286a6f28da6ef0b9af272` |
| Master original | `output/homm3/cabir-reference-v4/master-standing.png` | `6d63ce6e640719661b1ccd5d65447e273abe6e974a0c954a4d41913637ba19db` |
| Master cleanup derivative | `output/homm3/cabir-reference-v4/master-standing-edge-cleaned.png` | `3ff2d1fb9a6625d25fb280ef4f7271adc42343b608f0ecca4938ead9a319cc31` |

Exact prompts are `prompt.txt` and `master.prompt.txt` in that directory.
The cleanup derivative changes only six isolated alpha1 samples; preserve the
original. Standing drafts are **Provisional**, not final artwork or a walk cycle.
The separate private manual preview uses static poses, not complete animations.

## Superseded references and production hold

Tracked `assets/new-horizons/creatures/cabir/v3/standing-master.png` and the paired
Master are the older lava/trousers design. Their presence in the current runtime
does not make them the reference for the requested replacement. Do not infer art
authority from file version numbers or installed bindings.

The 2026-10-07 `rigged-walk-guide-v3-proportions` adaptation and both
`proportioned-contact-b` studies incorrectly used that older identity. Preserve
their technical/review evidence, but do not install or expand them as the latest
Cabir design. The rig validates mathematical pose samples, not current-design
proportions, whole-sole contact or finished creature artwork.

Before expanding a replacement cycle, establish shared current-design geometry,
camera and proportions across contact/passing/recovery poses. Independently
generated figures that drift in width, height or tail are not a coherent cycle.
Keep the playable version unchanged until actual motion and native visual review
justify replacement; do not claim completion from different frame hashes.
