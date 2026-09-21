# Integrated Hero screen and separate combat Orders UI

## Latest user direction

The Hero-development button opening an additional panel was a useful first step,
not the desired final Hero UI. Remodel the main Hero experience to accommodate
New Horizons' attributes, growth, skills/masteries and other existing systems.
User asked to find their earlier sketches.

Combat must have TWO distinct controls: Spellbook and Orders. Restore the familiar
spellbook entry instead of replacing it with the combined spell/order chooser.
For Orders, user suggests a pointing gauntlet issuing a command, painted in the
same material-aware style as the spellbook control. The Orders panel also needs
its own coherent artwork/layout. This is explicit UI direction, not permission
to grant a second hero action or change gameplay semantics.

## Sketches found and inspected

The previously supplied `new-horizons-philosophy.pdf` is actually a DOCX archive.
Its embedded image1.png is a detailed Hero-screen sketch; image2.png compares six
visual encodings of class growth (arrows, chevrons, bars, pips, embedded markers,
radial wedges). Dispatcher extracted both read-only and inspected their pixels.
Private external references: HoMM3-art/references/hero-ui-sketches/ contains images,
document text and source SHA256 metadata. No original/reference pixels added here.

The main sketch includes portrait/name/class/level; primary stats; a secondary-
attribute column (mana, Leadership, movement, morale, luck, siege); class/type,
growth, XP and specialty details; equipment/backpack; skill ranks/masteries;
selected-skill detail and investment area; army strip and section navigation.

It is a layout/design reference, not an approved implementation blueprint. It
contains illustrative old numbers and actions (e.g. free points, Replace Skill,
Mana70 with Knowledge7), repeated navigation and sample mastery text. Do not
implement those as rules or copy the image wholesale as an inaccessible painted UI.
Keep current saved-rule families, scaled values and actual available operations.
The experimental castellan/single-Hero/caravan proposals remain experimental;
this UI request does not activate them.

## Current source evidence

- CHeroWindow.cpp: adds Hero development button and opens HeroGrowthWindow.
- HeroGrowthWindow.cpp: currently exposes saved growth, Leadership/capacity,
  siege capabilities and masteries through a separate section-based window.
- BattleWindow.cpp: replaces the existing spell action image with
  NH_hero_actions_entry and tooltip Spell / Order. bSpellf routes to
  BattleHeroActionWindow under command rules. blockUI relaxes spellbook admission
  for command-enabled heroes, since bookless heroes may issue orders.
- BattleHeroActionWindow.cpp: mixed chooser contains Spells and Orders;
  refresh/chooseCommand/chooseSpell revalidate current battle authority.

Frontend must preserve and split those admission paths deliberately. Changing
only the entry image will not restore separate spell and Orders behavior.

## Implementation and art contracts

### Hero screen

First map every current saved data family and real action to a main-screen section
or integrated tab. Make the redesigned screen the primary Hero UI, not another
button chain. Group essentials on the main overview; use purposeful tabs for dense
inventory/mastery detail rather than cramming illegible text into one canvas.
Retain portraits, equipment/backpack, army interaction, navigation and tooltips.
Show actual total/base/growth and capacity values without confusing ratings with
old creature-stat arithmetic. No fictitious API/data fields or editable controls
for unavailable rules. Design legacy/saved-family-absent states explicitly.

Before commissioning final artwork freeze logical canvas size, supported layout
bounds, hit areas, text/data regions, tab states, scroll/focus behavior and sprite
manifest. Generate original reusable panels/materials/icons with $homm3-art in the
external art-capable session; all live text/numbers/interactions remain code-owned.
Do not use a generated full screenshot with baked labels as the actual UI.

### Combat buttons and Orders panel

Spellbook button returns directly to spellbook with established icon and applicable
magic/book/mana/action/controller/phase guards. Orders has its own pointing-
gauntlet control and tooltip; bookless or mana-empty heroes retain valid Orders.
Define a distinct shortcut without stealing the spell shortcut or colliding with
existing battle controls. Both still consume the same authoritative hero budget.
Legacy battles must retain their previous spell routing and not show unusable
new-rules actions. Tactics, AI/autocombat, ownership, spent-budget, battle-close,
modal return and cancellation paths need regression tests.

Orders panel: show active Order state, duration, effective values/targets and
action availability; disabled reasons,
hover/right-help, selection/confirmation as actually required, and cancel/back.
Do not invent orders from a sketch. Accommodate only current ready capabilities,
with future data-backed expansion, including Focus Fire when its real contract is
ready. Remove reliance on a combined chooser for reaching the spellbook.

Art specification needs exact toolbar slot dimensions, all supported layouts,
normal/pressed/disabled/highlighted states and native previews against the existing
spellbook button. Freeze geometry before asking the artist to paint the gauntlet
or Orders panel. Current64px generic command icons do not prove the toolbar uses
that same canvas. No copying purchaser spellbook pixels into supposedly original
art; match visual language while preserving legitimate installed-resource usage.

## Ownership and validation

Frontend owns layout/data mapping/UI source and original art integration; Runtime
supplies only actual missing read models/action validation; Build config/CMake,
serialized builds and integration; Content independent native/model and sole
fresh guarded graphical journeys. Artist charter remains Sorcery-only unless
explicitly reassigned; external artwork commission is a separate capability.

Preserve shared dirty source and current playable previews. Finish already-focused
Extras/QS-QL/ability-icon feedback repair without mixing this larger redesign into
that candidate. Continue this redesign in a bounded parallel source/layout plan.
Return user a source-backed wireframe and asset manifest before expensive final
panel-art generation. Full art/UI acceptance needs actual user review and readable
native in-game controls on both supported platform builds; source mapping alone
is not visual or gameplay completion.
