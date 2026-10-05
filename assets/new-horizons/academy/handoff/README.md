# New Horizons — Academy definitive art package

In New Horizons, we’ll shift the Wizard faction away from the purely snowy, alpine Tower identity of Heroes III and toward a more Persian-Islamic scholarly civilization, while still preserving the core themes of arcane knowledge, astronomy, alchemy, and magical engineering.

The faction’s visual language will draw from Persian, Islamic, and Central Asian architecture.

## Final selections

- Town: review-2 yellow sandstone landscape/buildings, the discreet Hall of Knowledge without clustered decorative spires, and the final basic/upgraded Gremlin Workshops. Previous roof and spire-entrance repairs are included.
- Adventure map: the three explicitly approved revision-3 sprites, restored byte-for-byte, and their eight matching UI icons. Later repaints are excluded.
- Siege: desert background, mine, stone architecture and damage states; original wood gate pieces retained.
- Other interfaces: recruitment backgrounds, guild window, construction-menu images, campaign building thumbnails and puzzle mural/pieces.

Open [PREVIEW.html](PREVIEW.html) first. [Town scene](previews/town-final.png) · [Workshop stages](previews/workshop-basic-upgraded.png) · [Three map variants](previews/map-three-variants.png).

## Folder guide

| Folder | Contents |
|---|---|
| native/ | Selected PNG exports at original game canvas sizes; use these for integration |
| masters/ | Selected editable-resolution art masters and available generation prompts |
| previews/ | Final assembled scenes, approved map lineup and original-town comparison |
| integration/ | Resource mappings, town layout, puzzle positions, icon indices, prompts and VCMI checklist |

This is an **art handoff ZIP, not an installable VCMI mod**. Native terrain should use VCMI's sand identifier. See the [implementation checklist](integration/VCMI-CHECKLIST.md).

The authoritative assembled guild is native/town/corrections/guild-and-wall.png; the old superseded standalone top-level guild layer is intentionally excluded. The 37 Tower building resource identifiers are mapped in [town-layout.json](integration/town-layout.json), with the joined guild overriding its original layer. The lower wall crop and the separate scenery-roof patch are also supplied. Production must still separate guild/wall visibility across build stages, register click/highlight masks and provide animation frames where required. The high-resolution guild master alone does not reproduce its preserved original tower pixels; use the supplied native composite as the final appearance.

Basic workshop: tbtwdw_0.png, 53×71. Upgraded workshop: tbtwup_0.png, 64×71. Both stages and corresponding menu/campaign images are included. All new building artwork is static; original DEF animation sequences are not fully redrawn. Player-colour masks, scene registration and siege seams still require engine testing.

No rejected art revisions, LOD archives, extraction caches or node_modules are included. Original game imagery appears only where needed in comparison previews and retained gate artwork. Prompt files describe creation provenance, not a guarantee that image generation can reproduce identical pixels. Creative art used built-in image generation; exports used deterministic Sharp resizing/compositing.

[PACKAGE-MANIFEST.json](PACKAGE-MANIFEST.json) lists every packaged file with its size and SHA-256 checksum. [VALIDATION.json](VALIDATION.json) records export checks. Engine execution remains untested.
