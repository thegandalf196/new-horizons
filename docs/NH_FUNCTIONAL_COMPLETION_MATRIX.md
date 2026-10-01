# New Horizons functional completion matrix

Updated: 2026-10-01
Canonical source SHA-256: `c39881d1bb9dc6bd9a37650a596ff6939af6bdd548d56133ca37f862cd04d0ec`

This is the durable evidence register for UP-023. It tracks functional gameplay
completion separately from catalogue presence and artwork. An `active` data row,
a description, an icon, or a generic engine primitive is not by itself a
completed mechanic. The full evidence chain remains relevant for stabilization;
Phase 1 moves on after production implementation, registration, a working
principal path, the necessary interaction hook, a build, and focused
verification without an obvious crash or state-integrity defect. Unverified
cross-system interactions are deferred explicitly to Phase 2.

## Phase 1 specification-coverage snapshot

UP-075 Estate Network and Quick Study are source/native verified. Estate
Network grants exact Wood/Ore per current owned towns and active Advanced
holder at week start, including the initial week, after ordinary income/AI
adjustments. Garrison holders, ownership changes, resource caps and reload
consume the existing NewTurn/calendar lifecycle without a new counter.
Quick Study rerolls the complete initial skill/perk offer once at reached
levels5/10/etc, preserving one primary growth award and the existing saved RNG
streams/final query seed. Query re-exposure consumes no extra draws. Human and
AI use the same server-authored candidates; real Nullkiller selects the isolated
legal Estate Network offer and sees the authoritative weekly resources.
Both-target20329 passes; focused80431 passes29/29, zero skips, in10.865s;
reports `UP075-focused-repaired.log`/`.xml`. Data/inventory19/19 pass.
Perks132→134/310, planned178→176; Estates1/9→2/8 and Learning1/9→2/8.
Ranks84/93, faction ranks/perks and combat identities60/67 are unchanged.
Comparative economic perk valuation, pending-query/crash-recovery handling and
broader scripted/custom-calendar interactions remain Phase2. Purpose-made art
is Not done; no rendered acceptance or playable promotion is claimed.

UP-073 Mentor is source/native verified. The first strictly lower-level allied
hero met each absolute week receives250×the mentor's captured level, composed
with the recipient's ordinary Learning. Both field and town visitor/garrison
meetings record a replicated, saved per-mentor weekly marker before XP; field
level-up queries sit above the exchange. Real Nullkiller selects the legal perk
and the authoritative AI-owned meeting awards the shared amount. AI resource
trading now chooses the best eligible owned-town effectiveness rather than the
first town; this prerequisite does not activate Merchant Prince. Both-target
build81553 and focused native52779 pass24/24, zero skips, in7.070s; reports
`UP073-mentor-focused-repaired.log`/`.xml`. Data/inventory final checks19/19
pass (the earlier broader content/inventory checkpoint passed78/78).
Perks131→132/310, planned179→178; Learning0/10→1/9. Ranks84/93 and combat
identities60/67 remain unchanged. Phase2 retains nested town-building XP,
mixed-owner allied meetings, mid-prompt reload, comparative AI perk valuation
and proactive meeting planning. The existing Muster higher-tier fixture fails
because it omits earlier selected tiers; retain that unrelated finding in
NH_RELEASE_FAILURES.md. No launcher promotion or new artwork is claimed.

UP-069 Grand Formula and UP-070 Tax Collector are source/native verified.
Grand Formula snapshots the first accepted Level 4-or-5 hero-cast gate from
existing serialized history, multiplying only the SP-derived numerical term
by 150%. School, Spellcraft, Arcane Focus and Empower compose without scaling
the flat base. Rejected/creature casts and round rollover do not consume/reset
it; counterspelled accepted casts consume it. Actual Time Stop and detached AI
use the same factor. Tax Collector adds +50 per owned town (cap500 per active
holder) through shared daily income before handicap; authoritative payout and
Nullkiller forecasts agree. Both-target build `68608` and native `45801` pass:
38/38, zero skips, in 13.664s. Reports `UP069-UP070-focused-repaired.log`/`.xml`.
Content/inventory checks pass 78/78; independent review has no remaining blocker.
Perks advance 129→131/310, planned181→179; Spellcraft3/7→4/6, Estates0/10→1/9.
Ranks remain84/93 and combat identities60/67. Phase 2 retains hero ownership,
handicap/reload combinations and battle-specific tooltip modifier breakdowns.
Purpose-made artwork remains Not done; no playable promotion is claimed.

UP-066 expected-outcome AI dependency is source/native verified. Runtime and AI
share the complete typed uniform form pool and nearest-legal landing positions;
AI averages signed detached offensive profile changes, not a single RNGStub draw.
Accepted-action validation remains active. Both-target build `52390` passes;
native `31943` passes 69/69, zero skips, including mixed favorable/harmful forms,
independent mean, midpoint distinction, live state/RNG preservation and a real
selected/accepted cast. Reports `UP066-UP068-focused-repaired.log`/`.xml`.
No new spell identity: Polymorph remains inactive pending Phantom composition
and exceptional expiry design/lifecycle work. Current-board two-round offensive
forecasts, hazard costs and effective-ownership interactions remain Phase 2.

Mana Conservation is source/native verified: the accepted-cost ledger records
gross hero spell and successful Counterspell ward payments, excluding rejected
actions, creature casts and hostile drains. Post-result recovery restores
floor(20% of expenditure), capped at 20, to Normal Spell Points only, preserving
Buffer and current capacity. Build `66640` passes both targets; native `65013`
passes all ten new perk tests, including packet/state round-trip and lossy-old
protocol rejection. Overall run is 68/69: the separate random-form AI fixture
remains under repair, not certified. Active perks advance 128→129/310, planned
182→181, Wisdom 7/3→8/2. Retreat/surrender/draw reward paths remain source-reviewed
Phase 2 cases. No playable promotion or artwork acceptance is claimed.

UP-066 clone/presentation slice is source/native verified as a partial dependency.
Ordinary clones are admitted without losing one-hit destruction; source form is
restored before death clears their HP ledger. Time Stop pauses form duration.
Unit-update/round events queue effective-creature sprite refresh, and the existing
stack panel reads current/original species, exact surviving creature HP and form
rounds using a native source portrait. Independent frozen-source review reports
no blocker. Both-target build `57163` passes and isolated native retry `39543`
passes 51/51, zero skips, including unchanged ordinary CloneApply regressions.
Reports `UP066-clone-presentation-focused-retry.log`/`.xml`; binary SHA-256
`ebd913de4bb8a9f4ff5d7f726876b02910ecf97df6e55180450cbf65869dd164`.
Ordinary detached units now preserve their direct bonus-delegation path without
an unnecessary creature-type query. Sprite refresh is source/compile evidence,
not rendered acceptance. Full Polymorph activation still needs Phantom-profile support,
expiry/Dispel geometry, expected-random-outcome AI and accepted hero casting;
no spell/perk/rank count or playable-delivery increase is claimed. The per-school
table below now correctly includes the already-verified Shield of Chaos (6/11).

UP-066 cast/result follow-up is source/native verified, not full Polymorph
activation. The native `core:battleForm` effect draws from the complete captured
category and publishes same-ID state with strict nearest-legal relocation and
exact creature HP. Safe reversion reports no legal footprint without mutation.
Detached original-form result views preserve campaign counts, casualty and
Necromancy species, and gated reserve survivors at early battle end. Both-target
build `70446` and fixture-repair rebuild `32737` pass; native retry `13545`
passes 28/28, zero skips, with independent source/repair review and module/diff
checks. Reports `UP066-cast-result-focused-retry.log`/`.xml`; binary SHA-256
`0bff72dc620bb2d687e2bd5badd1c6ef4ff15f437eab1d2342b3c1f28a32d2c7`.
The real packet-path cast fixture uses mocked mechanics; accepted hero-cast
resources, temporary profiles, expiry/Dispel/Time Stop lifecycle, expected random
AI outcomes and client presentation remain Phase 1 activation dependencies.
No spell/perk/rank count or playable-delivery change is claimed.

