# New Horizons Order icons, v1

These eight original provisional Order icons were generated with the built-in
image generator and reduced from square RGBA masters with the HoMM3 art export
helper. They are miniature modeled fantasy still lifes, not extracted game
assets. Each directory contains the selected high-resolution `master.png`, the
44px and 32px reductions, a labeled comparison sheet, an export manifest, and
the exact prompt used for generation.

The runtime copies under `Mods/new-horizons/Images/` are deterministic 64px
RGBA animations with the engine order `normal`, `pressed`, `disabled`,
`highlighted`. `export_runtime.py` creates those copies and their animation
descriptors. The normal icon is also retained as `<stem>_icon.png` for future
creature-window/status presentation. The eight paintings are intentionally
different so the Order cards never fall back to `NH_hero_actions_entry`.

| Order | Runtime animation | Subject |
| --- | --- | --- |
| Charge | `NH_charge_button` | Forward lance, pennant and horseshoe |
| Focus Fire | `NH_focusFire_button` | Bow, converging arrows and one bullseye |
| Riposte | `NH_riposte_button` | Buckler deflecting a blade into a counter-thrust |
| Hold the Line | `NH_holdTheLine_button` | Planted tower shield and crossed stakes |
| Brace | `NH_brace_button` | Braced spear, helm and leather strap |
| Protect | `NH_protect_button` | Guardian shield sheltering a ward marker |
| Flank | `NH_flank_button` | Circling saber, spur and oblique shield |
| Second Wind | `NH_secondWind_button` | War horn, green feather and renewed breath |

The existing golden gauntlet remains the shared battle-bar Orders entry button;
this set only replaces the generic per-Order card placeholder art.
