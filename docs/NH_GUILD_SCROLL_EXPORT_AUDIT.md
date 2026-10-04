# New Horizons Mage Guild Scroll Export Audit

Date: 2026-10-04

Status: **Not done — purpose-built transparent emblems/scroll-role exports are still missing.** This inventory makes no asset or binding changes and does not close UP-212 visual acceptance.

## Scope and result

This is a read-only inventory of New Horizons `graphics.iconScroll` bindings in
`Mods/new-horizons/Content/config/spells/newHorizons.json` and
`Mods/new-horizons/Content/config/spells/massVariants.json`. It does not cover
or authorize changes to `iconBook`, `iconEffect`, `iconImmune`,
`iconScenarioBonus`, spell behavior, or acquisition.

There are 27 NH `iconScroll` references and 26 unique PNGs. Every currently
bound file is a square, opaque RGB image: 19 unique files are 32×32 and 7 are
44×44. None has an alpha channel or the native 83×61 scroll-composite size.
Representative native-size inspection of Sanctuary and Re-animate shows a
complete dark-background painting rather than an isolated transparent emblem.
Placing one over parchment would cover the paper; adding a black rectangle or
pretending RGB pixels are transparent would not remove its painted background.

The runtime files in `Mods/new-horizons/Images/` are byte-identical to the
spell-specific source exports listed below. The art-source README describes
these masters as generated original artwork, not extracted or traced Heroes
III assets. Per-spell directories retain masters and 30×30, 32×32, and 44×44
square derivatives. These are alternative sizes of the same opaque painting,
not transparent cutouts or complete scrolls. The smallest existing
icon-sized export is 32×32; the 30×30 sibling is the effect-sized variant.
The largest listed output is 44×44. None can be stretched into a faithful
83×61 composite without changing its role and appearance.

## Current bindings and matching source exports

All rows refer to the `iconScroll` field. Source paths are repository-relative
and identify the existing byte-matching output, not a proposed replacement.