UP-066 shared form/HP foundation is source/native verified, not a completed
Polymorph identity. Original army species/count stay immutable; shared state
replaces effective creature abilities through evaluated native sources, keeps
exact creature HP separate from temporary HP and active source-species
casualty/remains/resurrection provenance, and restores that provenance on expiry.
Acquired-CStack JSON state and nested detached AI retain the form and rank
context. Nearest magical placement uses strict occupancy/obstacle/gate-reservation
legality with stable distance ties. Binary battle snapshots cannot discard the
state silently and fail closed. Both targets build (`85599` final), native retry
passes 21/21 with zero skips, module/diff checks pass and independent review has
no remaining blocker. Binary SHA-256:
`86640d13acb1d17f121c8fe91ccab088ef0c75b70e6b50c72dc591d0b763aba8`.
Full Polymorph cast/result/expiry-footprint/UI/AI selection, clone/Phantom
admission and active capacity/Time Stop/conditional-bonus interactions remain
unfinished. No spell/perk/rank counts or playable delivery advance here.

UP-067 Arcane Focus is source/native verified: the first accepted hero spell
captures +20% to its Spell Power-derived numerical component before completion
publication. Fixed bases do not change; creature/rejected casts do not consume
the shared history and round advancement does not renew it. Damage, timed and
direct numerical helpers share the snapshot. Land Mine count now uses one
coefficient-aware calculation in runtime, Lua, client placement and AI; pre-v3
profiles preserve raw thresholds. Both Linux targets build (`55273` final
retry), and native `49925` passes 27/27 with zero skips. Reports:
`UP067-arcane-focus-focused-retry.log`/`.xml`; binary SHA-256
`77775ed6fea734223b0587f438b820ece223438d2a66785bb5d1c82c96682327`.
Content/perk checks pass 76/76; placement, module and diff checks pass; independent
review has no remaining blocker. Active perks advance 127→128/310 (182 planned),
Spellcraft 2→3/10; ranks remain 84/93 and combat identities 60/67. Broader perk
interactions, full battle save/load and graphical preview/playable acceptance
are Phase 2/delivery gates, not established by these focused tests. Polymorph's
approved nearest-legal-position relocation is canonical; its source/native
foundation is recorded above, while the full spell remains unfinished.
No launcher promotion occurred.

UP-065 user-playtest defect: Toxic Spines is registered as a Basic perk but its
application requires actual Bulwark reflection, which is zero at Basic rank.
The latest battle log confirms Basic rank and Defend. Existing positive AI
coverage uses Expert and does not establish Basic functionality. Registry counts
remain descriptive; Toxic Spines is not functionally complete at acquisition.
A user decision is pending between perk-provided first-hit reflection at Basic
and moving acquisition to Advanced; do not alter canonical potency silently.

UP-063 Shield of Chaos / Paradox Shield are source/native verified. The neutral
single-target spell installs four timed
bonuses: -10 Morale, -10 Luck and distinct fractional physical/magical reductions.
Paradox adds ten points after the base cap; the user retained the global physical
80% cap, while magical protection can reach 90%. The generic physical basis-point
bonus is append-only and rejects unsupported downsaves. BattleAI compares actual
detached protection with expected Luck/Morale costs for targets on both sides.
Both Linux targets build (final `41010`); native `61362` passes 13/13, zero
skips, including real friendly/enemy AI choices and authoritative submission,
two-round refresh/expiry, ordinary Dispel, fractional physical/magical damage,
caps, saved-v2 exclusion and bonus roundtrip/downsave rejection. Reports:
`UP063-shield-final-retry3-focused.log`/`.xml`; binary SHA-256
`afce95331c80f72b18d79ccf9ea3a2ce72b1d516facd57d6cd857d72af7994ca`.
The current focused data/perk gate passes 76/76 and module/diff gates pass.
Independent final review finds no blocker. Combat identities advance 59→60/67
(Chaos 5→6/11), active perks 126→127/310 (183 planned); ranks remain 84/93.
This is not rendered/playable acceptance or promotion.
Phase 2 review findings: opposing hero-spell pressure is not forecast; future
attacker exposure is bounded and may overvalue protection; recast valuation does
not value protection retained beyond the old expiry. These do not block the
principal source implementation. Bespoke spell/perk art remains Not done.

UP-062 Berserk targeting foundation is source/native verified: v3 forces
shooters into melee, chooses nearest legal targets by movement cost and draws
uniform equal-cost ties only at authoritative activation. Detached BattleAI
retains exact forced movement/no-action and values the expected next action
against ordinary freedom, including allied harm and resistance. Legacy shooting
and first-nearest melee remain isolated. Both Linux targets build (27932), and
native 9314 passes 21/21, zero skips, including obstacle-path targeting, seeded
server ties, defender movement, saved-profile casts and actual/read-only AI.
Reports `UP062-berserk-foundation-final-focused.log`/`.xml`, binary
`83356a9a474e1300cbe78c66616e4dd004ef9fcab7305d44310b813f8e1bdee3`.
All 77 offline checks and module/diff gates pass; review has no remaining blocker.
Next-activation expiry, negative-Morale consumption and Frenzied Curse remain
missing Phase 1 clauses; all-blocked fallback is not certified. Wider tied-branch
multi-activation projection and forced movement in that longer forecast loop
remain Phase 2 findings. No identity/rank/perk count changes: 59/67 combat
identities, 84/93 ranks and 126/310 active perks (184 planned). No GUI/promotion.

Summon Boat clause checkpoint (UP-056): captured New Horizons rules now summon
existing unoccupied sailing boats only. No-boat admission rejects before Mana
or daily completion; shared creation policy is used by authoritative casting
and actual Nullkiller virtual-boat paths. Legacy Expert creation is retained.
Both Linux targets build; 13 focused native tests pass, zero skips, and 77
offline checks pass. Independent review has no blocker. Adventure identities
remain 5/5 acquired, but no full-effect count increases: adjacent legal target
selection/preview remains missing Phase 1 functionality. Other Adventure
clauses, broad boat eligibility/selection scenarios and playable acceptance
remain separately open; combat identities 59/67, ranks 84/93, perks 125/310.

UP-061 probability foundation is source/native verified (2026-09-30):
Misfortune now suppresses final positive Luck while preserving negative Luck,
and supplies a shared timed favorable-creature probability multiplier.
Explicit runtime consumers are Death Blow, attack-triggered spells,
destruction/transmutation and Death Stare. Deterministic abilities, hero-owned
machine chances and harmful Fear rolls remain unchanged. Weaver is activated
and verified, reducing the multiplier ten percentage points before its 25%
floor; registry counts are 126/310 active, 184 planned, Chaos 2/8. AI projects
Luck and Death Blow expectations without live RNG. Both worker implementations
are integrated; independent review has no remaining blocker and offline gates
pass 77/77. Both targets build; final rebuild 84281 and native 42389 pass
25/25, zero skips: 18 new runtime/AI/helper cases plus seven direct guards.
Reports `UP061-misfortune-expiry-retry2-focused.log`/`.xml`, binary SHA-256
`01f92c0564da87a2d21e2a471f692f2f95c6af4ce86df7370dbddcba9589629e`.
The missing lifetime flag is repaired and legal expiry/Dispel now pass.
Native evidence is not playable acceptance. Innate creature resistance scope is
pending; broader proc-family valuation, custom specialty, fractional seeded
roll and Sylvan/Perfect Moment execution interactions are recorded for Phase 2.

Hand of Fate (UP-057): Level 3 Chaos primary damage and uniformly selected
secondary spill are implemented. Half the primary's actual HP loss is reduced
by the recipient's own defenses without reroll or repeated caster bonuses.
Both Linux targets build; 17 focused native tests pass, zero skips, including
accepted AI submission and read-only expected collateral valuation. All 77
offline checks and generated-module gates pass. Coverage advances to 59/67
combat identities, Chaos 5/11; ranks 84/93 and perks 125/310 are unchanged.
Legacy Clone projection parity, explicit Time Stop/caster-bonus interaction
tests, full save/load and rendered/playable acceptance remain deferred. Fate
Dealer remains planned; borrowed art is Not done, not provisional authored art.

