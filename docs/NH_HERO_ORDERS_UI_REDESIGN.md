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

Revalidate the current battle, Hero, controller, phase and action availability at
activation, before closing a chooser, changing targeting state or reading a quick
slot's roster. Reacquire from weak context identity and value IDs, not retained
window/Hero/unit pointers. Reject changed or expired context; same-context panel
recreation alone is not a different battle. Check slot bounds before lookup and
preserve legal retargeting. Disabled-reason and target-help popups remain read-only.
Distinguish immediate commands from selection followed by explicit confirmation;
free selection/cancellation does not promise that another action remains unspent.

## Saved presentation and lifetime

Query each optional saved Hero view independently; absence must not be filled from
Hero-type templates or installed defaults. Show actual totals without inventing
base/growth data, and use shared saved-value formatters rather than UI formulas.
Capture actual signed level gains and the full-width level by value before queuing
level-up presentation; later reads can belong to another level. Preserve the
applicable legacy path and existing validated secondary-choice callbacks.

Preserve an available selected detail section on same-Hero refresh; if it disappears,
choose an available section deterministically. Section changes must synchronize
text and scrollbar position through the actual scrolling API. Rebuild/removal of
overflow controls requires widget checks, not just a pure navigation-state test.

Keep one artifact/garrison holder through tabs, resizing and layout fallback; its
destruction can return or erase a carried artifact. Preserve equipment access while
carrying an item, and distinguish picked state from an in-flight request ACK.
Inactive-window layout refresh must not change the shared drag cursor, hover state
or registered status bar. Use in-place layout fallback without replacing the holder.

An exchange kept open through Mentor level-up must refresh both live Hero titles and all eight fixed secondary-Skill slots, including newly acquired Skills and changed ranks. Clear stale slots and overflow state when the list shrinks; opening with fewer Skills must not limit later refresh capacity.

## Query-backed mandatory choices

For a mandatory choice, retain the authoritative offer and explicit offered index;
do not invent a default or optimistically change Hero state. A queued request or
successful request ACK is not query completion. Immediate submission refusal restores
explicit choice without waiting for an ACK. For a submitted request, match
Hero/query/request identity: only its matching failure restores selection, and
unrelated or late ACKs cannot unlock a newer request. Apply accepted state before
completion presentation.

Deliver matching ACK/resolution to a covered choice window as well as the topmost
one. Latch resolution while covered, then close only when safely topmost; never
blindly pop a help window or close reentrantly from activation. Defer subsequent
prompts until closure is safe. Exercise help-popup failure/success, stale ACKs,
double input and chained choices through the real query-backed UI route.

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
