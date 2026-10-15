# Guild scroll-role audit: retained migration rationale

## Historical finding

The 2026-10-04 read-only audit found 27 custom scroll references to 26 unique
square opaque PNGs: nineteen 32×32 and seven 44×44. They were byte-matching
icon derivatives, not transparent emblems or complete 83×61 scroll composites.
Representative Sanctuary/Re-animate paintings would obscure parchment if layered
without an explicit composition. The inspection did not change gameplay or art.

This bounded finding explains why alpha and role validation were required.
It is not a current missing-art count: later selected spell families and Guild
composites supersede portions of that inventory. Read current bindings in
`Mods/new-horizons/Content/config/spells/newHorizons.json` and
`massVariants.json`, plus the runtime-art manifest and selected-art ledgers,
rather than treating retired loose source-export paths as live inputs.

## Protected role distinction

Trace `iconScroll` through `CSpell::registerIcons` and the consuming UI.
Square native emblems and a parchment composite have different roles.
A nominal 83×61 template does not prove the actual rendered size. Do not stretch
opaque art, add an unexplained black rectangle, or pretend RGB has alpha.
Changing scroll presentation must not alter book/effect/immunity/scenario roles
or spell rules/acquisition accidentally.

Selected runtime composites belong in NewHorizons.nhart with origin and approval
records. Unchanged original archives remain external; masters, alternatives and
private inventory stay local. Static alpha/dimension checks and container hashes
do not establish native Guild readability. See
[asset contracts](NH_ART_ASSET_SPECIFICATIONS.md) and
[content acceptance](NH_CONTENT_ACCEPTANCE.md).