Ordinary acquisition correction (UP-059): specialty-only Master Chain Lightning
stays known/castable for Solmyr but is excluded from ordinary learning, Guild
and generated scroll/random reward pools. Fresh Counterspell is inactive;
historical captured profiles retain their previous admission/casting.
Shared saved eligibility is independent of casting and defaults true when absent.
Both Linux targets build; 42 focused native cases pass, zero skips, with 76
offline checks and module/diff gates passing. Review has no remaining blocker.
Counts remain ranks 84/93, active perks 125/310, combat identities 58/67;
these repairs do not certify missing spell effects. Wider teacher/scroll/save
journeys and rendered/playable acceptance remain Phase 2/delivery work.

School data correction: fresh Implosion now uses Sorcery and Earthquake Nature
(UP-058). Both targets build; four focused native cases pass, including old
captured-Havoc world/BattleStart preservation and updated Archmage AI submission.
Offline gates pass 75/75. Identity/rank/perk counts are unchanged: this corrects
classification and acquisition policy, not their still-incomplete spell effects.
The adjacent noncanonical Counterspell/specialty-only Guild gap is repaired
with focused source/native evidence in UP-059.

Archmage checkpoint: Expert Wisdom's first accepted Level 4 or 5 combat spell
costs three less after Wisdom and Prepared Caster, minimum one. Saved-level
completion history persists across rounds and saves and is copied/updated only
in detached AI projections during evaluation. Rejected and creature casts do
not consume it. Both Linux targets build; 21 runtime/AI cases plus three direct
Time Stop/Pursuit guards pass 24/24, zero skips. The crash exposed during
validation is repaired with authoritative dead-active unit-action rejection.
Offline gates pass 74/74. Coverage advances to 125/310 active perks, 185 planned,
Wisdom 7/3; ranks remain 84/93 and combat identities 58/67. Broader lifecycle,
Counterspell/resistance/Metamagic interactions, full save-world journeys and
playable acceptance remain Phase 2/delivery work. Bespoke art is Not done.
The AI case proves discount/history submission for the current saved Level 4,
not canonical Implosion's incomplete School/formula mechanics (UP-058).

Arcane Memory checkpoint: reviewed source builds and passes nine feature cases
plus two direct guards (11/11, zero skips). Exact equipped scroll provenance,
permanent-source priority, reusable scroll retention, accepted completion and
learned-spell persistence are exercised. Neutral Adventure acquisition policy
is awaiting user clarification, so production activation remains planned and
the positive fixtures explicitly enable it. No active-perk count increment.
See UP-054 and the failure ledger for retry identities and final gate state.

Adventure-effect audit (UP-056): five identities/acquisition paths exist,
but all five retain missing canonical effect/UI clauses. Summon Boat's existing-
only rule now passes native tests, but adjacent-target choice/preview is missing;
Water Walk end-day land legality is unestablished;
Town Portal still allows selected towns and fixed Movement expenditure;
Fly protected-barrier enforcement is unestablished; Dimension Door still lacks
visible range-eight/full-Movement/protected-barrier enforcement. Shared 1.5x
Water Walk/Fly step costs already exist. These are missing Phase 1 effects,
not coverage established by the 5/5 acquisition count. Next: shared Town Portal
policy, preserving deterministic existing distance unless evidence demands more.

Arcane Reservoir checkpoint: the Expert Wisdom perk adds 25 Maximum Normal
Spell Points after Knowledge/Intelligence rounding, without filling new
capacity or changing Buffer. Existing authoritative rank-loss reconciliation
removes the excess Normal capacity immediately. Shared UI and AI readouts use
the same capacity getter; the Tower building remains a separate Buffer source.
Both Linux targets build; four new cases plus 27 capacity guards pass 31/31,
zero skips. Offline gates pass 74/74, independent review has no blocker.
Coverage becomes 124/310 active perks, 186 planned, Wisdom 6/4 active/planned;
ranks remain 84/93 and combat identities 58/67. Extreme integer saturation is
source-reviewed, not separately exercised beyond validated fixture limits.
Broader interactions, bespoke art and rendered/playable acceptance remain
deferred. Arcane Memory's true accepted-scroll completion seam is next.

Deep Knowledge checkpoint: Advanced Wisdom's Knowledge bonus chance is 30%
and Expert's 40% with the captured active perk. The shared growth view supplies
both authoritative independent rolls and hero-screen percentages; fixed class
growth, opportunity order/count and legacy growth remain unchanged. Valid
custom chances saturate at 100%. Both Linux targets build; four new cases and
nine direct growth guards pass 13/13, zero skips. Offline checks pass 74/74;
review has no remaining blocker. Coverage advances to 123/310 active perks,
187 planned, Wisdom 5/5 active/planned; ranks 84/93 and combat identities 58/67
are unchanged. New perk choices affect subsequent level-up rolls, not the
already-applied roll. Strategic AI acquisition, bespoke art and rendered/
playable acceptance remain deferred. Arcane Reservoir is next (UP-053).

Meditation checkpoint: the Advanced Wisdom perk adds floor(15% of Maximum
Normal Spell Points) at day start when at least a quarter of the previous
Movement maximum remains. It adds to ordinary recovery, caps Normal and
preserves Buffer; the initial day does not qualify. On-map packets and tavern
pool recovery share the event-driven getter, with pre-expiry Movement maximum
captured only for eligible pooled heroes. Both Linux targets build; five new
cases plus 22 capacity guards pass 27/27, zero skips. Offline checks pass 74/74
and independent review has no blocker. Coverage advances to 122/310 active
perks, 188 planned, Wisdom 4/6 active/planned. Ranks remain 84/93 and combat
identities 58/67. Strategic AI Movement reservation, broad interactions,
bespoke art and rendered/playable acceptance remain deferred. Deep Knowledge
is the next shared growth-path slice.

Prepared Caster checkpoint: the first accepted combat hero cast costs two less
after Wisdom's percentage discount, minimum one, before battlefield modifiers.
Overcharge and Empower Spell eligibility retain their separate formulas.
A generic per-side accepted-cast marker persists across rounds/save-load and
is copied/updated only in detached AI forecasts. Rejected/creature casts and
previews do not spend it; the shared callback serves ordinary spellbook costs.
Both Linux targets build. All ten new runtime/actual-AI cases pass, zero skips;
20/21 direct guards pass, with one unchanged synthetic v2 fixture carrying
Quicksand's v3-only `selectedPlacement` rejected during setup. Its fixture repair
is deferred to Phase 2, not a production cost adjustment. Offline checks pass
74/74; review has no production blocker. Coverage becomes 121/310 active perks,
189 planned, Wisdom 3/7 active/planned. Ranks and combat identities are unchanged.
Old saves intentionally initialize completion false; a dedicated Adventure
exclusion case, broader interactions, bespoke art and rendered/playable
acceptance remain deferred. Meditation's day-start/tavern timing map is ready.

Mysticism checkpoint: the missing Wisdom Basic perk is implemented in shared
daily regeneration and authoritative `SET_NORMAL`, with minimum/percentage
floor, missing-Normal cap and unchanged Buffer. Captured active selection and
rank gate the effect. Both Linux targets build; six new cases plus 30 direct
capacity/pool guards pass together, 36/36, zero skips. Actual day-start packet,
Wizard offer selection/save-load, planned snapshot, rank loss and rest precedence
are checked. Offline checks pass 74/74; independent review has no blocker.
Coverage is now 120/310 active perks, 190 planned; Wisdom is 2/8 active/planned.
Combat identities remain 58/67 and ranks 84/93. Strategic AI acquisition ranking,
broader interactions, bespoke art and rendered/playable acceptance remain open.

Combined Arms checkpoint: Focus Fire's frozen damage bonus applies at half
strength to eligible melee attacks, with melee-only admission; Flank ranged
attacks receive half only the Attack-derived term and never record sides.
Fractional values survive the shared C++/Lua path (fixed 5,375-damage endpoint).
Both Linux targets build; five focused runtime/actual-AI cases and 37 directly
affected Command/Focus Fire guards pass, zero skips. Content/perk/inventory
passes 74/74. Both AI cases isolate other Order coefficients while retaining
legal Magic Arrow; normal tactical ranking is not certified. Coverage is now
119/310 active perks, 191 planned, Command 4/6 active/planned. Combat identities
remain 58/67 and active ranks 84/93. Generic help/log feedback exists; bespoke
art, rendered/playable acceptance, broad interactions and inherited Flank
reachability/remaining-activation valuation remain Phase 2 work.

