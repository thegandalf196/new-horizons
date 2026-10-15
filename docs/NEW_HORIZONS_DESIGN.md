# New Horizons implementation contract

## Authority and release boundary

The [private game-design canon](https://github.com/thegandalf196/new-horizons-docs/blob/main/canon/game-design.md) is the sole gameplay
specification; detailed system sections control over conflicting summaries.
The [amendment boundary](NEW_HORIZONS_PENDING_CHANGES.md) retains public capture and
compatibility contracts; selected legacy decisions and unresolved alternatives
are [private](https://github.com/thegandalf196/new-horizons-docs/blob/main/proposals/legacy-amendments.md).
Existing accepted decisions remain in effect; migration does not adopt drafts.
New proposals belong in the private library. The archival DOCX and [Overrides migration](NH_OVERRIDE_MIGRATION.md)
are comparison history, not additional precedence layers.

Version 1.0 includes primary attributes, Skills and their perks, six magic schools,
Core/Elite/Champion creatures and Commands. Governors/Castellans and Caravans are
future work; a single-expeditionary-Hero restriction is not implicitly authorized.
Doctrine is retired, apart from narrowly necessary historical-save reading.
The Sorcery roster uses Magic Arrow with Overcharge, not the superseded Magic
Missile summary. See [release scope](NH_VERSION_1_0_SCOPE.md).

Current phase is Phase 2: stabilization and integration. The Phase 1 exit decision
is in [NH_PHASE1_EXIT_AUDIT.md](NH_PHASE1_EXIT_AUDIT.md). The 0.85 experimental
preview was withdrawn and is not a Version 1.0 release candidate.

## Implementation invariants

Fun and working AI take precedence over numerical balance. Experimental numbers
must remain tunable and clearly provisional; correctness, understandable feedback,
crash prevention and save integrity are not optional balance work.

Preserve VCMI licensing and the original-content compatibility foundation in
[NEW_HORIZONS_MVP.md](NEW_HORIZONS_MVP.md). Single-player uses the existing internal
connection and authoritative simulation thread, not a new external server or
loopback transport. UI submits requests; authoritative code validates and changes
state. Ruleset activation and historical saved-rule snapshots must remain explicit.

Orders and spells compete for the shared Hero Action; Orders have no mana-like
currency. Human and AI decisions must use the same legal action/target rules.
Expanded Hero Attack/Defense ratings feed the specified command/formula systems,
not unchanged original creature damage arithmetic.

Class growth totals twenty deterministic primary points per level. Starting
attributes are five times the class vector; base attributes follow
`Attribute(L) = growth * (L + 4)`. Offense and Archery independently roll +1
Attack, Armorer +1 Defense, Spellcraft +1 Spell Power, and Wisdom +1 Knowledge
at 10/20/30% for Basic/Advanced/Expert. There is no level-ten transition.
Knowledge supplies base Normal Spell Point capacity. Skills use ordered Basic,
Advanced and Expert perk choices, not the retired post-Expert mastery proposal.

The six schools are Light, Nature, Sorcery, Havoc, Shadow and Chaos. Preferred
schools are equal rather than Major/Minor weighted: Necropolis Shadow/Sorcery,
Fortress Nature/Shadow. Core/Elite/Champion categorization must preserve physical
armies, recruitment and capacity; capacity never silently deletes creatures.

## Content and visual acceptance

Registration, source-backed coverage and consumer tests are documented in
[NewHorizonsContent](developers/NewHorizonsContent.md). A catalogue entry, tooltip,
active flag, image or focused test is not complete gameplay acceptance.
[Integration gaps](NH_INTEGRATION_GAPS.md) and
[regression guards](NH_REGRESSION_GUARDS.md) separate unresolved interactions
from protected invariants. [Delivery pipeline](NH_DELIVERY_PIPELINE.md) defines
candidate and distribution gates.

Follow [Heroes III UI style](NH_HOMM3_UI_STYLE_GUIDE.md) from the outer frame
inward. Live labels, numbers and controls remain code-owned. Concept art conveys
layout intent, not new rules or rights clearance. Actual native-resolution
rendering is required for visual acceptance.

Selected runtime visuals are delivered through `Mods/new-horizons/NewHorizons.nhart`,
with portable selection/origin/approval records. This includes selected Cabir,
turbaned Magi, Guild composites and custom UI resources; original-based editing
does not make selected runtime art automatically private-only. Unchanged purchaser
archives remain external. Preserve local masters, alternatives and review materials;
do not publish loose runtime exports or rebuild the pack from incomplete inputs.
Provisional purpose-made drafts may be selected under the current authorization;
selection, approval and rendered acceptance are distinct. Final requires approval
evidence. See [asset specifications](NH_ART_ASSET_SPECIFICATIONS.md) and the
[asset register](NH_UI_ASSET_STATUS.md).

Convenience additions such as QS/QL and creature status/ability icons must preserve
existing keyboard actions and gameplay. Review compatible Extras components and
their individual provenance rather than treating public availability as a license.
See [Extras integration](NH_EXTRAS_INTEGRATION.md).
