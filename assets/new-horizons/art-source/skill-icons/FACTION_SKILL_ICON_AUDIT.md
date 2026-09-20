# Faction skill icon audit

Audit date: 2026-09-20

Scope: `divineMandate`, `sylvanLuck`, `metamagic`, `demonicGating`,
`necromancy`, `shroudOfMalassa`, `bloodrage`, `bulwarkOfTheMire`, and
`elementalRebirth`.

## Result

`/usr/bin/python3 -m unittest tools.tests.test_new_horizons_faction_skill_icons -v`

```text
Ran 4 tests in 0.016s
OK
```

The audit covers the exact nine-skill manifest, config-to-file identity, all
108 runtime PNGs (9 skills × 3 ranks × 4 slots), native dimensions and RGBA
mode, distinct medium-rank hashes, and canonical Necromancy source equality.

The companion broad art audit also passes (`234 SVG, 234 PNG, 27 animations`).
Its inventory explicitly classifies the 144 painted provisional-skill PNGs and
24 approved spell-border PNGs as non-SVG art, so those assets are checked by
their dedicated source/audit workflows rather than reported as missing vector
counterparts.

## Native slot contract

| Slot | Dimensions | Production |
| --- | ---: | --- |
| small | 32×32 | LANCZOS derivative for generated masters; exact classic frame for Necromancy |
| medium | 44×44 | LANCZOS derivative for generated masters; exact classic frame for Necromancy |
| large | 82×93 | LANCZOS derivative for generated masters; exact classic frame for Necromancy |
| scenarioBonus | 58×64 | deterministic export; canonical 44×44 contain reduction for Necromancy |

## Visual review

The 44×44 contact review shows nine distinct silhouettes and value/color
families: sacred gold/red regalia, green/amber antler luck, multicolor crystal
spell shaping, red infernal gate, classic red-flame skulls, purple masked
shadow, blood-red axe, mossed swamp shield, and a four-element rebirth egg.
At 32×32 the dominant object and the three mastery progressions remain
readable; rank pips are subdued and do not replace the subject identity.

Necromancy is the canonical classic family, not a recolored or generated Light
Magic family: its Basic/Advanced/Expert files are exact copies of classic
frames 39/40/41, with only the module-only scenario slot reduced from the
canonical medium frame.

See `faction_skill_icon_manifest.json` for per-family source classification and
`necromancy_canonical_manifest.json` for source/output hashes.