Command efficiency checkpoint: Aggressive (+20 points only Attack-derived
terms), Defensive (+20 only Defense-derived) and Veteran (+25 only
Leadership-derived) are implemented and active. Flat bases and capacity remain
unchanged; rank and Warcasting are additive. Both Linux targets build and 11/11
focused runtime/progression/actual-AI cases pass, zero skips. AI fixtures isolate
each Order's shared coefficient consumer with legal Magic Arrow competition;
all-canonical-Order tactical ranking remains Phase 2. Coverage is now 118/310
active perks, 192 planned; combat identity coverage remains 58/67. Generic perk
selection/help exists, but bespoke art and rendered/playable acceptance do not.

Blink/Blinkmaster checkpoint: both Linux targets compile; all 12 focused
rules/runtime/actual-AI cases and 24 existing immunity/Entangle guards pass,
zero skips. Shared legal landing geometry, School-scaled radius, authoritative
uniform draws, automatic two-draw Blinkmaster resolution, pre-cost rejection,
friendly versus hostile resistance/Mirror handling, registration, preview and
Provisional art are implemented. Actual AI submission preserves live position,
health, Mana and RNG during evaluation and resolves to a legal endpoint.
Coverage advances to 58/67 combat identities and 115/310 active perks. This
is source/native evidence, not rendered or playable acceptance. Full save/load,
broader status/obstacle interactions, tactical AI fidelity and native hover
legibility remain Phase 2 work.

Hydra's Vitality checkpoint: capacity-safe compact health cohorts preserve
current HP/count on cast, genuine per-survivor activation regeneration,
casualty-safe ordinary healing, exact temporary resurrection cleanup and
expiry/recast normalization. Target preview/status, registration, Provisional
art and actual AI submission with detached/authoritative activation parity
are present. Both Linux targets link; the isolated Hydra filter passes 8/8
and existing health/Regeneration/Cure guards pass 16/16, zero skips. This is
source/native evidence, not rendered or playable delivery.

The counts below describe coverage, not release readiness. `Active` is a
registry/source status unless a focused execution result is cited. Areas
without a defensible item-level denominator remain explicitly uncounted.

| Specification area | Current coverage | Principal remaining work |
|---|---:|---|
| Skills registered | 31/31 | Three Skills have no active rank effects; many registered Skills lack working perk progression. |
| Skill rank effects active | 84/93 | All three Spellcraft ranks now work and are registered active; Diplomacy, Divine Mandate, and Elemental Rebirth account for the nine planned ranks. |
| Skill perks active | 134/310 | 176 planned; Estate Network and Quick Study are the newest source/native-verified activations. Active status alone does not certify every mechanic. |
| Faction Skill ranks active | 21/27 | Six planned ranks. |
| Faction perks active | 44/90 | 46 planned perks; Backstab is the first active Shroud perk. |
| Canonical combat-spell identities registered | 60/67 | 7 missing/inactive; Shield of Chaos is the newest identity. Chaos is 6/11, Light 11/11 and Nature is 9/11 by identity, not blanket mechanic certification. Rendered/playable delivery remains separate. |
| Adventure spells with ordinary acquisition | 5/5 | All five have a validated town unlock/purchase path, saved town state, visitor learning, client purchase UI, and AI purchasing. The five-spell effect audit finds missing canonical clauses in every spell (UP-056); no blanket effect-complete claim. Rendered/playable purchase remains unverified. |
| Orders registered | 8/8 | Config and `HeroCommand::isActive` agree; action/AI/UI integration still needs an item-level audit. |
| Hero-class Leadership profiles | 18/18 | Capability data exists; transfer paths remain a user-reported correctness gap. |
| Creature base-line Leadership requirements | 64/64 | Data coverage only; individual creature mechanics remain unaudited. |
| Creature category forms | 126/126 | 50 Core, 58 Elite, 18 Champion are registered; this is not creature-ability coverage. |
| Siege output formula families | 4/4 | Ballista, Catapult, Tent and defensive tower outputs have data; universal Blacksmith access and Ballista Yard's weekly Siege effect are implemented with focused native tests. Rendered/playable acceptance remains open. |
| Recruitment perks active | 4/10 | Six planned; Muster has server and AI paths. |
| Diplomacy ranks/perks active | 0/3 ranks, 0/10 perks | Deterministic Diplomacy and its UI remain missing. |

Additional canonical breadth not yet reducible to a defensible completion
fraction: nine town/faction sections (33 grouped unique-building table rows),
nine artifact-conversion families, six specialty families, required combat/hero/
adventure UI surfaces, save-state representation, and minimum AI hooks. The
next ledger pass must enumerate these items rather than invent a denominator.

Priority for this phase is missing gameplay coverage, especially shared paths
that unlock several specified items. Focused verification is sufficient to
advance to the next item; rendered/playable and broad interaction evidence
remain separately tracked rather than silently assumed.

## Skills and perks baseline

The canonical catalogue contains 31 Skills, 93 rank effects, and 310 perks: 403
requirements in total. The current registry has 84 active rank effects and 134
active perks, leaving nine ranks and 176 perks planned. These counts were
rechecked directly from `config/newHorizonsPerks.json` on 2026-10-01; they are
registration coverage, not proof that every active mechanic has the whole
UP-023 evidence chain. The Basic Bulwark source head
`40628d29d92ab0d47282321fd411f5d079f38844` passed Windows build run
`36360403677` (artifact `10945274902`), but native tests and in-game validation
are still pending. Deep Bulwark remains planned;
`Corpse Preservation` is read but does not
change casualty eligibility.

Current-source native checkpoint: the Linux client, shared library, and test
targets link. In an isolated TEST profile with New Horizons active, v3 Bless
duration/rank/Benediction and hypothetical AI parity pass 7/7 focused tests;
this activates one Light Basic perk but not the other Light effects. Bulwark's
shared ordinary/automatic activation hook and lethal Poison serialization pass
8/8 focused native tests after independent review. At that earlier checkpoint,
seven other Bulwark perks remained planned. Neither
focused result is rendered or playable-delivery acceptance.

Mire Grip's AI forecast and hypothetical expiry also pass 3/3 focused native
cases after independent review. The projection uses the authoritative
Bulwark-sourced, battle-duration Speed penalty, retaining it through round
rollover and spell/Order continuations and clearing it only at the attacker's
next real activation. This removed one AI parity blocker before the later data
activation.

Toxic Spines' detached BattleAI forecast now converts the positive residual
physical-Poison damage delta through the same AI-value helper as immediate
reflection rather than adding raw HP damage to the score. The focused
`ToxicSpinesProjectsActualReflectionPoisonAndActivationTicks` native case passes
1/1 in an isolated New Horizons TEST profile after a valid owner-view fixture
correction; it also checks the converted score, poison state/tick, and live
battle immutability. This was one forecast seam, not complete Toxic Spines
activation or playable evidence at that checkpoint.

2026-09-28 six-perk activation checkpoint: Toxic Spines, Swamp Renewal, Mire
Grip, Shared Cover, Immovable, and Vengeful Mire now have active production
rows; Deep Bulwark remains planned because no nonmagical forced-displacement
producer exists. The isolated New Horizons profile passes 11/11 focused
authoritative cases and 8/8 focused BattleAI cases using production perk data;
the AI fixture no longer overrides statuses. Independent source review found no
blocking activation issue. After repairing seven invalid fixtures, the broader
active-profile Bulwark regression passes 53/53. A subsequent client slice
added persistent physical-Poison status with remaining activations and the
authoritative next tick; the Linux client and focused native UI test pass,
and independent source review found no blocker. Its native-resolution layout
and playable behavior remain unverified, and no target-package acceptance is
claimed.

2026-09-28 magic/Cure checkpoint: saved v1/v2/v3 battle-start round-trips
exercise all 23 inherited core creature-spell Expert target shapes (45/45
focused profile tests). Focus Magic now scales only its Arcane Breach Spell
Power term by the saved Sorcery rank and reports the current ordinary value in
help; 12/12 focused casts/help tests pass and independent review found no
source blocker. Cure's selected physical-Poison path and actual survivor-wound
predicate pass 24/24 focused native cases, including no-op and Spell Lock
rejections; independent review found no source blocker. Focus Magic's
rank-sensitive detached BattleAI projection passes 8/8 focused
cases after independent source review, and the post-Cure combined spell/Cure
selection passes 74/74 with zero skips. Rendered/playable checks remain open.
These slices do not close the remaining combat-spell identities or any
full Skill's completion chain.

