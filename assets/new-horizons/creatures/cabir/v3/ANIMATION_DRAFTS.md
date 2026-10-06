# Barehanded animation sources — provisional

These original project images were made with built-in image generation using
the HoMM3 Art skill. Exact generation/edit prompts accompany every candidate.
The v3 standing masters are the identity references; base action sheets also
serve as pose references for their upgraded equivalents. All artwork is
CC0-1.0 original project work, not extracted purchaser sprites.

New source sheets, viewed by root after generation:

| Creature / action | Source PNG SHA-256 |
| --- | --- |
| Base upward/downward melee | c6a7b5831041f977668a3cdb75ff58707dbf31c2fd2c4e5021974fc3c9acbbf6 |
| Master front melee | d3edb6e641348b06e30bcf4c6495c5eb8e18473317b7a300b441b28a2040bd6f |
| Master reactions/death | ed5bc5c94942d7c5c5c7fbc0b1712db52596ce00d94894608fdefa3eb887cf2f |
| Master front shooting | 921c65c91a194dd56da044ed6f6299322e14f083357229f11534c73791e38dfa |
| Master upward/downward melee | 758899e21dbec3c4417d1832aea4ea792e20073a1afc20af04f31d10f0969d91 |
| Master upward/downward shooting | a1525239c00440b83fed0c3570f4a48fe2b64868a39b5621e1e325365433a0e4 |
| Master repair casting | c6232b9f16eb4df4d42e53f8534f0e20053d91c16698b128fd7c0b6f5fb78bc8 |

All six are RGBA PNG sources in their respective v3 action directories. The
reaction sheet is 1326×1187; the other new sheets are 1323×1189. Each is a 2×2
row-major atlas. Front attacks/shots show ready, windup, release and recovery.
Directional sheets show upward windup/release, then downward windup/release.
Reactions show recoil, brace, dying and collapsed dead. Both forms use empty
bare hands; the Master keeps dark iron shoulder armour and a reinforced collar.
Shooting sheets depict concentrated palm embers, not a separate flying missile.

These are **Provisional** sources, not completed runtime animations. Extended
limbs can cross nominal grid seams: preserve whole silhouettes, never crop a
quadrant blindly. Alpha isolation, shared cross-action scale and ground anchor,
timing, release points, directional bindings and native motion review are still
required. New frames are not installed into the normal playable snapshot.

The Master walk-v1 source has an offline reproducible `cleaned-v1/` export:
10,660 exterior pixels removed, maximum alpha 4; masters/retained pixels are
unchanged. The pinned wrapper's `--check` reproduces its atlas, four 450×400
frames, contact sheet and GIF. Root inspected the contact: visible coloured
fringe specks remain, including two isolated source pixels at alpha 17/19 that
the conservative cleanup policy retains. Gait and cross-action scale are not
accepted. This bundle is **withheld from runtime**, not a approved walk cycle.

Base front-melee and reaction sources now have whole-component alpha separation
under `separated-v1/`. Only faint exterior alpha was cleared (melee 12,869 pixels,
maximum alpha 5; reactions 12,534 pixels, maximum alpha 4). Masters and retained
RGBA are unchanged. Offline native contacts show readable unclipped poses, but
their per-group reference scales need cross-action calibration before delivery.

The repair sheet adds ready, reaching, bare-palmed repair focus and recovery
poses. Its exact prompt has SHA-256
`9db44d47b0ee986963e9eb9e98d34b3a416a3e9411c8419c45e0e0c4b22673d6`.
It is original Provisional art; runtime casting group 31 and directional fallback
bindings still require calibrated export and native resource verification.

Still missing: final common calibration/animation bindings,
ranged projectile origin/release calibration, map animation, portrait/icon exports and
playable presentation acceptance. Standing, four-pose walk and action-source
existence do not establish a finished creature.
