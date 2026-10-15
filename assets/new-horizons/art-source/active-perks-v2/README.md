# Active perk paintings, provisional v2

This historical family contains three original provisional New Horizons paintings
generated with the built-in image generator on 2026-09-20. The recorded subjects
and resource identities are:

| Perk | Runtime key | Subject |
| --- | --- | --- |
| Discipline — Inspirational Leader | `NH_perk_inspirational_leader` | Gold command standard, baton, and steel helmets |
| Sylvan Luck — Wild Chance | `NH_perk_wild_chance` | Emerald summoning seed wrapped in a flowering vine |
| Sylvan Luck — Perfect Moment | `NH_perk_perfect_moment` | Gold arrow aligned to the center of a celestial target |

The original square RGB/RGBA masters remain separate from earlier art families.
Historical `exports/` paths identify the helper's exact master copy, 44×44 and
32×32 LANCZOS reductions, labeled comparison sheet and per-export manifest in
the complete external authoring archive. Native previews were reviewed for
distinct silhouettes; that historical review does not imply Final approval.

`generation.json` retains source hashes, dimensions, generator/reference provenance,
concepts, prompt filenames and the family's CC0-1.0 contribution. Exact prompts
and master images are private inputs, not files required by an ordinary checkout.
Do not substitute invented prompt text or repin original provenance to make an
incomplete archive pass. Other families' rights and current selections are unchanged.

## Mechanical authoring export

Set `NH_PYTHON` to a verified absolute Pillow-enabled interpreter, `NH_ART_ROOT`
to a complete external project-shaped workspace, and `NH_EXPORT_HELPER` to the
absolute path to the retained standalone `export_art.py` helper (no plugin
installation or activation is required). Then use:

```sh
"$NH_PYTHON" assets/new-horizons/art-source/active-perks-v2/export.py \
  --staging-root "$NH_ART_ROOT" --export-helper "$NH_EXPORT_HELPER"
```

The exporter never generates or modifies paintings. It validates required files
in existing helper exports and refuses to overwrite output states/descriptors
or its review/runtime manifest. Supply complete original inputs in a fresh
staging revision rather than treating that refusal as permission to replace them.

The four 44×44 states derive only from the normal reduction: group 0 frames 0–3
are normal, pressed, disabled and highlighted. They use brightness/color
transforms, not a changed source painting; the separate small reduction is 32×32.
The external staging mirror's `Mods/new-horizons/Images/` output is not a live
installation. Ordinary consumers use the selected pack and current resource
bindings described in the [source index](../README.md).