2026-09-28 Time Stop rank slice: Sorcery School rank and eligible Warcasting
scale its Spell Power radius threshold without changing its fixed radius,
Chronomancer cap, or stasis lifetime. Authoritative cast, preview hexes, and
AI affected-stack valuation share the Lua radius path; v2 saved rules stay at
100%, and v1 roster access is not widened. The Linux native test target links,
11/11 changed-behavior cases and 15/15 Time Stop-named cases pass in an isolated
New Horizons TEST profile. Independent source review found no blocker. No
rendered/playable or target-package verification is claimed.

2026-09-28 Holy Wrath Phase 1 checkpoint: the missing Level-3 Light identity
is registered at 11 Mana with one-enemy targeting and `40 + 2 × SP` base
damage. Saved-v3 Light School rank strengthens only the SP term; Undead or
Inferno-origin targets receive one 1.5× final bonus before the ordinary
per-source damage cap. Saved v1/v2 roster boundaries remain. Purpose-made
provisional book/effect art is bound; the scenario-bonus frame is still a
placeholder. The Linux native test target builds, the focused authoritative
suite passes 10/10, and the actual BattleEvaluator choice/forecast/cast case
passes 1/1 in an isolated active New Horizons profile. Root confirmation ran
the 11 cases together with zero skips and exit 0. The content suite passes
34/34 and module-mirror check passes. Independent review confirmed the
damage-cap fix. Ordinary guild acquisition, save roundtrip, rendered icon
presentation and playable delivery remain unverified Phase 2/delivery work;
this does not close the broader spell or Skill coverage gaps.

2026-09-28 Nature Poison source checkpoint: the distinct
`new-horizons:poison` Level-2 hero spell is registered in the saved-v3 Nature
roster at 7 Mana. `core:poison` remains the older creature ability and the
physical-affliction marker recognized by Cure; reclassifying it as a hero spell
would be invalid. The cast path uses the existing serialized physical-Poison
state, School-rank-scaled Spell Power term, three escalating activation ticks,
and equal/stronger refresh rules. Provisional purpose-made art is bound.
Both Linux `vcmitest` and `vcmiclient` link, and the offline content suite
passes 47/47. A fresh isolated
TEST preset activating New Horizons passed eight authoritative/AI Poison cases
with zero skips; the saved-v2 roster exclusion and adjacent Magic Arrow AI
regression each pass 1/1. Earlier runs that skipped every case under stale
presets are not counted. Hero-source kill attribution, broader save/dispel
interactions, and rendered/playable acceptance remain separate.

| Skill | Active/planned ranks | Active/planned perks | Immediate state |
|---|---:|---:|---|
| Offense | 3/0 | 10/0 | Evidence audit required |
| Armorer | 3/0 | 4/6 | Six perks missing |
| Archery | 3/0 | 10/0 | All ten perks are active; focused evidence pending |
| Battlecraft | 3/0 | 1/9 | Nine perks missing |
| War Machines | 3/0 | 0/10 | Progression blocked |
| Discipline | 3/0 | 1/9 | Nine perks missing |
| Recruitment | 3/0 | 4/6 | Six perks missing |
| Command | 3/0 | 4/6 | Aggressive/Defensive, Veteran and Combined Arms have focused runtime/AI evidence; ordinary Advanced/Expert progression opens. Six perks remain planned. |
| Light Magic | 3/0 | 7/3 | Benediction, Healer, Guardian, Aegis, Purifier, Retributionist, and Crusader active; Sanctuary Keeper, Litany and Miracle Worker remain planned. Crusader has focused native evidence. |
| Shadow Magic | 3/0 | 6/4 | Malediction, Withering Touch, Soul Binder, Dark Gift, Night Feeder, and Reanimator are active. Reanimator's casualty-only pool has focused authoritative evidence; four perks remain planned. |
| Nature Magic | 3/0 | 4/6 | Herbalist, Rootcaller, Beastcaller and Verdant Warden active; six perks missing. |
| Havoc Magic | 3/0 | 3/7 | Seven perks missing |
| Sorcery Magic | 3/0 | 10/0 | Evidence audit required |
| Chaos Magic | 3/0 | 3/7 | Blinkmaster, Weaver and Paradox Shield are active with focused evidence recorded above; seven perks remain planned. |
| Spellcraft | 3/0 | 4/6 | Grand Formula scales the first accepted Level 4-or-5 hero spell's SP term by 150%; Arcane Focus, Spell Penetration and Empower Spell remain active. Empower Spell uses the final Wisdom-adjusted 12-Mana threshold. Basic/Advanced/Expert efficiency is 110/120/130% under saved v3 rules. |
| Wisdom | 3/0 | 8/2 | Mana Conservation post-combat Normal recovery, Archmage first accepted Level 4/5 discount, Arcane Reservoir flat capacity, Deep Knowledge shared bonus-growth chance, Meditation completed-day Movement recovery, Prepared Caster's first accepted combat-cast discount, Mysticism daily Normal recovery and Intelligence capacity are implemented. Arcane Memory source passes focused scroll/provenance/completion cases but remains planned pending neutral Adventure acquisition policy. One other perk remains missing. Broader interactions and playable acceptance remain open. |
| Warcasting | 3/0 | 4/6 | Six perks missing |
| Logistics | 3/0 | 3/7 | Seven perks missing |
| Diplomacy | 0/3 | 0/10 | Ranks and progression missing |
| Estates | 3/0 | 2/8 | Tax Collector and Estate Network supply working Basic/Advanced perks and open ordinary Expert-rank progression. Daily income, weekly Wood/Ore and AI receipt/selection are native verified. |
| Learning | 3/0 | 2/8 | Mentor and Quick Study supply working Basic/Advanced perks and open ordinary Expert-rank progression. Meetings, weekly persistence, initial offer reroll and query/RNG parity are native verified. Academic Study awaits first-visit timing. |
| Luck | 3/0 | 0/10 | Progression blocked |
| Divine Mandate | 0/3 | 0/10 | Ranks and progression missing |
| Sylvan Luck | 3/0 | 10/0 | Evidence audit required |
| Metamagic | 3/0 | 10/0 | Evidence audit required |
| Shroud of Malassa | 3/0 | 1/9 | Basic Backstab is active; the other nine perks remain planned. |
| Demonic Gating | 3/0 | 10/0 | Evidence audit required |
| Necromancy | 3/0 | 3/7 | Seven perks missing; one inert hook |
| Bloodrage | 3/0 | 1/9 | Nine perks missing |
| Bulwark of the Mire | 3/0 | 9/1 | Nine perks are active in committed source; 53/53 native regressions pass; persistent Poison status builds and passes focused tests; Deep Bulwark and rendered/playable evidence remain open |
| Elemental Rebirth | 0/3 | 0/10 | Ranks and progression missing |

Strict progression requires a perk at the preceding rank before the next Skill
rank. Five Skills therefore cannot normally advance beyond Basic because they
have no active Basic perk: War Machines, Diplomacy,
Luck, Divine Mandate, and Elemental Rebirth. Tax Collector opens Estates and
Mentor opens Learning. Backstab now
opens the Shroud's ordinary Advanced-rank progression; Blinkmaster opens Chaos.

## Spell baseline

The detailed canonical school rosters govern when they conflict with older
summary counts. They contain 67 combat spells plus five Neutral Adventure
spells. The current saved roster has 60 of 67 combat identities with active
settings rows and registered mod/core definitions; seven are absent or inactive.
Shield of Chaos is the newest registered identity. This count describes
identity registration, not exact-effect or AI completion.

Frailty replaces core Weakness in new saved-v3 acquisition while older saved
rules retain Weakness. Its battle-long, Dispel-removable Defense reduction is
calculated from intrinsic Creature Defense and accumulates to a 60% cap;
Withering Touch adds five percentage points to each cast. Both Linux targets
link and 6/6 authoritative plus 1/1 AI projection focused tests pass. Stack
status text and purpose-made spell/perk icons are source-bound, but native
rendering, save/load continuation, actual AI spell choice, and playable
acceptance remain unverified Phase 2 work.