| Spell | Current runtime file | Size / alpha | Matching source export |
|---|---|---|---|
| Poison | `NH_nature_poison_32.png` | 32×32, RGB opaque | `assets/new-horizons/art-source/nature-poison-v1/nature-poison-32.png` |
| Holy Wrath | `NH_holyWrath_44.png` | 44×44, RGB opaque | `assets/new-horizons/art-source/holy-wrath-v1/holy-wrath-44.png` |
| Sanctuary | `NH_spell_sanctuary_32.png` | 32×32, RGB opaque | `assets/new-horizons/art-source/sanctuary-v1/runtime/NH_spell_sanctuary_32.png` |
| Guardian Spirit | `NH_spell_guardian_spirit_32.png` | 32×32, RGB opaque | `assets/new-horizons/art-source/guardian-spirit-v1/guardian-spirit-32.png` |
| Heavenly Gale | `NH_spell_heavenly_gale_32.png` | 32×32, RGB opaque | `assets/new-horizons/art-source/heavenly-gale-v1/heavenly-gale-32.png` |
| Divine Retribution | `NH_spell_divine_retribution_32.png` | 32×32, RGB opaque | `assets/new-horizons/art-source/divine-retribution-v1/exports/divine-retribution/divine-retribution-32.png` |
| Purify | `NH_spell_purify_32.png` | 32×32, RGB opaque | `assets/new-horizons/art-source/purify-v1/exports-final/NH_spell_purify-32.png` |
| Blink | `NH_spell_blink_32.png` | 32×32, RGB opaque | `assets/new-horizons/art-source/blink-v1/blink-32.png` |
| Hydra's Vitality | `NH_spell_hydras_vitality_32.png` | 32×32, RGB opaque | `assets/new-horizons/art-source/hydras-vitality-v1/hydras-vitality-32.png` |
| Verdant Prison | `NH_spell_verdant_prison_32.png` | 32×32, RGB opaque | `assets/new-horizons/art-source/verdant-prison-v1/verdant-prison-32.png` |
| Summon Trolls | `NH_spell_summon_trolls_32.png` | 32×32, RGB opaque | `assets/new-horizons/art-source/summon-trolls-v1/summon-trolls-32.png` |
| Vengeful Vines | `NH_spell_vengeful_vines_32.png` | 32×32, RGB opaque | `assets/new-horizons/art-source/vengeful-vines-v1/vengeful-vines-32.png` |
| Entangle | `NH_spell_entangle_32.png` | 32×32, RGB opaque | `assets/new-horizons/art-source/entangle-v1/entangle-32.png` |
| Holy Armor | `NH_spell_holy_armor_44.png` | 44×44, RGB opaque | `assets/new-horizons/art-source/holy-armor-v1/exports/holy-armor-44.png` |
| Life Drain | `NH_spell_life_drain_32.png` | 32×32, RGB opaque | `assets/new-horizons/art-source/life-drain-v1/life-drain-32.png` |
| Storm of Daggers | `NH_stormOfDaggers_44.png` | 44×44, RGB opaque | `assets/new-horizons/art-source/storm-of-daggers-v1/icon-exports/storm-of-daggers-44.png` |
| Regeneration (also Mass Regeneration) | `NH_regeneration_44.png` | 44×44, RGB opaque | `assets/new-horizons/art-source/regeneration-v1/selected/NH_regeneration-44.png` |
| Hex of Pain | `NH_hex_of_pain_32.png` | 32×32, RGB opaque | `assets/new-horizons/art-source/hex-of-pain-v1/exports/hex-of-pain/hex-of-pain-32.png` |
| Plague | `NH_plague_32.png` | 32×32, RGB opaque | `assets/new-horizons/art-source/plague-v1/plague-32.png` |
| Soul Chain | `NH_spell_soul_chain_44.png` | 44×44, RGB opaque | `assets/new-horizons/art-source/soul-chain-v1/exports/soul-chain/soul-chain-44.png` |
| Shadow Gift | `NH_spell_shadow_gift_44.png` | 44×44, RGB opaque | `assets/new-horizons/art-source/shadow-gift-v2/shadow-gift-44.png` |
| Vampirism | `NH_spell_vampirism_32.png` | 32×32, RGB opaque | `assets/new-horizons/art-source/vampirism-v1/vampirism-32.png` |
| Re-animate | `NH_spell_reanimate_32.png` | 32×32, RGB opaque | `assets/new-horizons/art-source/reanimate-v1/runtime/NH_spell_reanimate_32.png` |
| Soul Reaper | `NH_spell_soul_reaper_32.png` | 32×32, RGB opaque | `assets/new-horizons/art-source/soul-reaper-v1/exports/soul-reaper-32.png` |
| Doom | `NH_spell_doom_32.png` | 32×32, RGB opaque | `assets/new-horizons/art-source/doom-v1/doom-32.png` |
| Frailty | `NH_frailty_44.png` | 44×44, RGB opaque | `assets/new-horizons/art-source/frailty-v1/exports/frailty/frailty-44.png` |

Shadow Gift is the one source-version distinction worth preserving: the bound
44×44 file matches `shadow-gift-v2`, not `shadow-gift-v1`.

## Role sharing and alternatives

The existing PNG files are shared with other spell graphics roles. The 44×44
files above are also bound as `iconBook`; most 32×32 files are also bound as
`iconScenarioBonus`. `iconEffect` generally uses a separate 30×30 sibling.
Therefore, do not overwrite these shared files to solve the scroll role. Any
future dedicated scroll-role export should be introduced and bound through
`iconScroll` only, leaving the other graphics fields and spell behavior alone.

No identity-matched transparent emblem or 83×61 full scroll composite was
found in the checked spell-specific art-source folders. Storm of Daggers does
have alpha-enabled cast/impact animation frames, but these are 224×112 VFX
frames, not a spell emblem or scroll composite. They are not a suitable
substitute.

The blank open parchment identified for the runtime wrapper is the native
`TPMAGES.DEF` group 0, frame 0 resource. It may be referenced by the runtime;
this audit contains no purchaser pixels or redistributed frame bytes. The
wrapper/template reference is separate from the missing purpose-built NH
emblem exports and is not evidence that the opaque paintings now read
correctly on parchment.

## Verification performed

- Enumerated NH `iconScroll` references in `newHorizons.json` and
  `massVariants.json`; confirmed 27 references / 26 unique PNGs.
- Used focused `jq` extraction to associate each bound path with its spell
  identity, `identify` for image dimensions/channels/opacity, `cmp` for
  byte-for-byte source comparisons, and `rg --files` over the matching
  spell-specific art-source folders for alternate exports.
- Confirmed every binding is opaque RGB and every listed runtime file matches
  its source export; found no transparent emblem or 83×61 scroll alternative.
- Checked representative images at native size. No build or GUI was run, and
  no artwork, binding, or runtime file was modified for this audit.
