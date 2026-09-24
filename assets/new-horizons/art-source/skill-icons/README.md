# New Horizons faction skill icon provenance

This directory records the nine faction-skill families that are installed in
`Mods/new-horizons/Images/`. Each family has Basic, Advanced, and Expert
variants in the four module slots (`small`, `medium`, `large`, and
`scenarioBonus`). Seven generated families use a high-resolution square master
generated with the host's built-in image generator and deterministic
downsampling in `../export_skill_icons.py`; their installed rank variants are
the same still life with restrained contrast/saturation shifts and three rank
pips. Metamagic is a versioned exception: three rank-specific masters and
no-stretch native exports are recorded in
`../metamagic-prisms-v2/manifest.json`, and the new
`NH_metamagic_prism_*` bindings are separate from the historical single-master
exports. Do not use `../export_skill_icons.py` to produce the active Metamagic
family. Source classification is recorded in `faction_skill_icon_manifest.json`.

Necromancy is deliberately different: its Basic/Advanced/Expert entries refer
to frames 39/40/41 of the purchaser-supplied `SECSK32`, `SECSKILL`, and
`SECSK82` resources. No original-game pixels are copied into this repository or
the distributable module. This preserves the classic icon while honoring the
fork's no-original-assets packaging contract.

## Visual identity briefs

| Skill | Dominant still-life identity |
| --- | --- |
| Divine Mandate | Gold crown, ivory sun medallion, and red velvet command regalia; sacred authority. |
| Sylvan Luck | Silver antler branch wound with wet green leaves, clover, and amber berries; forest fortune. |
| Metamagic | Rank-specific prism mounts with one, two, and three outgoing rays; see `../metamagic-prisms-v2/README.md`. |
| Shroud of Malassa | Cracked black mask beneath a heavy purple veil on volcanic stone; stealth and shadow. |
| Demonic Gating | Massive horned iron gate opening onto a red inferno, with chain and basalt; summoning portal. |
| Bloodrage | Blood-wet battle axe embedded in a crimson crystal slab; escalating violence. |
| Bulwark of the Mire | Moss-grown iron shield rooted in swamp water and reeds; defense and mire. |
| Elemental Rebirth | Cracked luminous egg surrounded by fire, water, earth, crystal, and a rising gold wing; elemental return. |
| Necromancy | Canonical classic skulls surrounded by red necromantic flame, with rank-specific frame 39/40/41 progression. |

Run `tools/tests/test_new_horizons_faction_skill_icons.py` for the focused
config, dimension, uniqueness, provenance, and no-copied-Necromancy audit.