Plague is a Level-3 Shadow magical affliction with a saved three-round marker.
Its end-of-turn tick uses captured raw Spell Power and the saved School
coefficient, and it can spread deterministically to either side without a
biological-type filter. WAIT does not tick; extra activations do not tick more
than once in a round. All six focused authoritative tests and the focused AI
valuation/selection test pass under the active New Horizons profile; both
Linux targets link. The purpose-made icon and battle status are Provisional.
Plaguebearer's undefined normal spread limit remains an open design decision;
delayed Spell Penetration/Annihilator interactions, multi-hop AI valuation,
native rendering, and playable acceptance remain unverified/deferred.

Soul Chain is a Level-3 Shadow spell with an ordered primary and up to two
secondary enemy targets. Its saved two-round status links secondary damage to
the primary without recursive echoes; the fixed 20% base is unchanged by
School rank, while the Spell-Power term uses saved School/Spellcraft scaling.
Soul Binder adds 15 percentage points after the ordinary 40% cap. Both Linux
targets link. The active-profile runtime filter passes 5/5 with no skips,
covering target legality, status serialization/Dispel, indirect and attack
damage, and the recursion guard; the focused AI cast-choice filter passes 1/1.
The module mirror and 38/38 content checks pass. Spell/Perk icons are
purpose-made but Provisional, and the selection/status UI has source-only
review. Native rendering, playable delivery, whole-battle save continuation,
active-link attack forecasting, Spell Lock versus new echo damage, primary
Dispel semantics, and triggering-hit versus echo log order remain Phase 2 or
delivery checks rather than completed evidence.

Shadow Gift is a Level-3 Shadow spell with an explicit 10/20/30% sacrifice
choice. The server validates the choice and friendly recipient, pays real
current HP and a battle-long aggregate maximum-HP loss only after the
three-round status lands, and emits per-victim spell-typed Shadow damage on
attacks. Dark Gift discounts the HP cost without reducing the damage bonus.
The cap survives stack-state serialization and now blocks ordinary healing as
well as resurrection above the reduced maximum. A compact choice modal and
separate timed/cap-loss status cues are present in client source. Both Linux
targets link; the active-profile focused Shadow Gift filter passes 8/8 without
skips, including an authoritative cast/attack, cap/save checks, AI's 30% tier
choice and conservative Phantom-integrity pricing. The module mirror and
39/39 content checks pass. Art is purpose-made but Provisional; native
rendering and playable delivery remain pending. Phase 2 should check recast
valuation against an already-active gift, Shadow-specific mitigation in AI
forecasts, Dispel/Spell Lock interactions, whole-battle save continuation,
multi-target damage/log order, and postbattle casualty accounting.

Vampirism is the next Level-4 Shadow identity: a 15-Mana, three-round friendly
enchantment that heals surviving creatures from the enchanted stack's actual
attack or retaliation damage. The saved-v3 School coefficient scales its raw
Spell Power term; Night Feeder adds 15 percentage points after the ordinary
50% lifesteal cap. The registered timed combat trigger, heal-only packet,
focused AI forecast, stack-status readback, and purpose-made Provisional spell
and perk icons form the Phase 1 source path. The Linux `vcmitest` target links;
all 15 focused runtime/AI cases pass under an active New Horizons profile with
zero skips, and `vcmiclient` also links. Offline content and UI source checks
pass. Playable acceptance is tracked separately in UP-023.
Independent source review found no blocking defect. Phase 2 should cover
ordinary AI attack-choice valuation of healing, third-round expiry, overkill
clamping, legacy live-cast rejection, and live-status save/load continuation.
Native-resolution rendering and playable delivery remain separate.

Re-animate is the Level-4, 16-Mana Shadow temporary-restoration spell. Its
authoritative one-battle restoration uses the engine's serialized resurrected
ledger, accepts usable remains regardless of creature species, heals wounded
survivors first, and excludes Disintegrated remains. Reanimator adds 25% only
to the remaining casualty-restoration HP pool, rounded down. The saved-v3
School/Spellcraft coefficient scales the raw Spell Power term; legacy Animate
Dead stays classified but inactive in new v3 snapshots, preserving validation
and old-save semantics. The Linux `vcmitest` and `vcmiclient` targets link;
11/11 active-profile focused runtime/AI cases pass with no skips. The 58/58
content/perk-data tests, 5/5 UI source checks, module mirror and diff checks
also pass. A purpose-made Provisional icon set and a generic Temporary stack
count are bound in source. This is Phase 1 implementation evidence, not a
rendered/playable acceptance claim. Phase 2 should verify end-to-end battle
result accounting with a real army-backed stack, interactions with other
one-battle restorations, spell-blocking effects, and live save/load continuation.
The current focused cleanup check directly exercises the same
`CHealth::takeResurrected` primitive called by `BattleResultProcessor` rather
than claiming a full result-dialog path. The old Animate Dead sound/impact is
provisional effect reuse. The subsequent Soul Reaper slice is recorded below.

2026-09-29 Soul Reaper Phase 1 checkpoint: the Level-5 Shadow spell is
registered at 21 Mana with target-specific `60 + 1.4 × SP + 40% of missing
aggregate HP` damage. Saved-v3 Shadow School and Spellcraft rank scale only
the Spell Power component. A post-mitigation hit that leaves a stack at or
below 10% of effective maximum HP executes its survivors; ordinary casualties,
usable remains, and Rebirth processing are preserved. Authoritative cast,
detached preview/AI evaluation, and the execution combat-log line use the same
damage path. Old v1/v2 snapshots cannot cast the new identity, including a
synthetic v2 snapshot containing its roster row. Both Linux `vcmitest` and
`vcmiclient` targets link; all 9/9 focused server/AI tests pass in the active
profile, alongside 42/42 curated-content tests and the module-mirror check.
Purpose-made Provisional 44/32/30 spell icons are bound. Phase 2 retains
partial-Magic-Resistance AI valuation, unusual temporary-HP/status mixtures,
full save/load continuation, and 32×32 versus 58×64 scenario-icon consumer
review; native rendering and playable acceptance are unverified.

Doom is now registered as the saved-v3 Level-5 Shadow malediction at 25 Mana.
Its authoritative timed spell-source bonuses impose the capped 35% + 0.15% ×
raw Spell Power penalty on damage (including retaliation once), Initiative,
and battlefield movement for three rounds, with fixed −3 Morale and no Defense
loss. Shadow School and Spellcraft scale only the Spell Power term. Recasting
refreshes the effect; old saved v1/v2 rules cannot cast the new identity.
Purpose-made Provisional 44/32/30 icons, application/refresh combat logs,
stack status, and projected BattleAI valuation are bound. Both Linux targets
link, the current private active New Horizons profile passes 8/8 focused
server/AI cases with zero skips, the Doom UI source guard passes 3/3, and the
curated-content suite passes 43/43. This is a Phase 1 source/native checkpoint,
not a rendered or playable one. Phase 2 retains Dispel and save/load round trips,
mixed flat Initiative/movement/Fortune bonuses, live AI cast-selection and
combined resistance/exchange-score valuation. The next missing detailed-roster
identity after that checkpoint was Guardian Spirit in Light.

Guardian Spirit is now the saved-v3 Level-2 Light, 8-Mana single-friendly-stack
protection spell. It grants a separate two-round `50 + 2 × Spell Power`
pool, with the School/Spellcraft coefficient applied to the Spell Power term.
Healer raises that term by 20%, and Guardian raises the resulting pool by 25%.
The pool absorbs typed physical creature damage before ordinary HP; spell
damage bypasses it. The remaining pool and duration are saved unit state,
visible in the stack status, and the combat log reports absorption and
overflow. Purpose-made Provisional 44/32/30 icons and BattleAI casting
valuation are bound. Both Linux targets link; a private active-profile run
passes 7/7 focused Guardian/Healer/server/AI checks with zero skips. The
45-case curated-content suite and module-mirror check pass. This is a Phase 1
source/native checkpoint, not rendered or playable acceptance. Phase 2 retains
physical-attack exchange prediction, legacy scripted ability provenance,
full combat save/reload, Dispel, and broader cross-system interaction checks.
The next missing detailed-roster combat spell after Guardian Spirit was Heavenly
Gale in Light.

