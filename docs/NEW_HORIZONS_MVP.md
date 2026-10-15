# New Horizons original-content compatibility baseline

The full redesign in [NEW_HORIZONS_DESIGN.md](NEW_HORIZONS_DESIGN.md) builds on
this compatibility baseline; it is not limited to an original-rules-only MVP.
This repository is the VCMI-based New Horizons fork, not Reconstruction.

## Product and architecture

Provide a curated single-player edition with a straightforward menu, local
scenario selection and ordinary game journey. Players must not manage an optional
mod collection. Retain the internal mod/dependency loader and essential resources;
curation is not permission to delete `Mods/vcmi` or bypass validation.

Use VCMI's existing in-process authoritative simulation and internal connection.
The frontend sends commands; authoritative validation remains mandatory.
Historical references to a server describe its role, not a separate executable
or socket requirement. Preserve working AI, ordinary saves and historical ruleset
compatibility; never silently reinterpret an old save under new rules.

## Distribution and licensing

Preserve VCMI GPL notices, legitimate attribution and corresponding source.
The purchaser supplies Heroes III Complete data externally: Data, Maps and Mp3,
including relevant expansion/localization resources and audio/video.
Do not redistribute original archives, executables, installers, extracted
unchanged assets, saves or copied maps. HD Edition and native LOKI data are not
the Complete acceptance baseline.

Selected custom runtime artwork, including selected original-based edits and
composites, is delivered in NewHorizons.nhart with origin and approval records;
this changes packaging, not underlying copyright or license obligations.
Authoring masters and review materials remain local. See
[content acceptance](NH_CONTENT_ACCEPTANCE.md).

## Ordinary acceptance journey

A bounded original-content journey must cover menu and scenario setup, map
interaction and movement, pickups, town entry, building/recruitment, AI turns,
combat and spellcasting, save/quit/reload/continue, and victory or defeat.
A seven-turn AI sample is a useful initial route, not whole-product certification.
Required UI should remain readable and usable at native resolution.

A compile, source check, package checksum or startup alone is not gameplay
acceptance. Windows build/package checks and native Windows interaction are
separate evidence. Follow [delivery gates](NH_DELIVERY_PIPELINE.md); the withdrawn
0.85 preview is not a current release candidate.
