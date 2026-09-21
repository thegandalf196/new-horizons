# Orders v1 validation report

Date: 2026-09-20

The eight generated masters were inspected as a contact sheet and at native
44×44 and 32×32 previews. Each has a distinct physical silhouette and remains
recognizable after reduction. All masters are square 1254×1254 RGBA PNGs with
transparent outer pixels. No lettering, readable runes, ornamental frames or
interface chrome are present in the glyphs.

Automated checks passed:

```text
PASS: eight distinct Order masters, 44/32 previews, 64px runtime states and client bindings
New Horizons Orders client surface: PASS
PASS: eight unique Order effects, Mirth Second Wind, accepted-transition routing and resource references
PASS: four preserved gauntlet states, classic runtime chrome, bounded battle-bar geometry
```

The runtime contract is eight `NH_*_button.json` animations with four RGBA
64×64 frames in engine order: normal, pressed, disabled, highlighted. Normal
and source-master hashes are unique across all eight Orders. The existing
`NH_orders_gauntlet_framed` battle-bar entry remains separate and unchanged by
this art set.

This is provisional art: it establishes clear, coherent gameplay symbols for
the current build and can be replaced later without changing command identity,
targeting, or authoritative mechanics.