Heavenly Gale is now the saved-v3 Level-3 Light, 13-Mana whole-friendly-army
protection spell. Its two-round timed marker reduces physical ranged projectile
damage, including physical siege shots, by `min(80%, 50% + 0.15% × Spell Power)`.
Fractional percentages use basis points; Light rank, Spellcraft, Warcasting,
Empower, and the active Aegis perk scale only the Spell Power term. Aegis also
scales Holy Armor's corresponding term before final rounding. Melee, spell-like
shots, and spell damage are excluded in the focused native paths. A stack
status, BattleAI choice/valuation, and purpose-made Provisional 44/32/30 icons
are bound. Both Linux targets link; the isolated active-profile server/AI
filter passes 15/15 with zero skips. The 46-case curated-content suite and
module-mirror check pass. This is source/native evidence, not rendered or
playable acceptance. Phase 2 retains exhaustive magical-beam and area-shot
classification, AI valuation under combined physical-damage caps, AI mass
projection logging, Dispel and save/load round trips, and live AI submission.
The remaining missing Light identity at that checkpoint was Crusade!.

Divine Retribution is now the saved-v3 Level-4 Light, 16-Mana single-ally
reactive spell. Its two-round marker records each qualifying creature
attacker's actual post-mitigation HP damage and pays Holy damage at round end
before the protection duration decreases. The base cap is `25 + 1.25 × SP`;
School rank strengthens only the SP term and Retributionist adds 20% after
the cap. Recasting replaces the old cap and duration. The Judged state and
protection have separate combat-status descriptions; BattleAI can select and
value the spell. Both Linux targets link, 14/14 isolated active-profile
server/AI tests pass with zero skips, the 47-case content suite and module
mirror check pass, and independent review's blocking refresh finding was
repaired with a focused recast test. Purpose-made Provisional 44/32/30 art is
bound. This is source/native evidence, not rendered or playable acceptance.
Phase 2 retains full save/load and Dispel round-trips, area/secondary-attack
classification, Holy mitigation and AI valuation under mixed threats, and
unusual shield or repeated-hit packet interactions.

Purify is now the saved-v3 Level-4 Light, 15-Mana battlefield-area cleanse.
After selecting a center hex, the player chooses up to
`min(2, 1 + floor(Spell Power / 120))` temporary negative effects per friendly
stack within radius 2. One spell-source group counts as one choice; physical
Poison is separately selectable and cannot be removed by ordinary Dispel.
Purifier removes one physical affliction automatically in addition to the
ordinary choices. Positive effects and Order-sourced effects survive, while
forged, stale, over-cap and no-op selections fail before spending Mana or a
Hero Action. The client picker, BattleAI target/choice projection, serialized
action payload, script registration and purpose-made Provisional 44/32/30 art
are bound. Both Linux targets link; 10/10 isolated active-profile focused
server/helper/AI tests pass without skips, the 48-case curated-content suite,
module-mirror check, and Purify picker source guard pass. This is source/native
evidence, not rendered or playable acceptance. Phase 2 retains a hypnosis
ownership interaction: authoritative eligibility uses current stack ownership,
while the picker and BattleAI also check original side, so controlled hostile
stacks may be omitted from those consumers. Full save/load, Dispel interaction,
other future physical afflictions, rendered layout and live play remain
unverified.

Crusade! is now the saved-v3 Level-5 Light, 24-Mana whole-friendly-army
empowerment. It applies capped Attack/Defense, flat Initiative rather than
movement Speed, fractional independent Magical Damage Reduction, and a
negative-Morale floor. School rank strengthens only the Spell Power-derived
terms. The five timed bonuses refresh rather than stack. Crusader adds one
round to the fixed three-round base, and Echoed Duration adjusts a Metamagic
cast once before that extension. BattleAI projects the actual detached effects
and submits the shared no-location mass action. The existing stack-status
surface shows applied values and remaining rounds; appropriate original Prayer
icons, animation and sound are used by reference, without copying purchaser
pixels. Both Linux targets link; all 19/19 isolated active-profile Crusade
cases pass with zero skips, including actual AI submission and cast parity,
recast/expiry, rank/cap formulas, independent mitigation, Echoed Duration
and Crusader stacking, status readback, and current bonus serialization with
downsave rejection. The 49-case content suite, two perk-inventory checks,
two UI wiring checks, module mirror and diff checks pass. Independent review
has no remaining blocking finding. These are source/native results, not
rendered or playable acceptance.
Phase 2 retains full combat save/reload and Dispel interaction, generic
`SPELL_DURATION` artifact interactions, hypnosis/original-side AI classification,
AI valuation beyond immediate legal attacks, and random creature-casting-pool
inclusion. Rendered/playable acceptance and bespoke art approval remain open.
Light has 11/11 registered identities, not blanket mechanic certification.
Entangle is now in source as the saved-v3 Level-1 Nature, 4-Mana root. It uses
a parameterless timed BIND marker, preserves Initiative and nonmovement
actions, and clears its own marker on accepted displacement or teleportation.
Rootcaller extends the capped base before common Echoed Duration. Detached
AI valuation and remaining-round status are present. Both Linux targets link;
all 17 focused native cases pass with zero skips, including actual AI casting,
teleport/displacement, Rootcaller plus Echoed Duration, action legality,
refresh/expiry and legacy pre-cost rejection. Content passes 50/50, perk
inventory 2/2 and status wiring 4/4; module mirror and diff checks pass.
Independent review has no remaining blocker. The HoMM3 art workflow produced original Provisional
44/32/30 icons, retaining the master, prompt, manifest and comparison.
Phase 2 retains combined classic Bind lifecycle, full save/load/Dispel,
wider AI forecasts, rendering and playable acceptance.

Vengeful Vines now has the saved-v3 Level-1 Nature, 5-Mana winding attack.
Shared geometry enforces a full six-hex S-bend for execution, client preview
and AI candidates. Intersected enemies take `20 + 1.1 × SP` damage once per
stack and lose two movement Speed for two rounds without changing Initiative;
the existing movement-only bonus preserves even classic Initiative fallback.
School rank scales only the damage power term. Both Linux targets link, and
the isolated active-profile geometry/runtime/AI filter passes 13/13 with zero
skips. It includes actual AI submission and forecast/resolution parity,
duration/Echoed Duration, immunity/resistance and pre-cost malformed/stale
request rejection. Content passes 51/51, perk inventory 2/2, UI source guard,
module mirror and diff checks pass; independent review has no remaining
blocker. Original Provisional art retains master, exact prompt, exports and
native-size comparison. Full save/load, Dispel, combined movement statuses,
hypnosis ownership, wider AI horizons and rendered keyboard/visual acceptance
remain Phase 2 work. No playable snapshot is promoted. Next Nature identity:
Summon Trolls (now implemented below).

Summon Trolls implements the saved-v3 Level-2 Nature, 9-Mana independent
temporary stack at a chosen legal empty hex. It floors the whole modified
`100 + 2.5 × SP` pool once and wounds the final Troll to preserve exact HP;
School rank scales only SP and Beastcaller increases the whole pool by 25%.
Legal-placement overlay and HP/count preview reuse authoritative mechanics.
Prospective and hypothetical stacks inherit ordinary army/creature health
bonuses without live graph attachment; the Elixir case agrees on 54 max HP,
five Trolls and 242 aggregate HP. Both Linux targets link and the ten focused
runtime/AI cases pass with zero skips, including actual AI destination
submission, pre-cost rejection, independent recasts and spawn/state JSON
roundtrips with authoritative UPDATE replay. The two existing Phantom Army/
Transfigure Matter AI guards also pass. Content/inventory passes 54/54, and
the module mirror, UI source guard and diff checks pass. Independent review
has no remaining blocker. Full mid-combat binary save/reload, broader reward/
effect interactions, tactical placement quality and rendered/playable evidence
remain Phase 2 work. Original spell art is Provisional; Beastcaller art is
Not done. No launcher promotion. Next: Verdant Prison and Verdant Warden.

