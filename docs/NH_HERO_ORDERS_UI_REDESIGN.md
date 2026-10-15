# Hero presentation and separate combat Orders controls

## Design boundaries

The main Hero experience should express New Horizons attributes, growth, derived
values, Skills/perks and military identity coherently, rather than relying on an
extra button chain. Sketches are layout references, not new gameplay rules, approved
numbers or distributable UI artwork. Governors/Castellans and Caravans remain future
work; a single-Hero restriction is not implied.

Map actual saved data and available operations to overview/details or purposeful
tabs. Preserve portrait, equipment/backpack, army interaction, navigation and help.
Show total/base/growth, Leadership cost/capacity and contextual rule-family-absent
states accurately. Do not paint labels into a generated screenshot or expose
fictitious editable operations.

## Actual integration surfaces

Inspect `client/windows/CHeroWindow.cpp`,
`client/windows/HeroGrowthWindow.cpp`, `client/battle/BattleWindow.cpp` and
`client/battle/BattleHeroActionWindow.cpp` before changing routing.
These are source entry points, not a claim that the entire redesign is finished.
Separate spellbook and Orders admission deliberately; replacing an image alone
does not repair control semantics.

Spellbook and Orders have distinct controls. Orders uses a pointing-gauntlet
visual language and its own help/shortcut without stealing existing spell
controls. Bookless or mana-empty heroes retain legal Orders, but both actions
share the authoritative Hero Action budget. Legacy battles retain their routing;
tactics, controller, AI/autocombat, spent budget, cancellation, modal return and
battle-close paths require focused regression checks.

Orders show actual ready capabilities, recipient state, duration/effective values,
disabled reasons, target/confirmation behavior and help. No Orders are invented
from sketches and no extra action is granted by separating controls.

## Visual and acceptance contract

Follow [Heroes III UI style](NH_HOMM3_UI_STYLE_GUIDE.md). Freeze logical canvas,
hit areas, live text/data regions, supported layouts, tab/scroll/focus states and
resource roles before authoring a panel family. Verify consumer dimensions rather
than assuming command icons define toolbar size. Use actual art capabilities and
maintained art guides, not an external plugin prerequisite.

Selected UI art belongs in NewHorizons.nhart with origin/selection/approval records;
masters and review materials remain local. Current authorization permits provisional
purpose-made drafts, not automatic Final approval. Existing purchaser resources may
be referenced appropriately; copying them does not make supposedly original art.

Source mapping and static previews do not prove readable in-game controls.
Acceptance includes native-size ordinary navigation and actual action/cancel/return
flows against an immutable candidate. See [Orders acceptance](NH_COMMAND_ACCEPTANCE.md),
[content acceptance](NH_CONTENT_ACCEPTANCE.md) and
[integration gaps](NH_INTEGRATION_GAPS.md).