Sanctuary is now the saved-v3 Level-1 Light, 5-Mana single-friendly-stack
protection spell. Its spell-sourced, battle-duration marker excludes the stack
from deliberate enemy primary creature attacks and hostile single-target
spells, while area damage remains legal. Accepted movement, attacks, and
offensive creature spells break protection before resolution; Wait and Defend
preserve it. The existing active-spell status shows the purpose-made Provisional
44/32/30 icon and help, and BattleAI can choose a threatened passive ally
without treating collateral damage as prevented. Both Linux `vcmitest` and
`vcmiclient` targets link. In a fresh isolated active-profile run, all 7/7
authoritative and 2/2 AI Sanctuary cases pass with zero skips; the 44-case
curated-content suite and module-mirror check pass. Independent source review
found and prompted correction of the hostile-spell flag gate and found no
remaining blocker in the repaired paths. This is source/native evidence, not
rendered or playable acceptance. Phase 2 retains save/load and Dispel
round-trips, unusual area/secondary creature attacks, and AI valuation when a
protected stack acts before the projected threat. The exact expiration
interpretation after a later Wait/Defend activation awaits user clarification;
the current implementation preserves Sanctuary through those passive actions.

Quicksand is among the active Nature identities. Its selected-placement path
now has a saved-v3 opt-in marker, exact ordered caster selection, authoritative
pre-spend and pre-effect validation, a matching Lua obstacle effect, concealed
presentation, client count/undo/confirm feedback, and deliberate AI target
selection. Markerless v3 and v1/v2 snapshots keep random placement. The
focused native Quicksand filter passes 9/9, both Linux targets link, and the
two client source guards pass. This is a Phase 1 source/native checkpoint, not
graphical or playable acceptance. Trap lifecycle, hidden-obstacle collision,
network packet confidentiality, and Mire Shaper's unresolved cap interaction
remain outside that evidence; do not infer full spell-specification acceptance
from the active identity row.

| School | Canonical | Active identity coverage | Missing canonical spells |
|---|---:|---:|---|
| Light | 11 | 11 | No missing identity; Crusade! has focused runtime/native evidence. Rendered/playable and broader interaction evidence remain open. |
| Shadow | 12 | 12 | None by identity; rendered/playable and broader interaction evidence remain open |
| Sorcery | 11 | 11 | None by identity; exact-effect evidence still required for other spells |
| Chaos | 11 | 6 | Confusion; Polymorph; Puppet Master; Reality Warp; Pandemonium |
| Nature | 11 | 9 | Nature's Wrath; Elemental Convergence |
| Havoc | 11 | 11 | None by identity; exact-effect evidence still required |

All five Adventure spell effects have partial or substantial runtime support,
and adventure casting calls their Mana-cost helper. Their ordinary acquisition
path now uses a per-town Guild-tier unlock: the authoritative purchase checks
ownership, active turn, eligibility, built Guild tier, duplication, and saved
cost before charging; visiting heroes learn purchased spells, and later visitors
learn town-unlocked spells. Garrisoned heroes are not treated as visitors. The
client exposes purchase controls and Nullkiller can buy affordable unlocks.
The Linux `vcmitest` and `vcmiclient` targets linked and an isolated New Horizons
profile passed 6/6 focused server/AI cases plus 1/1 polymorphic packet
round-trip; the UI source guard passed. A rendered purchase journey and
individual effect-completeness audit remain outstanding. These five spells
are Summon Boat, Water Walk, Town Portal, Fly, and Dimension Door.

Thirty-seven legacy core spells are still admitted despite not belonging to the
detailed canonical combat rosters. The cleanup must disable their ordinary
acquisition without breaking creature abilities or saved compatibility:

`Air Elemental`, `Air Shield`, `Animate Dead`, `Anti-Magic`, `Blind`,
`Bloodlust`, `Counterstrike`, `Death Ripple`, `Destroy Undead`, `Disguise`,
`Disrupting Ray`, `Earth Elemental`, `Fire Elemental`, `Fire Shield`,
`Force Field`, `Fortune`, `Frenzy`, `Haste`, `Hypnotize`, `Magic Mirror`,
`Mirth`, `Prayer`, `Precision`, `Protection from Air`, `Protection from Earth`,
`Protection from Fire`, `Protection from Water`, `Remove Obstacle`, `Sacrifice`,
`Scuttle Boat`, `Shield`, `Slayer`, `Stone Skin`, `View Air`, `View Earth`,
`Visions`, and `Water Elemental`.

Sorrow's Shadow-school correction and exact saved-v3 Morale effect now have
7/7 authoritative and 3/3 AI projected-score focused native passes. This
improves effect/rank coverage without adding an identity to the 36/67 total.
Full AI cast selection and playable/rendered acceptance remain unverified.
Life Drain now has an active saved-v3 Shadow identity, complete ordered
enemy-then-friendly targeting, damage derived from its canonical formula,
healing from actual damage dealt without resurrection, and a combat-log heal
line. The principal server path, legacy script gate, and AI pair enumeration
pass 12/12 focused native tests with zero skips; both Linux targets link.
Its icon is provisional and the human target picker has a source guard, not
rendered/playable evidence. AI healing valuation, Magic Mirror's single-target
reflection seam, friendly resistance, Blood Drinker execution, and broader
cross-system battle behavior remain Phase 2 checks.
Malediction extends saved-v3 Curse and Sorrow from three to four rounds,
refreshes on recast, and leaves v1/v2 duration behavior untouched. The focused
active-profile server and AI filters pass 8/8 and 5/5 respectively, with no
skips. Both Linux targets link, and the 17-case perk-data suite and module
mirror pass. The purpose-made provisional icon has a verified unique binding
and four 44×44 states; the global art guard remains red on 21 unrelated
pre-existing active-icon gaps. Rendered/playable delivery and full AI cast
choice remain unverified.

Sorcery Slow now has an authored saved-v3 Initiative-only magnitude:
`min(50%, 20% + floor(scaled Spell Power / 5))`, with School rank affecting
the Spell Power term rather than the fixed base. Hero casts last two rounds
before Temporalist and existing duration extensions; explicit Mass Slow still
scales the final ordinary penalty to 60%. Older v1/v2 profiles retain their
configured magnitude and duration. The same timed-effect execution path
feeds hypothetical AI previews and authoritative casts. Both Linux targets
link, and the focused active-profile filter passed 19/19 with zero skips;
rendered/playable feedback and broader combination cases remain open.

Shroud of Malassa's Basic Backstab perk is active. A selected perk adds 15
percentage points to the existing rear-facing physical melee flanking bonus,
using the shared authoritative/forecast damage path. Basic and Expert rear
attacks therefore receive +40% and +75% respectively; front attacks and an
unselected perk retain their previous values. Existing saved perk selections
carry the effect without a new state field. The Linux test and client targets link,
the isolated active-profile Shroud filter passes 5/5 with zero skips, the
perk/UI-inventory checks pass 19/19, and the generated-module check passes.
Independent review found no Phase 1 blocker. Purpose-made four-state art is
Provisional; rendered/playable icon review, save round-trip, unusual facing,
and broader ranged/collateral interaction tests remain Phase 2/delivery work.
The legacy repository-wide `nh-new-art-audit.py` still stops at its SVG/PNG
inventory equality check; Backstab's four runtime frames were checked directly
as 44×44 RGBA, and this broad mixed-art audit is deferred for the art lane.

Roster correction status: fresh Implosion/Sorcery and Earthquake/Nature are now
implemented with focused saved-profile evidence (UP-058). Counterspell is not in the
current canonical roster; Master Chain Lightning is Solmyr's specialty and
must not become an ordinary Guild spell. Ice Bolt must not retain its legacy
Speed/Initiative reduction.

## Active slices

1. **Spell Lock:** the canonical mechanic has source and focused evidence;
   rendered/playable validation and remaining cross-system cases are separate
   Phase 2 or delivery work.
2. **Archery:** all ten perks are active in committed source. The earlier
   module-reconfigure blocker was cleared before the 2026-09-29 Linux target
   links; retain the focused perk-data evidence and do not infer rendered or
   playable acceptance from the build.
3. **Bulwark of the Mire:** nine perks are active; Deep Bulwark remains planned
   because the required nonmagical forced-displacement producer is absent.
   Existing focused native checks do not close its rendered/playable evidence.

Next slices are selected by dependency leverage: remove progression deadlocks,
reuse generic infrastructure across multiple requirements, and never activate a
catalogue entry before its mechanic and evidence exist.
