# New Horizons — release failures and regression lessons

## Purpose

### 2026-10-02 UP-148 — prospective read-only battle adapters

Build95660 fails because BattleProxy is intentionally abstract: a prospective
Order-choice view must supply the remaining IBattleInfo accessors and pure
IBattleState operations. Do not instantiate a partial adapter or mutate the
real active stack to work around preflight legality. The local adapter forwards
read-only queries and explicitly rejects mutation. Retry43682 then fails because
its by-value BattleLayout accessor requires the defining BattleLayout header,
not a forward declaration. Root adds the include. Preserve both failed logs;
retry26167 is running and native acceptance/registration remain pending.
Retry26167 subsequently passes both targets. Principal99345 passes5/5 in13.180s;
activated build75589 and production-registry native40531 pass30/30 in45.567s,
zero skips. Both fixtures now use real production registration, not local
activation overrides. The preserved failures are compile-time adapter/include
requirements, not grounds to weaken authoritative opening validation.
Independent source review records exhausted grant-ID preflight handling for
Phase2: the candidate ledger throws without mutating the authoritative state.

### 2026-10-02 UP-147 — mandatory Order AI forecast boundary

Root source review found that BattleEvaluator's ordinary pre-candidate queue
forecast still runs while Double Command requires an immediate different Order.
Its projected nextRound correctly rejects unresolved continuation, so the
forecast can throw; its imminent-victory shortcut can also decline the mandatory
choice. Do not weaken the round-integrity guard. Skip that ordinary forecast and
its early decline only in mandatory-Order mode, preserving legal candidate
ranking and receipt-backed projection. Build37865 remains frozen/running;
repair and a focused actual-AI-choice fixture follow its terminal result.
No observed native failure or activation is claimed from source inspection.
Build37865 exits1 at the new server fixture: its save/snapshot calls passed the
read-only IBattleInfo interface instead of the concrete BattleInfo fixture, and
two assertions referenced an absent act helper. Preserve UP147-build.log; use
the actual concrete fixture and validated server action entry point rather than
adding permissive production overloads. Separate workers now repair that fixture,
the one-file AI forecast guard and one focused actual evaluator submission case.
Do not execute the stale binary before the retry builds successfully.
Phase 2 replay gap, independently reviewed: HypotheticBattle does not override
IBattleState::setDoubleCommandState. Current production uses its explicit copied
Order projection; only authoritative server paths produce the optional state
packet, and hypothetical generic apply does not dispatch it. Therefore this does
not block the current principal AI path, but future generic packet replay must
apply the continuation and remove its grant rather than inherit a no-op setter.
Retry build 68259 passes both targets. Principal 86481 passes 3/6 with zero
skips: legal acquisition, no-choice exhaustion and actual AI submit/server accept
pass. Two rejected-target fixtures incorrectly use UINT32_MAX, outside the action
factory's signed target range; use a representable nonexistent ID, retaining the
server rejection and unchanged-budget assertions. The third failure exposes
load-time live validation before CStack descriptor initialization. Decode must
check references/round/Order/ledger structure; alive, ghost and effective owner
checks run only after unit initialization, and remain mandatory before writes.
Do not compare immutable unit side with current controller or remove liveness
guards. The binary descriptor is not a full midbattle restore: runtime unit state
is omitted and CGameState does not serialize ongoing battles. Preserve the failed
principal log/XML and test descriptor roundtrips without inventing resume support.
Post-freeze review identified one additional atomicity boundary: grant-ID
exhaustion is checked after the primary HERO allowance is consumed. Move that
rejection into pre-mutation validation, including the authoritative preparation
path; retain receipt checks and add an unchanged-state fixture assertion. Build
47418 remains frozen and must terminate before edits/rebuild. This is a repair
of a material partial-application risk, not a broad interaction-test detour.
Repairs are verified: builds 47418/82381/47633 pass both targets; principal
retry 30525 passes 6/6 in 99.078s and activated focused 99320 passes 47/47 in
134.777s, zero skips. Server rejection leaves ledger, continuation, Orders and
StartAction count unchanged under grant-ID exhaustion. Data/client 30/30 and
module drift pass after correcting the inventory row category from `Perk` to
the established `Active perk`. Original build/native failures remain preserved.
CI dispatch lesson: use `new-horizons-windows-notices.yml` for the cheap notice
gate. Sending `preflight_only=true` to the full build workflow also installs the
toolchain and resolves dependencies. Accidental run 37071223589 was cancelled
before compilation; intended notice run 37071300435 uses source 51340a3d4.
Do not start another full job until cancellation is terminal and notice succeeds.

### 2026-10-02 UP-146 — sibling-Order packet integrity

Pre-build review found that a full state-update packet could retain the newest
Order while silently dropping an earlier sibling. Shape/round/latest validation
alone is insufficient. Issuance belongs to StartAction and round expiry to the
round transition; consumption updates must preserve the issued command sequence
and immutable issue-time fields, and reject missing unit references before
mutating state. Retained dead/ghost/changed-controller references are valid.
Repair and atomic-rejection native fixtures are pending; no failed build or
native pass is claimed from this source-review finding.
Fixture build42111 subsequently fails because standalone SideInBattle instances
require an explicit GameCallbackHolder argument. Root gives both isolated
serialization-test instances a null callback; this test serializes IDs/state
without dereferencing a game context. Preserve the original build log and
rebuild before running native tests; production/default constructors are not
changed to accommodate a fixture.
Retry96338 builds successfully. Principal13014 runs20 cases:19pass/1fail,
zero skips,32.516s; UP146-principal.log/.xml retain the result. The client-visitor
fixture reuses an isolated wire helper's BattleID7 against live battle0, so even
its valid progress is rejected. Root binds all three contextual packets to the
live battle ID. Keep the valid-progress assertion and the production identity
check; rebuild/retry must establish the intended mutation checks, not merely
accept rejection caused by an unrelated packet field.
Rebuild62682 succeeds; principal retry75974 passes20/20, zero skips,31.867s.
Adjacent87780 passes38/46 in12.558s, zero skips. Eight unchanged Vengeance
fixtures stop before their Order execution path with "Earlier New Horizons perk
tier is still required": their prepare helper directly selects Advanced
Vengeance without a Basic Offense perk. Preserve UP146-adjacent.log/.xml;
repair those acquisition fixtures in Phase2, without relaxing production
progression. Order, Iron Discipline and persistence suites in that filter pass.
The capped-reduction fixture does not independently prove uncapped multiplicative
arithmetic; that additional numeric matrix is deferred, while production Lua's
independent multiplication is reviewed directly. No broad-suite pass is claimed.

### 2026-10-02 UP-145 — Blood Scent principal fixture reference

Both Linux targets compile. First principal61899 passes3/5, zero skips,1.740s;
`UP145-blood-scent-principal.log`/`.xml` retain both failures. Boundary/cap,
rank snapshots and versioned-save checks pass. Physical payload assertions
incorrectly scale the already attribute-raised damage by1.05. Existing Lua
adds Bloodrage to the raising factor alongside other percentages, so these are
not equivalent. Verify the corrected reference against the ordinary Bloodrage
payload with an equivalent capped counter and no low-health snapshot, restoring
state afterward. Do not change production math or discard payload parity to
force acceptance. Registration remains planned until rebuilt native evidence.
Repair rebuild84207 succeeds; principal retry91133 passes5/5 in1.713s, zero
skips. Equivalent ordinary-counter damage and explicit increase assertions pass
without altering production damage. Activated focused62923 passes36/36 in8.312s,
zero skips; data36/36/drift/both-target build pass. Original logs/XML retained.

### 2026-10-02 UP-143 — threshold fixture pre-build review

Independent review caught the new threshold fixture using EGameSettings values
without directly including GameSettings.h. Root adds that include before test
registration. Preserve the existing direct-include convention rather than
depending on transitive declarations. No failed native run is claimed from this
source-only finding; build/native results are pending.
Both-target fixture build54931 succeeds. Principal30137 runs6 cases:5 pass,
1 fails, zero skips (`UP143-threshold-principal.log`/`.xml`). The detached
casualty case reaches24 Bloodrage on both sides but the defender's Berserker
query returns0 and allowance1 instead of1/2. Preserve the assertion and failure
artifacts; investigate player-scoped proxy hero visibility before activation.
Confirmed cause: BattleProxy's player-visible fighting-hero query returns null
for the enemy side. Do not fix this by changing the test to a spectator or by
exposing hidden heroes globally. Snapshot only resolved per-side threshold
Speed/retaliation values and copy them through the battle-state interface.
Current control still selects the benefiting side. Add explicit binary fields,
bounded validation, lossy-downgrade rejection, and zero legacy defaults.
Frozen repair rebuild46666 is live; first succeeding native rerun is pending.
Rebuild46666 exits0 at635/635, linking both targets. First principal retry64160
passes7/7 in2.554s, zero skips, including the unchanged enemy-AI case and new
savecase. After activation, data36/36/module/UI source guard and both-target
build pass; focused70764 passes31/31 in6.987s with zero skips. Original failure
artifacts remain preserved. Broader enemy-hero visibility gaps are deferred,
not globally bypassed by this repair.

### 2026-10-02 UP-143/144 — focused Bloodrage fixture review

Before compilation, review catches an active-War-Drums baseline mistakenly
asserted negative under Fury, cleanup removals that themselves grant Bloodrage,
a temporary passed to the AI damage API requiring an lvalue and a planned-only
registry guard that would fail after activation. The existing public projected
Bloodrage transition seam is valid; do not misclassify it as private. Use
explicit setup boundaries, public projected state updates,
and planned/active fixture override support; do not alter working production
death counting or progression to accommodate setup. Verify projected kills
above60 actually reach80, not merely that the UI/getter advertises80. Older-cap
serialization checks must isolate unrelated newer-feature downgrade guards.
Source/runtime and client build pass; native acceptance remains pending.
Frozen fixture and test build68195 pass. Principal66256 passes4/4, zero skips
in1.559s after these pre-build repairs; no native failure was concealed. Source
review has no blocker. Activation data36/36, module drift, UI source guard and
both-target18196 pass; adjacent native gate is separate.
Activated native69374 runs41 cases:40 pass,1 fails, zero skips. The rankless
legacy compatibility fixture inherits typed Hero Actions from the NH profile,
so its ancient writer correctly rejects those unrelated budgets before testing
Bloodrage cleanliness. Isolate that fixture's legacy Commands/attributes and
Warcasting settings, preserving its original format, assertions and rankless
death event. Do not loosen the production downgrade guard. Retain
UP144-activated.log/XML. Exact-source rebuild6389 exits0 and activated retry20229
passes41/41 in29.684s, zero skips (`UP144-activated-retry.log`/`.xml`). The repair
changes only the old fixture's profile, not assertions or production save guards.

### 2026-10-02 UP-142 — canonical perk-description synchronization

First data check after lifetime integration failed because registry help shortened
the approved canonical row while the existing source-contract test requires the
full effect wording. Copy the complete approved wording into both description
fields and refresh the canonical SHA/module; do not weaken the source-equality
guard. Focused data36/36 passes after correction. Runtime acceptance is separate.
Pre-build review also finds Combined Arms Focus Fire's melee recipient branch
must exclude SPELL_LIKE_ATTACK, matching actual damage eligibility. Reuse the
existing shooter predicate and add a focused nonrecipient check; do not grant a
Morale floor from cohort presence alone when its benefit cannot apply.
Fixture review catches two incorrect assumptions before native execution:
the generic attack helper requests no movement, so cannot prove Charge's
three-hex movement consumption; ordinary shooters remain melee-capable Flank
recipients. Repair legal action geometry and expectations, not production
eligibility. Confirm activation ordering where enemy Angels are faster.
First native56838 runs9 cases:2 pass,7 fail, zero skips. Added-1 penalties do
not establish net-1 under existing Angel/army Morale bonuses. Explicitly
normalize the fixture baseline and retain assertions of suppression and expiry;
do not change ordinary Morale calculation to satisfy the test. Log/XML remain
UP142-principal; activation awaits the repaired exact-source retry.
Fixture rebuild15520 and native retry18653 pass:9/9, zero skips in21.568s.
Synthetic-100 ordinary Morale is checked against the configured lower cap;
the enemy control is living rather than NO_MORALE. Activation's first inventory
check also catches an invalid "Skill perk" category; repair it to the existing
"Active perk" schema. Data36/36, generated-module drift and both-target21780
then pass. Never weaken inventory category/neutral-art guards to pass activation.

### 2026-10-02 UP-140 — native fixture assumptions

First principal run13 has9pass/4fail; adjacent9 has4pass/5fail, zero skips.
Three principal expectations exceed the active global Morale cap despite
map-local chance overrides; native aura owner assertion also needs tracing.
No Quarter fixtures fail during outdated direct Expert-perk acquisition, before
exercising their mechanics. Preserve both logs/XML, repair bounded fixture
assumptions without weakening production validation, then rebuild/retest.
Independent tracing finds a real added-stack aura defect: base-null localInit
attaches army before creature, letting OwnerUpdater run from the neutral
creature definition. Original-army initialization has an owned context. Attach
the creature source before the army, allowing owned-stack propagation into the
battle graph. Require friendly/enemy added Bone Dragon tests; never redefine
neutral ownership as an enemy guess to make the fixture pass.
Both-target rebuild76129 exits0; principal retry14/14 passes in3.155s, zero
skips. Adjacent retry9/10 passes in3.314s: the remaining No Quarter whole-battle
roundtrip hits the pre-existing unconditional BattleInfo::hasVeteranDamageHistory
guard. CStack's binary payload omits CUnitState, so no whole-battle current-format
roundtrip is claimed. Retain that broader representation gap for Phase2; Bonus
hostility binary/JSON and detached state have their own passing principal checks.
Activation data36/36 and both-target build pass. The first inventory attempt
reversed CSV Implementation/Art cells; corrected them to Provisional/Not done,
without changing the guard against calling a neutral fallback purpose-made art.

### 2026-10-02 UP-140 — spell attribution callback contract

Both-target build9276 stops at BattleSpellMechanics.cpp:515: the recorder stored
`IBattleInfoCallback`, which does not expose current-controller `battleGetOwner`.
Actual and projected spell mechanics already supply `CBattleInfoCallback`.
Narrow the recorder's constructor/member to that existing callback rather than
guessing ownership from unitOwner or duplicating Hypnotize rules. Build retry
and principal native acceptance remain required. Preserve UP140-build.log.
Retry29668 reaches both executable links; repair rebuild76129 exits0 and
principal14/14 passes. Earlier failure remains retained, not overwritten.

### 2026-10-02 UP-139 — Havoc structural targeting review

Final both-target retry4772 exits0. Native retry35313 passes10/10 in2.669s,
zero skips; adjacent27391 passes6/6 in1.369s. Typed parser mismatch and legal
AI fixture setup are repaired without weakening casting or immunity. Final
independent review has no blocker; retain the earlier failures below as lessons.

Both-target builds20497/48092 and repaired-AI rebuild43129 exit0. Principal81936
runs10 in2.661s with zero skips: all6 runtime cases pass; typed parser1 and
AI3 fail. Integer-valued Float50.0 passed validation while the runtime helper
required DATA_INTEGER; repair the narrow structural validator to match its
reader, not the test. AI returns empty structural candidate sets in two cases
and selects an Order instead of Meteor in the paid-cast scenario. Diagnose the
actual target-type/lifecycle/scoring paths before adjusting fixture or production;
do not claim AI coverage from runtime success. Retained UP139-principal.log/xml.
Read-only diagnosis proves the first two AI cases enumerate before beginCombat:
their Hero Action allowance has not been initialized, so rejection is correct.
Initialize a real round before enumeration. The paid-choice case selects a
valid Order; use a validated test-local zero-effect Order profile to isolate
spell ranking while preserving enabled Orders and all normal action gates.
This does not establish ordinary-profile tactical preference for Meteor.

Supplemental Python content inventory runs59 cases:56 pass, one fails and two
error because its pre-existing NEW_HORIZONS_SPELLS set omits the five implemented
Mass identities. This is unrelated to the structural payload; preserve the
finding for Phase2 inventory cleanup rather than weakening roster validation
or blocking the structural principal gates. The focused36 data checks pass.
Final static review catches an AI fixture demanding an immune creature in
getAffectedStacks, which correctly excludes unreceptive recipients. Assert
geometric footprint intersection separately from the filtered creature set;
do not weaken production immunity. Re-run the test build after its repair if
the current build already consumed the file.

Pre-build review found Meteor metadata deduplicated against creature positions.
Resistance or Spell Lock could remove the creature and its structural hex;
double-wide primary positions could also extend the area beyond the blast.
The Lua producer now preserves independent pure-hex impact metadata, excludes
unit-bearing aim entries, and leaves creature targets unchanged. Focused native
verification is pending; do not infer acceptance from this static repair.
The new fixture also constructed a base obstacle tagged MOAT, whose footprint
asserts. Mirror the engine's SpellCreatedObstacle/customSize moat representation;
do not weaken production obstacle queries to accommodate an invalid fixture.

### 2026-10-02 UP-135 — Bastion AI compilation

Client build92791 exits1: AttackPossibility's new Counterfire branch gate reads
archeryCounterfireRound directly from battle::Unit, whose interface exposes
that marker through acquireState instead. Preserve the gate and inspect the
copied state; do not remove Counterfire projection to pass compilation.
Retry and focused native acceptance remain pending. New aggregate optional
marker fields also need explicit initialization to avoid added warnings.
Both-target retry79589 builds successfully. Principal64640 runs8 cases in2.607s
with zero skips:4 pass,4 new fixture cases fail. Repeated accepted-action setup
returns false, post-spell Defend eligibility disappears, and initial detached
eligibility is false. Diagnose action lifecycle and saved/local perk gates before
changing production; the fixture owner is repairing only evidenced setup errors.
The wrapper tails the log, so its exit0 is not the test exit status; the native
GoogleTest report explicitly records4 failures. Subsequent wrappers must preserve
the test process exit code as well as checking the report.
Detached eligibility failure is a fixture visibility error: PlayerColor0's
callback intentionally hides the defending hero, so it cannot inspect that
hero's perk. Use an all-knowing test view for this deterministic parity assertion;
never relax production visibility to make a fixture pass.

Repaired-fixture build72269 exits0. Retry48708 passes6/8 in2.622s, zero
skips. Two failures remain: baseline prediction after an already-spent actor
reports1 against actual3825, and a next-round attack is rejected. Keep the
production rules frozen while diagnosing the fixture; do not activate Bastion
from the passing subset. This wrapper preserves the native exit1 correctly.
The baseline fixture used the spent first actor: its activation-output factor
correctly reduced the hypothetical damage to the floor. Forecast from the
still-unspent second actor instead. Round renewal must use accepted actions
through endRound rather than only emitting BattleNextRound. Rebuild49426 is
running with these fixture-only repairs; production remains unchanged.
Rebuild49426 exits0, but independent review catches a fixture blocker before
native execution: the second attacker pair reuses occupied hexes, and the
double-wide footprints were not checked. Repair legal placement before accepting
any test result; no passing result is inferred from this build.
Legal-footprint build48454 exits0. Native46941 runs8 in2.653s:4 pass;
three cases compare floor(70% of an already-rounded baseline) against the
correct once-rounded result and differ by1HP. The spell-like gate also loses
its next melee actor to the Lich's collateral damage. Fixture-only repairs must
preserve exact protected predictions and meaningful70% bounds, alive actors,
accepted-action checks and round renewal. Bastion remains planned.
Final fixture rebuild92361 exits0. Native45892 passes8/8 in2.618s, zero skips.
The protected multiplier applies before the final floor, so the fixture bounds
its comparison to a separately rounded baseline by1HP. The spell-like test
asserts living follow-up actors and exact pre-hit prediction, avoiding collateral
casualty assumptions. Independent final review has no blocker. Production rules
were not weakened; activation and its final gates follow this passing result.
Activated both-target28218 exits0. Combined native67346 passes18/18 in5.197s,
zero skips; registration/data/inventory35/35 and generated module checks pass.
Earlier failures remain historical lessons, not current blockers.

### 2026-10-02 UP-131 — Unbreakable principal fixture

Client52523 and test10838 exit0. Principal30730 runs10 cases with zero skips:
9 pass and the positive-Morale/immunity fixture fails to observe its expected
Morale activation. Actual negative-trigger suppression, renewal, Rally
coexistence, controller ownership, wire and detached cases pass. Investigate
the positive-action fixture before changing production or weakening assertions;
Unbreakable remains planned until the repaired principal run passes.
The helper used Defend for every action, but rollGoodMorale explicitly excludes
Defending stacks. Use a validated ordinary movement for the positive stack,
then retain the actual Morale-activation and unused-Unbreakable assertions.
This repairs the fixture's action choice, not the production Morale rule.
Fixture rebuild52313 exits0; retry10314 passes10/10 in3.330s. Activated
both-target98579 and native69637 pass10/10 in3.303s, zero skips. Retain the
failed principal evidence and Defend exclusion lesson for future Morale tests.

### 2026-10-02 UP-124 and Windows36994237037 — build gates

UP-124 local build exited1 before compilation: CMake's curated Muster text
allowlist omitted the three new external-dwelling keys, although the generator
correctly included them. Add the same keys to the CMake comparison; do not
disable drift validation. Principal build/native evidence remains pending.
Windows full run36994237037 on6ca967db6 passed preflight but failed compiling
NewHorizonsDiscipline.cpp: MSVC parsed the parenthesized static BonusSourceID
declaration as a function (C2751/C2267). Use brace initialization to remove
the ambiguity without changing the bonus source identity. No Windows package
was produced. Follow-up full run37000555568 on7331e1056 now completes
successfully: Windows x64 compile, recursive dependency/source/license package
and upload all pass. The nonexpired preview artifact is751939717 bytes.
This confirms the brace-initialization correction; retain the original failure.
It does not establish Windows graphical acceptance or include later Broad Muster
and Unbreakable commits.
Local retry29673 compiled the production changes but exited1 in the new
fixture: accessing PlayerState resources requires CPlayerState.h, not only a
forward declaration. Add the explicit include and retry the same client/test
targets. Do not run the stale test binary or remove the resource assertions.
Final build44293 exits0 for both Linux targets. Principal37505 runs11 cases
in1.700s:9 pass, two fail with "Earlier New Horizons perk tier is still required".
Both fixtures select Advanced/Expert Recruitment perks without earlier perk
tiers. Repair legal prerequisite acquisition and retain production enforcement;
combined Volunteer Network/Master Recruiter town amounts must include both
effects. The free recruitment case must actually empty resources before purchase,
not merely compare unchanged positive resources. Do not activate until retry passes.
Fixture rebuild47067 exits0; retry21396 passes10/11 in2.904s, including the
complete External Recruiter principal case. The older town test retains the
first Basic Muster's four recruits when only weekly markers are reset. Its
Master phase must assert an eight-recruit delta from that existing stock,
not a total of eight. Assert the prior four and the exact delta; do not change
production arithmetic or erase stock merely to pass the test.
Rebuild8494 exits0 and principal15376 passes11/11 in2.877s. Production-enabled
build exits0 and activated81048 passes11/11 in2.899s, zero skips. A combined
Python invocation initially lacked tools/tests on PYTHONPATH and failed to
import the legacy-schema helper; the corrected invocation passes35/35.
Generated-module and UI wiring checks also pass. Windows acceptance is separate.

### 2026-10-02 UP-120 — Spellward pre-build integration corrections

Read-only mapping rejected static original-army bonus inheritance: Hypnotize
must change the protecting hero, and summons/gates must not require an original
army bonus node. Use the existing current-controller callback on damage paths,
with a separate independent reduction source before the shared cap/penetration.
No new state or polling is needed. Root pre-build review repaired the native
fixture's missing skills registry level; independent Astra review identified a
redundant final brace and an explicit IBattleState include required by the Lua
proxy. Both were repaired before compilation. Strict legacy-MDR fixture adapters
strip current-v3-only placement/Earthquake/Mass metadata rather than relaxing
old schemas. These are pre-build corrections, not played regressions.
Two reviewer service calls were rejected at the thread limit; after the runtime
worker completed, a fresh Astra reviewer succeeded and found no remaining
production blocker. Both-target build34789 is running with12 jobs. Perk remains
planned until focused actual casts pass. Wider magical-ability/save interactions
and Fire Shield's existing reduction-immunity shortcut are Phase2 findings.

Build34789 exits1 in the new fixture: BattleCast requires CBattleInfoCallback,
not its virtual IBattleInfoCallback base alias. Use the concrete callback type
in the forecast helper; do not cast around the API or change production types.
Production files compiled before this test-only failure. Retry must finish both
targets before native execution or activation.

Retry79021 exits0 for client and test targets. Focused native21882 completes
21 cases in5.775s:20 pass and one fails, zero skips. Live Spellward prediction
and paid damage agree at198, but detached AI prediction and castEval both return
220. Do not activate the perk or weaken the parity assertions until the proxy
path is corrected. Existing MDR, Plague and Soul Chain checks pass.
Read-only Luna diagnosis confirms BattleProxy::getSideHero uses the filtered
player callback, hiding the opposing hero. Its computed-defense override now
forwards the projected unit to the subject callback; it does not expose the
hero or relax visibility checks. Projected Hypnotize remains the ownership
input. Build80192 validates this repair and stronger combined/cap assertions.
Independent Astra proxy review finds no blocking issue. Phase2 retains a
detached-only Hypnotize fixture in which projected ownership differs from live
ownership; current tests cover live control changes and ordinary detached
parity separately. No validation or playable delivery is implied by review.
Subsequent activation closes that specific fixture gap: projected-only control
flips now verify both directions and unchanged live ownership. Repair build80192
exits0; principal24023 passes21/21 in5.858s, zero skips. Activated build12348
exits0 and native60120 passes22/22 in6.000s, zero skips. Data/schema/inventory
35/35 and module drift check pass. Independent Astra activation review finds
no blocker. Wider save/ability interactions remain Phase2. No GUI/promotion.

### 2026-10-02 UP-118 — weighted movement pre-build correction

Root diff review found that the first terrain implementation added the surcharge
when computing the candidate distance and again when storing that distance.
Store the already-complete candidate once; focused fixtures must assert exact
weighted distances, not merely that terrain makes movement more expensive.
Skip per-edge footprint work when no movement-cost terrain exists. This was a
pre-execution correction, not a failed build or a user-played regression.
An independent reviewer spawn was service-rejected; root performed direct
review. Earthquake remains unactivated pending its own runtime implementation.
Client54585, test92767 and focused82811 exit0; native72881 passes12/12 in3.287s,
zero skips, including exact-cost, state compatibility and detached guards.
Pre-run fixture review also corrected the attacker tail to head-1 via
occupiedHex(destination), and explicitly set a non-triggering terrain spell ID.
These corrections preserve the intended assertions rather than weakening them.

### 2026-10-02 UP-115/116 — pre-build source/fixture corrections

Independent review found that Sanctuary Keeper's narrow negative-Morale AI
utility initially credited the already-active stack, whose pre-activation roll
had already occurred. Exclude that stack from this prospective benefit; do not
invent post-action positive Morale after an attack breaks Sanctuary. Frozen
production review has no remaining blocker; compilation/native evidence pending.

Pre-build fixture inspection found fabricated SANCTIFIED markers being used to
expect Keeper's cast-time grant, and a rank-toggle assertion assuming live perk
polling. Replace these with real casts and a non-perk recast that cleans the
previous snapshot. A stored effect remains until its source lifetime ends.
Legacy-profile fixtures must strip every v3 variant row/metadata, not merely
change the version number. These are pre-execution corrections, not failed runs.

Both-target build60415 exits1: the new fixtures omitted
`NewHorizonsSpellAvailability.h`, which declares `spellAllowedBySavedRoster`;
Keeper also omitted `AI/BattleAI/StackWithBonuses.h` for `HypotheticBattle`.
The resulting target-construction errors are cascades, not production failures.
Add the explicit declaring headers to both fixtures and rebuild before native
execution. The client target already passed90317; neither perk is activated.

Retry both-target96409 exits0. Principal90439 completes24/25 in6.529s,
zero skips. All Sanctuary Keeper, shared formula, ordinary Sanctuary and real
Sanctuary/Poison AI casts pass. Only the new Toxic Spines exclusion fixture
observes zero reflected loss: it selected Basic Bulwark, whose reflection is
zero. After legally selecting Basic Toxic Spines, advance Bulwark to Advanced
as the existing reflected-damage fixture does. Preserve the positive reflection
assertion and exclusion expectation; no production change is needed.

Repaired both-target48358 exits0; principal20512 passes25/25 in6.559s,
zero skips. Activated both-target10531 exits0 and native76413 passes30/30
in7.833s, zero skips, including ordinary Poison and Toxic Spines guards.
Data/schema/inventory34/34 and generated module check pass. Both new perks
are active; no remaining blocker was found by independent review. Broad
interaction coverage remains Phase2; no GUI or playable promotion occurred.

### 2026-10-02 UP-114 Mass Slow — pre-native family refresh correction

Root inspection and independent review found that generic timed refresh updated
only duration, retaining the previous Slow magnitude. This would preserve a
full-strength penalty after Mass Slow or a weakened penalty after ordinary Slow.
Added explicit saved-v3 `STACKS_INITIATIVE` family replacement. V1/v2 refresh
is unchanged; historical v3 profiles retain their toggle and budget but also
receive the magnitude-refresh fix. Require detached and authoritative evidence in both
directions before activation. Client33547 exits0; data/schema/inventory34/34 pass.
These gates alone do not establish native or playable acceptance.
Both-target64419 exits0. Principal26295 completes17/22 in5.891s, zero skips.
Five new actual-cast assertions reject (including base Slow and the historical
toggle); detached magnitude and targeting checks pass, and all eleven existing
legacy fixture cases pass. Diagnose the new fixture's active-player/roster setup
before attributing this to the variant runtime. Activation remains gated.
The fastest defender Griffin owned the active turn; BattleActionProcessor's
owner gate correctly rejected attacker Hero Actions. Fixture now activates and
asserts its friendly owner after combat start and one-packet round advances.
No production validation was weakened, and rejection tests use the same setup.
Repaired both-target38775 exits0; native35996 completes21/22 in5.841s.
Only reverse refresh's detached assertions fail (-36/2 instead of -60/3);
the actual accepted cast restores -60/3 correctly. Check whether the fixture
retained the live unit pointer before detached cast materialization. Do not
weaken the forecast expectation or activate until its actual state is verified.
Forecast build46894 exits0; principal7002 passes22/22 in5.821s after querying
the post-cast detached units by ID. Activated build63474 exits0; native7462
passes27/30 in8.003s. Two historical AI fixtures omitted Basic Temporalist;
repair their prerequisites without weakening progression. The new AI cannot
select Mass Slow because two Initiative valuation checks still use the base ID,
and only the legacy toggle normalizes empty forecast targets. Resolve the saved
family for Slow valuation and use massive mechanics for the no-location sentinel.
Require real AI selection and accepted server cast before coverage closure.
Independent review caught a normalization interaction before the final run:
Heavenly Gale's untargeted admission checked the normalized destination vector,
which is now nonempty by protocol. It now checks the original candidate target;
include its focused AI test in the activated gate. Other destination checks
were inspected and remain valid. Preserve this finding rather than silently
claiming broad compatibility from the Mass Slow-only fixture.
Final both-target53442 exits0. Activated47843 passes36/36 in9.512s, zero skips,
including real distinct Mass Slow selection/server acceptance, historical AI,
Communion and all five Heavenly Gale AI guards. Principal7002 passes22/22.
Independent final review has no remaining blocker. Data/schema/inventory34/34
and module drift check pass. Source/native evidence only; no GUI or promotion.

### 2026-10-02 UP-114 Mass Regeneration — pre-build correction

Independent review caught an escaped apostrophe in the new AI fixture's
integer literal. Corrected to the standard C++ digit separator before building.
This is a fixture compile correction, not native acceptance of the mechanic.
Review also found that generic massive timed targeting did not inherit base
Regeneration's clone/phantom exclusions. Require the shared eligibility filter
in live, detached and affected-target paths, with explicit native exclusions.
Strengthen the forecast fixture by mutating the detached unit before casting;
copying a wounded live battle alone does not establish materialized-state parity.
Activation remains gated on the repair and focused native evidence.
Client37281 and both-target18607 exit0. Principal8366 passes21/26 in9.741s,
zero skips: all existing family/base tests pass, but five new fixture cases
omit the required Basic Nature perk before Advanced Communion. The offer
search cannot offer an illegal tier and direct selections throw the expected
earlier-tier validation error. Repair fixture progression with Basic Rootcaller
(not Herbalist, so rate assertions retain their intended formula). Do not weaken
production progression or activate the perk before the repaired native gate.
Repaired build52222 exits0. Native24363 passes the first three new cases,
then spins in the refresh fixture for over80s; root terminated that owned
process (exit143), preserving the log. No full passing result is claimed.
The fixture uses unbounded endRound with synthetic excluded siege/clone/phantom
stacks; isolate refresh from those eligibility-only actors and bound its round
advance. Keep the independent eligibility assertions intact. Debugger attach
was denied by ptrace restrictions; no OS policy was changed. Treat the spin's
precise cause as unproven until the bounded fixture supplies evidence.
Bounded build29708 exits0; native35545 completes25/26 in7.574s. The remaining
fixture assertion incorrectly interprets CUnitState::damage's reference as
unspent damage; it reports actual damage, which is2 here. Assert2 and exact
two-HP loss instead, keeping materialized pending-wound preservation checks.
The bounded round now advances correctly; the broader eligibility-roster
round-advance interaction remains a Phase2 diagnostic, not a proven repair.
Final build88628 exits0; principal79005 passes26/26 in7.453s, zero skips.
Activated57857 passes29/30 in8.580s: AI selects the right distinct spell but
submits an empty wire target, rejected by authoritative validation. The existing
NO_LOCATION sentinel conversion covers only two named mass spells. Extend it
to empty accepted candidate destinations generally; retain server validation
and assert the actual emitted wire destination in the AI regression.
Final both-target55569 exits0. Activated81701 passes30/30 in8.697s, zero
skips, including actual AI cast acceptance. Data/schema/inventory33/33 and
module drift check pass; independent review has no remaining material blocker.
The fixture spin remains a separate Phase2 interaction diagnostic, not a claim
that every round-advance interaction is repaired. No GUI/playable promotion.

### 2026-10-01 UP-114 Mass Bless — data fixture correction

The first bounded data assertion still assumed every Mass entry belonged to
Shadow/Grand Malediction. Adding Light/Litany exposed that fixture assumption;
the worker generalized the explicit expected grant mapping. Spell activation
remains dormant until focused native verification. The Mass Shadow old-v2
fixture now removes every variant row, not just its two original entries, so
new installed content cannot leak v3-only metadata into the synthetic old save.
The previously deferred Bless description assertion is reconciled to the
existing combined Spell Power coefficient wording while this family is under
focused verification; no gameplay value is changed by that assertion repair.
Client build23367 exits0 (`UP114-bless-client-build.log`). Dormant data/schema/
inventory validation passes32/32. Static fixture review caught an old snapshot
retaining other Mass metadata, a selection helper restoring Advanced instead of
the requested Expert rank, and a clean forecast mislabeled already-materialized.
Repair these before the first native build/run; add focused uncapped School
duration and Curse-counter cases rather than relying only on capped values.
Both-target build20799 exits0. Principal72787 passes53/55 in9.033s, zero
skips (`UP114-bless-principal` log/XML). The offer fixture's fixed seed did
not select either requested perk from the larger active Light pool; search a
bounded deterministic seed range instead of assuming one random offer.
The materialized Bless forecast retained its old family marker alongside the
new one. Explicitly replace the saved-v3 Bless family status in the shared timed
Lua path before adding its new marker; test ordinary/Mass authoritative refresh
as well. A helper returned a raw Bonus pointer from a temporary filtered list;
retain shared ownership when inspecting computed forecast bonuses. Activation
remains pending the repaired native gate.
Repaired both-target28088 exits0 (`UP114-bless-repaired-build.log`). Repaired
31806 passes55/55 in10.977s and activated5126 passes55/55 in11.867s, zero skips.
Both native runs include the repaired Bless description assertion. Data/schema/
inventory32/32 and module check pass. Litany and Mass Bless are active only after
this focused gate; independent final source/fixture review has no blocker.
Broad save/load, AI selection and modifier interactions remain Phase2; this is
not graphical acceptance or playable promotion.

### 2026-10-01 UP-114 Mass foundation — pre-build/data corrections

Independent production review found that the draft powerPercent metadata
accepted1..100 without an implemented magnitude consumer. Restrict schema,
native validator and saved-row helper to100 for the first Curse/Sorrow slice;
Mass Slow's60% requires its own consumer before the contract can widen.
The first Python invocation omitted tools/tests from PYTHONPATH and could not
import test_new_horizons_content. The repaired invocation then found six v2
schema errors because its synthetic downgrade retained the new v3-only variant
rows. Remove those rows from that old-profile fixture; native fixture adapters
also strip variant declarations during their explicit v1/v2 downgrades.
Repaired scoped data/inventory/schema validation passes31/31. These are fixture
and metadata corrections, not native execution or playable acceptance.
The additional variant contract case raises the focused data total to32/32.
Client build40655 exits1: the new Lua proxy returned CSpell* even though
Mechanics::getSpell exposes the spells::Spell interface, and used IBattleInfo
without its defining header. Return the interface type expected by SpellProxy
and include IBattleState.h. Root also corrects the custom Curse/Sorrow hero
descriptions to name mass targeting rather than overriding them with the old
single-target text. Native validation and activation remain pending.
Repaired client89090 and both-target84160 exit0. Initial principal12488
passes6/7, zero skips in2.330s (`UP114-initial` log/XML). Its only failure
incorrectly expected hasSchoolProficiency(Curse) to be false at no Shadow rank:
Curse is Level1 and is ordinarily accessible at that rank. Verify the actual
zero rank rather than asserting an inapplicable acquisition restriction.
Review also found the existing Curse/Sorrow CStack-only effect application
could skip already-mutated StackWithBonuses targets in detached predictions.
Use the battle::Unit interface and materialize projected state in the focused
regression. Base-family spell immunity is now checked alongside variant immunity;
ordinary nonvariant immunity avoids duplicate checks. Final rebuilt validation
and activation remain pending; the clean first forecast is not proof against
the previously-mutated projection defect.
Compatibility-initial74707 passes40/41, zero skips in5.045s. The only failure
is Bless's previously stale description substring: it expects "Light School
coefficient:115%" while the existing production description reports the
combined Spell Power coefficient. Its duration/effect assertions pass and this
slice does not change that Bless description. Record the assertion reconciliation
for Phase2; do not alter Bless gameplay or silently report41/41. The remaining
availability/schema, Sorrow and legacy/v3 Bless guards pass.
Final both-target37060 exits0 after the Unit-interface/projection fixes.
Principal99435 passes48/48 in7.091s and activated21991 passes48/48 in7.326s,
zero skips, with the stale Bless text assertion explicitly excluded. The new
eight-case suite includes pre-materialized projected state and one family
replacement, real scroll sources, physical-book removal, both immunity IDs,
tooltip scope and accepted cost/effect parity. Data/inventory/schema32/32 and
module drift check pass. Grand Malediction and both entries are active only
after this gate; source/native verification does not imply playable promotion.

### 2026-10-01 Shadow perks — pre-build fixture corrections

Root review repaired Hex's fixture-local perk lookup to use the nested
`rules["skills"][skillId]` registry. Life Drain's existing no-rank baseline
was preserved; explicit Basic cases compare the same232 damage with139 versus
174 healing. Selected fixtures must reserve enough missing survivor HP for
the intended healing, rather than accidentally testing the ordinary-heal cap.
An AI draft wounded20 Pikemen by100 HP, killing ten and leaving no repairable
survivor injury. The owner switches to a high-HP recipient before compilation.
Ordinary healing cannot restore casualties: aggregate missing army HP is not
the same as repairable survivor HP. These were source-review catches; no failed
native execution is claimed.
Both-target99778 exits1 (`UP112-principal-build.log`): the AI fixture passed
an rvalue and a const value to CUnitState::damage, which requires a mutable
int64_t reference and may update the amount to actual damage. Root uses named
mutable wound variables. Client linked successfully; no native pass or perk
activation is claimed. Confirm mutating damage API signatures when adding
detached unit-state fixtures. Repaired both-target96988 exits0.
Principal57470 (`UP112-principal` log/XML) passes20/23, zero skips in6.372s.
The new Hex attack fixture attached BLOCKS_RETALIATION to its victim, although
that bonus belongs on the attacker; the victim retaliated and killed the
cursed stack. The reactive injury packet itself already matched53. Root
corrects the fixture, not the production mechanic. Both new AI cases failed
at strategic spell choice before running any projection assertions. The
bounded Phase1 checks use the existing legal-target enumeration and shared
detached forecast/value seams, retaining exact authoritative parity and live
immutability assertions. Strategic nonselection is deferred to Phase2 rather
than tuning AI valuations in this perk slice. No activation is claimed yet.
Fixture-repaired both-target16858 exits0; principal77901 passes23/23, zero
skips in6.320s. Independent review subsequently requires a background enemy
in Blood Drinker to avoid battle-result finalization invalidating pointers
used for accepted/projection parity. This retains the exact20 damage/15 heal
assertions and does not exercise or bypass battle-result handling. Final
build/native evidence follows before activation.
Final both-target68123 exits0; final principal6616 passes23/23, zero skips in
6.246s. Activated33834 passes29/29, zero skips in7.838s; data/inventory19/19
and module drift check pass. All reported build/fixture failures are repaired;
strategic AI nonselection remains explicitly deferred, not claimed fixed.

### 2026-10-01 Havoc perks — saved fixture profile review

Before the test build, independent review found the new Pyromancer/Cryomancer
parameter fixture inherited saved v2 magic rules, although these new damage
perks deliberately use the current v3 coefficient model. The owner explicitly
selects savedV3Formula before preparing the game. Review also corrected the
Fire Wall fixture's nested skills registry lookup and expected damage to include
both Basic Havoc and Pyromancer (40 + floor(43 x 1.15 x 1.15) = 96).
No failing native execution is claimed: these were repaired before compilation.
New fixtures must explicitly select the saved ruleset they intend to exercise;
real-hero scaling alone does not switch a legacy magic snapshot to v3.
Both-target12189 exits1 (`UP109-fixture-build.log`): the root-owned AI parity
fixture passed serialized BattleAction DestinationInfo records directly to
castEval, which requires resolved battle Destinations. Root uses the existing
action.getTarget(projected.get()) conversion against the detached callback.
Production/client compilation remains successful; no native pass or activation
is claimed. Resolve action targets in the destination battle before evaluation,
never cast serialized records or bind them to live-unit pointers.
Repaired both-target48589 exits0. Principal73570 exits1: 13/17 pass, zero
skips in4.325s. All four Controlled Blast cases fail before behavior because
the inherited v2 fixture retains current-v3 selectedPlacement fields. The
bounded owner explicitly selects savedV3Formula for these new/modified feature
setups, keeping all damage/target assertions. Eight damage-perk cases, both
Fire Wall cases, legal offer/save-load and actual AI parity already pass.
Retain UP109-principal.log/.xml; no Controlled Blast activation is claimed.
The older hybrid v2 helper's broader uses are a separate Phase 2 fixture audit,
not reason to weaken legacy validation or consume this Phase 1 slice.
V3-fixture build10278 exits0. Repaired principal5598 passes20/20 in5.977s;
activated19887 passes28/28 in8.153s, zero skips. Data/inventory19/19 and module
check pass. All assertions are retained; the three perks are active. This is
source/native evidence, not graphical acceptance or playable snapshot delivery.

### 2026-10-01 Financier — mastery namespace in fixture signature

Both-target25033 exits1 (`UP106-fixture-build.log`): the new fixture uses
`MasteryLevel` as a parameter type, but it is a namespace and its enum type is
`MasteryLevel::Type`. Production and client link; the fixture owner is repairing
the signature before retry. No assertions are weakened and no principal native
pass or Financier activation is claimed. Use the declared enum type in helpers.
Repaired both-target21766 exits0 (`UP106-fixture-repaired-build.log`). Principal
39424 passes3/3 in58.108s; activated95915 passes15/15 in61.975s, zero skips.
The same fixture assertions remain intact; data/inventory19/19 also pass.

### 2026-10-01 Quartermaster — terminal battle lifetime review

Before compilation, independent review identified a new use-after-free risk:
cleanup used the incoming BattleInfo callback after checkBattleStateChanges
returned true. The terminal path can synchronously run setBattleResult,
endBattleConfirm, battle-query removal, battleFinalize and BattleEnded, which
erases the owning currentBattles entry for no-dialog AI battles. An empty
allowance does not make accessing the old callback safe. Preserve the existing
immediate-return terminal contract, or re-fetch by a previously captured BattleID
and operate only on a still-present state. Never infer lifetime merely from the
body of checkBattleStateChanges without tracing the result-finalization calls.
Repair requested before build; no failing native execution is claimed.
The runtime restores immediate return on terminal checks; repaired-source review
has no remaining blocker. The same review also repaired a new Catapult helper's
dereference of lightweight proxies without creature identity, and added the new
genuine turn reason to Fire Wall's activation whitelist. Build88648 is running;
source review alone is not a native pass.
Both-target88648 exits1 (`UP101-build.log`): AI target forecasting passes const
raw output to CUnitState::heal, whose amount argument is a mutable reference.
Use a fresh mutable copy for each candidate, not one shared mutable amount across
the target loop (heal may consume/change it). The subsequent pair construction
diagnostic is cascading from that call. Root repairs this API mismatch without
changing output formulas or weakening the six-case fixture. Retry is pending.
Repaired log reaches both executable links; after the original session handle
was lost, its same CMake/Ninja processes were monitored to exit. A fresh target-
freshness check exits0. Principal73624 exits1: five of six cases pass, zero skips;
the fortified-town Catapult fixture cannot advance to its intended active unit
and fails before its structural-output assertions. A bounded Luna fixture owner
is diagnosing setup versus runtime admission; retain UP101-principal.log/.xml
and do not activate Quartermaster or weaken its half-output assertions.
Fixture repair: the tiny map's one-Pikeman armies are not viable against the
opening automatic Citadel tower shots. Turrets queue before the Catapult;
siege weapons do not keep a defeated army in combat. The bounded owner adds
durable troop stacks before combat begins, retaining legal offers, accepted
Catapult actions and exact half-output assertions. Minimal rebuild21374 is
running; repaired native evidence remains pending.
Repaired fixture build21374 exits0. Principal47670 passes6/6 in2.277s;
activated88418 passes22/22 in8.107s, zero skips; data/inventory19/19 pass. This
confirms the bounded fixture repair while retaining the failed run's evidence.

### 2026-10-01 Master Gunner — callback Unit contract

Both-target76640 exits1 (`UP099-build.log`): two continuation cleanup branches
access the new CUnitState field through `const battle::Unit *`, whose interface
does not expose it. Root replaces those reads with the shared raw-pending callback
instead of an unchecked downcast or permissive compiler flag. Preserve this
failed log; no native pass or registration activation is claimed. Review callback
return types when adding state-dependent flow branches.
Repaired build20641 exits1 (`UP099-repaired-build.log`): production compiles,
but the new fixture lacks the direct HypotheticBattle declaration include.
Root adds its owning header without changing assertions or runtime behavior.
Keep direct dependencies explicit in standalone fixtures; retry/native results
remain pending.
Retry14515 exits1 (`UP099-fixture-repaired-build.log`): root initially used a
nonexistent `HypotheticBattle.h` filename. Symbol lookup locates the declaration
in `AI/BattleAI/StackWithBonuses.h`; corrected that direct include. Resolve owning
headers from source, not class-name guesses. No assertions were weakened.
Corrected both-target89421 exits0 (`UP099-header-repaired-build.log`). Native88930
passes3/4, zero skips in1.445s (`UP099-principal.log`/`.xml`); the principal
ratio comparison alone fails. Its ordinary baseline is sampled before advancing
to the Ballista, while that advancement submits Defend for the prospective
targets and changes their Defense. The fixture owner is checking same-state
baseline timing; do not widen tolerance or activate before the repaired gate.
The fixture moves only its ordinary baseline after accepted Defend advancement;
the60% assertions/tolerance remain unchanged. Both-target76128 exits0. Repaired
principal71745 passes4/4, zero skips in1.450s; activated74243 passes16/16, zero
skips in5.022s. Data/inventory19/19 pass. Registration is active; no full binary
battle-save or rendered/playable acceptance is claimed.

### 2026-10-01 Fortification Engineer — fixture registry nesting

Independent source review caught the new fixture's activation helper indexing
the full perk registry without its `skills` level. That would fail setup before
any siege assertion. The fixture owner repaired the lookup before native
execution; no failed native run is claimed. Both-target19393 is running with
12 jobs (`UP098-build.log`). Keep ordinary-control activation breadth deferred
without weakening the accepted manual shot and exact Siege-output assertions.
Both-target19393 subsequently exits0. Principal2955 passes4/4 and activated75235
passes12/12, zero skips; data/inventory19/19 pass. The schema correction is
verified without bypassing normal perk offers or authoritative build/shot paths.

### 2026-10-01 Hold Fast — fixture callback type

Both-target77264 exits1 (`UP092-build.log`). Production compiles; the new fixture
passes battleActiveUnit's `const battle::Unit *` to an action helper unnecessarily
restricted to `const CStack *` (line195). Use the callback Unit contract, not a
downcast or permissive compiler flag. The worker also strengthens Hold Line from
Wait (which skips bad Morale) to an accepted Move followed by a normal activation.
Preserve the original failure; no native pass or feature activation is claimed.

Repaired both-target80348 exits0 (`UP092-repaired-build.log`). Native88734 passes
5/6, zero skips (`UP092-principal.log`/`.xml`): actual Defend/Hold Line Morale
gates, continuation/round/Time Stop boundaries, serialization and branch isolation
pass. The accepted Second Wind setup fails its ordinary command-capability check.
Trace and repair the fixture's rank/action availability without bypassing command
validation or weakening the genuine-activation assertion. Registration stays planned.

Trace found an existing canonical mismatch, not a missing Skill rank: Second Wind
requires `moved()`, while accepted Defend sets `defending` but not `movedThisRound`.
The canonical Order accepts a completed normal Creature Activation, including
Defend. Root retains the accepted Defend→Second Wind test rather than replacing
the real grant with an injected test bonus. Repair only canonical target
validation and its AI heuristic to accept Defended stacks; preserve legacy
validation and existing queue flags. No global Defend bookkeeping rewrite.

Independent review also found original-side Second Wind lookups that missed a
hypnotized recipient's controller. Repair shared/live/detached activation, hazard
and action-end state lookup, and remove the redundant Defend grant comparison
against original action.side (the request path already authenticates current
owner). Final both-target2758 exits0. Principal85726 passes7/7; activated69879
passes28/28, zero skips in8.612s. Independent repaired-source review has no blocker;
the actual hypnosis case verifies grants, expiry, branch isolation and completion.
Registration is now active. The earlier failures remain retained above.

### 2026-10-01 Standard Bearer — configured Morale caps

Both-target68249 exits0 (`UP091-standard-bearer-build.log`). Native17745 passes
3/4 (`UP091-standard-bearer-principal.log`/`.xml`): both actual Morale gates and
branch-local adjacency pass. The caps fixture assumes ±10 from its map-level
chance arrays, but AFactionMember uses the existing engine-level chance-vector
sizes (±3 in this profile). Derive the expected clamp/MAX bounds from the same
configured engine settings; preserve the raw-before-cap assertion and production
behavior. Do not activate the perk before the repaired focused gate passes.

Independent Astra repair review confirms the dynamic bounds/raw-before-cap test.
Repaired both-target42607 exits0; principal82873 passes4/4, then activated74455
passes39/39 with zero skips in10.603s. The original caps failure is retained above;
production needed no repair. Standard Bearer registration is now active.

### 2026-10-01 Reserve — fixture compile and portable sidecar

Both-target build25950 exits1 (`UP091-build.log`). The new fixture used an
unqualified CUnitState and an invalid const_cast from battle::Unit to CStack.
Repair the namespace and resolve the authoritative stack by unit ID. Review also
found incomplete-CStack calls in the BattleInfo serialization template: move
collection/restoration into compiled helpers rather than depending on permissive
compiler behavior. Add the Reserve UnitChanges downgrade guard and test a legal
perk offer with a local active rule before claiming coverage. No native gate or
feature activation is implied by this failed build.

Repaired both-target64222 exits0. Native66681 passes3/4 (`UP091-principal.log`
and `.xml`): accepted Wait/delayed extra-reach movement, owner/reason gates and
immobilization pass. The sidecar itself is present after binary roundtrip, but
the assertion uses alive-only getStack on a CStack binary format that omits unit
health. Inspect the saved field through ID lookup with onlyAlive=false; do not
claim full unit-health snapshot migration. The bounded detached-AI ownership
case is added before the next gate. Registration remains planned.

Final fixture compile26032 exits0. Native91032 passes4/5; the AI case mistakenly
uses the opposing player's callback, which correctly hides the controller hero.
Use the current controller's PlayerColor(1) callback and assert that its hero is
visible. Preserve the visibility boundary; do not expose enemy perk state to AI.

Controller-view compile34777 exits0. Native55349 and activated native69809 both
pass16/16 with zero skips; the final XML records4.778s. Five Reserve cases verify
the real delayed movement, lifecycle gates, guarded state roundtrips, immunity
and branch-local/controller-aware Wait forecasting. Registration is now active.
Full binary unit-health/state persistence is not certified by the Reserve sidecar
test and remains a separate Phase2 audit; wider Wait-choice planning is deferred.

### 2026-10-01 Twist of Fate — actual-runtime fixture header

Production compile77876 and32 focused regressions pass, but new-fixture
compile59606 exits1 (`UP089-runtime-fixture-build.log`): the Chain Lightning
case calls `setTestSpellPointTotal` without its direct SpellPointTestUtils header.
Add the fixture header only; do not modify Mana rules or weaken the runtime
assertion. Preserve the failed log and record the succeeding compile/native
gate separately. The perk remains planned; this is not completed coverage.

Repaired compile16551 exits0. Native64569 passes34/35, zero skips in9.596s
(`UP089-runtime-focused.log`/`.xml`); the new chain case fails before casting
because `castOn` rejects its setup. Forecast and detached-branch cases pass.
Independent review also requires an explicit second chain hop and unchanged
secondary HP when the primary reroll resists, so the test cannot pass with
post-preparation resistance. Trace the cast rejection and repair only evidenced
fixture setup; do not bypass ordinary spell availability or casting legality.

The fixture had not started the combat round after battle setup. Add the normal
`beginCombat()` transition before resetting the seeded RNG, and an explicit
secondary enemy plus unchanged-HP assertion. Both-target4456 exits0; native24774
passes3/3, then final native34416 passes35/35, zero skips in9.458s
(`UP089-runtime-verified.log`/`.xml`). Independent repair review has no blocker.
Casting legality is preserved and the chain timing assertion is strengthened.
This verifies the bounded runtime slice, not missing scripted hostile procs.

### 2026-10-01 Chain of Fortune — first native gate

Both-target build74505 passes (`UP087-build.log`). Native99946 passes24/26,
zero skips in7.422s (`UP087-focused.log`/`.xml`), failing the round-carry and
controlled-reaction fixture cases. The round case changes the live probability
table after combat setup but still receives positive Luck; the reaction case
emits only the initiating attack. A bounded fixture worker is tracing whether
setup or production is responsible. Preserve the failed evidence and do not
count Chain as completed until the repaired principal gate passes.

The live Luck randomizer reads game settings, not the battle forecast table.
Use No Luck on the round recipient after verifying its carried+1 entitlement,
keeping the actual100% curve for the later fresh trigger. The reaction fixture
also attached BLOCKS_RETALIATION to its initiating attacker, which explicitly
prevents the target's counterattack in BattleActionProcessor. Remove that
fixture bonus rather than weakening production retaliation validation.

Repaired both-target3912 exits0 (`UP087-repaired-build.log`). Native73716 passes
26/26 with zero skips in6.779s (`UP087-verified.log`/`.xml`); all six Chain
cases and20 Luck regressions pass. Independent fixture-repair review has no
blocker. The initial failed report remains retained; no production rules were
relaxed. Coverage may now count Chain, but playable acceptance remains separate.

### 2026-10-01 Gambler — fixture probability-field visibility

Both-target build49572 exits1 while compiling NewHorizonsGamblerTest.cpp.
The derived TEST_F cases set goodLuckChance/badLuckChance, but the fixture
declares both private. Retain `UP086-build.log`; make only the fixture's
configuration surface protected or provide an appropriate setter. No production
rule changes are warranted. The succeeding build and native execution must be
recorded separately before counting Gambler as implemented coverage.

The approved Chain of Fortune recipient clarification also exposed a data
description drift: the first post-amendment data run passed18/19, failing the
canonical perk-definition comparison. Align both registry description fields
to "next different friendly stack", regenerate the module, and rerun the
same19 checks. The perk remains planned; no runtime behavior was activated.

Repaired both-target build68457 exits0. Native52671 passes15/20 with zero
skips (`UP086-focused.log`/`.xml`); all five Gambler gameplay cases expose
the fixture's unneutralized Advanced Luck+2: expected first bonus3 is5,
expected baseline0 is2, and expected penalty-2 is0. Preserve legal Basic
Fortune's Favor and Advanced Gambler selection, but establish the fixture's
intended zero-Luck baseline. Existing14 Luck regressions and the new state/
packet case pass. Do not change production Luck rank rules to satisfy the
fixture or count the failed run as completed Gambler evidence.

Fixture baseline repaired with a local ordinary LUCK-2 compensation after
legal Advanced Luck selection, rather than removing rank/perk requirements.
Final both-target76677 exits0 (`UP086-baseline-build.log`); native39678
passes20/20, zero skips in5.324s (`UP086-verified.log`/`.xml`). The same
selection now exercises the actual first-window, penalty, controller,
continuation/activation and detached outcome paths; the earlier failures are
retained. Data/inventory19/19 also pass after the description-drift repair.

### 2026-10-01 Gambler — pre-build lifetime and refresh identity review

Independent review caught two pre-build issues. Selective genuine-activation
expiry alone did not protect Gambler from the legacy UntilGetsTurn cleanup on
HERO_SPELLCAST continuations. Exclude only the Gambler marker from legacy
cleanup in authoritative and detached state, leaving unrelated lifetimes
unchanged; remove it through battleBeginsActivation, including Second Wind.
The generic SetStackEffect removal comparison also ignores stacking identity.
An existing Gambler penalty already has the required next-activation expiry,
so avoid unnecessary removal/replacement rather than widening a generic
removal API. Add only when its unique marker is absent. No failing native run
is claimed for these review findings; continuation/activation assertions must
verify the repair before completion.

### 2026-10-01 Second Chance — fixture processor header

Both-target build26224 exits1 at the new fixture's accepted shot submission:
`CGameHandler.h` only forward-declares `BattleProcessor`, so invoking
`gameHandler->battles->makePlayerBattleAction` requires the direct processor
header. Preserve `UP084-build.log`; add that include to the fixture without
altering runtime behavior. Rebuild both targets and run the six new cases plus
the scoped Luck/Providence regressions before claiming native completion.

Repaired build15358 exits0. Native59247 passes12/14 with zero skips: all six
new Second Chance cases and six Fortune's Favor/Lucky Aim regressions pass.
The two older Providence guards fail before combat with `Earlier New Horizons
perk tier is still required`; each selected its Advanced perk without a Basic
perk. Retain `UP084-focused.log`/`.xml`. Add legal Basic Fortunate Aim before
Nature's Providence in only those two setups; it does not alter these melee
cases. Do not weaken production prerequisites or omit the failing guards.
Repeat both-target compilation and the complete14-case filter.

Final-build77313 passes, but native64576 again passes12/14: those two setups
now reject `Unavailable New Horizons perk selection`. Root's suggested
Fortunate Aim prerequisite was incorrect: the actual registry requires Advanced
rank for it as well. Preserve `UP084-final.log`/`.xml`; replace only the new
prerequisite selections with verified Basic Elven Precision, whose ranged-only
Defense ignore cannot affect these melee cases. Inspect actual registration
before assigning a prerequisite; do not infer its rank from its name. Rebuild
and repeat the same filter, keeping both failed runs.

Verified prerequisite build23927 exits0. Native6011 passes the same14/14 filter,
zero skips in3.859s; preserve `UP084-verified.log`/`.xml`. Six new Second Chance
cases, six recent Luck guards and both legal Providence guards pass on binary
`8a34b62c978fa1842b571c9d1e0c5344583d18aa4678e6c5f73f5d61cd553f5f`.
No production prerequisite, damage or existing Providence rule was relaxed.

### 2026-10-01 Second Chance — pre-build detached AI review repairs

Independent review and root inspection caught two blocking defects before the
first build. Retaliation recording looked up its primary target in a map that
deliberately excludes the main attacker; use the detached attacker state for
that ID and the map only for collateral targets. Fortune projection also
inferred a negative outcome from negative base Luck even when the strike was
explicitly guaranteed positive. Gate negative inference with `!positive`,
matching authoritative mutually exclusive outcomes. No failing native run is
claimed for these pre-build findings. Add focused melee-retaliation and
forced-positive-under-negative-Luck regressions before accepting the slice.
Exact probabilistic multihit distributions and explicit stochastic-result
replay remain Phase 2 work, not silently certified by certain-roll tests.

### 2026-10-01 Lucky Aim — calculator fixture shot geometry

Native65046 passes4/8 and fails the three new Lucky Aim cases plus the older
Elven Precision positive-shot case (`UP082-focused.log`/`.xml`). Both fixtures
placed the shot target adjacent to its shooter, triggering New Horizons'50%
adjacent ranged penalty. Older Elven Precision expected unpenalized damage;
relocate only its fixture target to three hexes away, within normal range.
The new fixture also needs independently verified Attack/Defense coefficients
before setting exact expectations. Do not halve/double expected values blindly
or change production penalty rules to make a fixture pass. Repair/native gate
pending; no functional completion claim is made from the failed run.

Geometry build27645 passes; native93424 passes5/8, with both older Elven
Precision cases now passing. The new assertions establish Attack24, Defense30
and Defense coefficient0.025. The remaining expectations incorrectly added a
negative Defense factor to Lucky damage instead of multiplying it:
4000*2*(1-6*0.025)=6800 (maximum10200). After bypass, positive Attack advantage
adds to yield8200/12300 mathematically. Lua truncation produces12299 for that
fractional maximum; retain a tightly bounded one-HP tolerance and defer general
floating-point rounding stabilization to Phase2 rather than alter production
damage math for this perk. Final both-target62197 exits0; native69961
passes8/8, zero skips in2.487s, reports `UP082-final.log`/`.xml`.
Independent repaired-fixture review agrees with the factor composition and
the tightly bounded truncation tolerance.

### 2026-10-01 Lucky Aim — ranged Defense fixture API

Both-target build72030 failed in the new fixture because `CStack::getDefense`
requires the ranged-context argument; unlike an abstract/defaulted interface,
the concrete override has no zero-argument call. Use `getDefense(true)` for the
shot target assertion. Log `UP082-build.log` retains the failure; production
client73900 compiled successfully. Repaired build76508 exits0; final build62197
and native69961 pass after the separate geometry/formula repairs described above.
Future concrete-unit tests must use explicit attack context rather than assume
default arguments are inherited through overridden APIs.

### 2026-10-01 Fortune's Favor — detached-unit test pointer comparison

UP080 strengthened its AI test to use `HypotheticBattle::getForUpdate` units
instead of live stacks. Both-target build7003 failed because GTest compared
unrelated derived pointer types (`StackWithBonuses*` and `CStack*`) directly.
Log: `build/new-horizons-linux/UP080-final-build.log`. Compare both through their
common `const battle::Unit*` interface; do not remove the detached-unit assertion
or revert to a live-unit forecast. Repaired both-target56878 exits0; native69200
passes4/4 with zero skips in1.398s (`UP080-final.log`/`.xml`). Earlier build2508 and native48561 passed four tests, but that AI
case did not yet establish detached bonus-bearer parity. Production compilation
and perk logic were not implicated by this fixture-only failure.

### 2026-10-01 Veteran — AI Guardian Spirit absorption

Independent source review finds a blocking integration defect before native
execution: newly classified physical preview hits absorb Guardian Spirit, but
the primary/collateral strike payload stores the post-absorption amount and
replay absorbs it again. Pre-clamping incoming hits to creature HP also ignores
the buffer, allowing lethal hits to become nonlethal. Preserve incoming payload
damage separately from actual HP loss and apply absorption exactly once before
the health cap. Focused regression must compare forecast versus committed HP,
Guardian Spirit buffer and Veteran history together. Client build12894 is the
pre-repair compile checkpoint, not verification of this behavior; repair and
first succeeding native run remain pending.

Both-target50025 compiles after the repair and duplicate-declaration cleanup.
Native68335 runs14 cases, zero skips:12 pass and2 new AI fixture assertions
fail because `effectPreview` is optional and absent when no separate Fortune/
mark hook needs it. Ordinary attack forecasts retain the target in affectedUnits;
inspect that actual snapshot rather than adding unrelated perks or changing
production to manufacture a preview object. Preserve `UP078-focused.log`/`.xml`.
All five new server cases, owner-scoped detached activation, three Guardian and
three Bulwark guards pass. Focused repaired retry remains pending.

Both-target27203 passes; native99360 passes13/14, zero skips. Preserve
`UP078-focused-repaired.log`/`.xml`. Remaining fixture expects one hit but sees
two because it gives BLOCKS_RETALIATION to the target rather than the attacker.
Move the helper call to the attacker in both Guardian fixtures; retain the
single-hit and state-parity assertions. Buffered overkill already passes.

Both-target60339 passes; native11688 passes13/14, zero skips. Preserve
`UP078-focused-final.log`/`.xml`. The sole failure is the authoritative buffer
expected40 but observed0 after the projection; the fixture grants an unmarked
buffer before beginCombat can clear it. Both fixtures must grant after combat
initialization and assert the actual pool before evaluation. Do not weaken the
no-live-mutation assertion or count buffered-overkill evidence without a
positive-buffer precondition. Preview/commit HP, pool and history comparisons
otherwise pass; final meaningful retry remains pending.

Both-target8101 passes after real-marker/post-initialization buffer setup.
Native19447 passes14/14, zero skips, in4.124s; first fully succeeding reports
`UP078-focused-buffer.log`/`.xml`. Binary SHA-256
`ef35142a822610a400a5f9dad60f358fb21645a2cb15736ce325a84f0428dca8`.
All original failed logs remain retained. Independent source review closes the
logic/compile blockers; final buffer preconditions make the two AI regressions
meaningful. No separate broad-suite or playable acceptance is claimed.

### 2026-10-01 Formation Fighting — inventory columns

Initial UP-077 offline check runs19 tests,18 pass. Root reverses the CSV
Implementation/Art fields for the activated perk and accidentally marks the
neutral fallback art Provisional. Restore Implementation=Provisional and
Art=Not done; the existing inventory guard correctly rejects that claim.
No artwork or runtime failure is implied. The repaired19-test gate passes.

Client57962, native prerequisite84365 and both-target60326 compile. Native
12147 runs19 cases with zero skips;15 pass and4 fail. Preserve
`UP077-focused.log`/`.xml`. Three new fixture failures concern opponent-hidden
hero ranks in a player0 detached view and purported isolation of stacks in a
starter-army battle roster; diagnose and repair fixtures without bypassing
visibility or relaxing adjacency rules. An existing Encirclement repeated-hit
assertion also fails; do not claim it repaired or certified. The no-perk gate
and existing Shroud/Pavise/Flank cases pass. Final retry remains pending.

Both-target14193 passes after starter-unit isolation and defender-owned AI view
repairs. Native25832 passes17/18: only the new combined Shroud fixture's
post-attack geometry assertions fail. Move the same support-away/restoration
assertions before combat advances state, then retain the authoritative protected
hit assertion; do not relax the assertions. Both-target74477 passes and
native61868 passes18/18, zero skips, in4.465s; first succeeding reports
`UP077-focused-final.log`/`.xml`. Independent repair review passes. Original
`UP077-focused-repaired.log`/`.xml` remain failure evidence. The excluded
Encirclement repeated-hit case remains a separate Phase2 finding: damage changes
4897 versus5900 after real execution; no Formation Fighting perk is selected
and its gate is inactive, so do not attribute that assertion to this perk or
claim the wider suite fully healthy.

### 2026-10-01 Estate Network / Quick Study — early CMake registration

Client build attempt fails during CMake regeneration because the root registers
NewHorizonsQuickStudyTest.cpp before the worker has created it. Preserve
`UP075-client-build.log`; defer registration until that real fixture exists
(no empty placeholder). Retry the client target while the fixture is written,
then register it before native compilation. No gameplay failure is implied.

Client retry51396 and isolated Quick Study7306 / economy49783 compiles pass.
Both-target83432 fails in the Estate AI Environment adapter: converting
BattleInfo* to BattleCb* needs BattleInfo's complete definition. Add the direct
BattleInfo.h include; preserve `UP075-final-build.log`. Focused execution remains
pending; this is a fixture include repair, not a production change.

Repair build65084 passes. Native18122 runs29 tests with zero skips;28 pass.
The AI choice fixture receives a legal mixed offer and correctly chooses its
existing Inspirational Leader preference, contradicting the fixture's demand
for Estate Network. Isolate the new perk's legal offer while retaining its
selected Tax Collector prerequisite and canonical registry structure; keep
comparative AI valuation deferred. Preserve `UP075-focused.log`/`.xml`.

Fixture repair82666 and final style-only rebuild20329 pass both targets.
Focused native80431 passes29/29 without skips in10.865s; first succeeding
reports `UP075-focused-repaired.log`/`.xml`. Independent review approves the
isolated offer repair. No production formula/AI valuation repair was needed.

### 2026-10-01 Learning Mentor — fixture query header

Native build `85716` fails because the new server fixture calls
QueriesProcessor methods with only CGameHandler's forward declaration visible.
Preserve `UP073-mentor-test-build.log`; include QueriesProcessor.h directly.
Client build `93948` already passes. This is a fixture compile repair, not a
production gameplay failure. Both-target retry `42943` fails in the AI loopback
fixture because IClient is incomplete at inheritance. Include IClient.h directly;
independent review also identified explicit QueriesProcessor.h and CHeroHandler.h
requirements. Keep `UP073-mentor-final-build.log`; focused execution is pending.

Repair build `58230` passes both targets. Native `85399` runs25 tests, zero
skips:23 pass. Mentor's enemy gate assumes the generated enemy starts at zero
Experience, but it starts at83; snapshot its initial XP and assert unchanged.
The unrelated existing Muster perk fixture clears earlier selected tiers before
selecting Advanced/Expert perks and throws the existing strict-tier validation.
Record that fixture integration finding for Phase2, not as Mentor regression.
Preserve `UP073-mentor-focused.log`/`.xml`; Mentor repair/retry remains pending.

Fixture-repair both-target build81553 passes. Focused retry52779 passes24/24,
zero skips, retaining all new Mentor/market cases and the economy/Muster weekly
guards. `UP073-mentor-focused-repaired.log`/`.xml` are the first succeeding
native evidence; the failing unrelated Muster higher-tier case remains recorded
above, not silently claimed repaired. No production repair was needed.

### 2026-10-01 Grand Formula / Tax Collector — fixture rank type

Native build `65937` fails in NewHorizonsEconomyTest.cpp:155/162 because
MasteryLevel is a namespace of rank constants, not a type. Preserve
`UP070-native-build.log`; use int helper parameters matching the skill API.
Production client build already passes. Reviewer separately caught and worker
repaired Grand Formula's unmatched final brace and identical-SP radius test
inputs before those fixtures were registered. Retry and native execution remain
pending; no verification or playable claim follows from these source repairs.

Both-target retry `57824` fails in the Grand Formula AI fixture's binary
CGameState snapshot because GameSettings is incomplete at template
instantiation. Preserve `UP069-UP070-build-retry.log`; include GameSettings.h
directly, keeping the full live-state invariant assertion. No production change.

Both-target repair build `24828` passes. Native `86706` executes 38 tests with
zero skips: 35 pass, including all Tax Collector payout/AI cases and Grand
Formula AI. Three new server cases fail: the armed ward does not negate either
high-level cast, and the two-hex Archangel marked beyond radius still occupies
a legal affected hex. Preserve `UP069-UP070-focused.log`/`.xml`; repair the ward
fixture prerequisites and use a one-hex radius-boundary subject, retaining real
accepted casting and radius assertions. Grand Formula remains unverified.

Final fixture-repair both-target build `68608` passes. Native retry `45801`
passes38/38 with zero skips; `UP069-UP070-focused-repaired.log`/`.xml` retain
the first passing combined result. Tax Collector isolated25581 also passes
13/13. Review finds no remaining blocker. No production change was needed for
the failed fixture prerequisites; these are not evidence of a gameplay defect.

### 2026-10-01 random-form AI / Mana Conservation — fixture include

Client build `53309` succeeds. Test build `71345` fails because the new AI
fixture queries `CModHandler::getActiveMods()` without its complete header.
Preserve `UP066-UP068-test-build.log`; add the direct CModHandler include rather
than relying on transitive headers. Production behavior is not changed by this
repair. Review also corrects the fixture's even-pool midpoint comparison to use
RNGStub's actual draw and adds pre-cast live JSON/RNG snapshots. Both-target
retry `77609` also fails: the root's RNG snapshot helper was passed the entire
GameRandomizer instead of its CRandomGenerator. Preserve
`UP066-UP068-build-retry.log`; obtain the generator through getRandomGenerator,
check its concrete type and serialize that generator only. Both-target repair
build `66640` passes. Native `65013` executes 69 tests with zero skips: 68 pass,
including all ten Mana Conservation tests; the injected random-form AI fixture
fails because production score (-11483.907) differs from its independently
computed mean (-3.906), and the supposedly favorable pool is negative. Preserve
`UP066-UP068-focused.log`/`.xml`; investigate forecast/fixture parity without
weakening the signed full-pool or actual evaluator-selection assertions. No
promotion or complete Polymorph coverage is claimed.

Isolated Mana Conservation run `36603` passes 10/10, zero skips, in 2.823s;
reports `UP068-mana-conservation.log`/`.xml`. Test binary SHA-256:
`b76baa1a39c38c1e61e71215ea2f6ced5131d7fda0329a5fa647c38652dc82e8`.
This certifies the independent perk, not the still-failing random-form AI slice.

AI-only original-binary run `89456` reproduces the same failure, excluding
cross-test ordering as the cause. Diagnostic both-target build `40205` passes;
AI-only native `15212` still fails. Its per-form traces agree with the oracle
and confirm identical callback/detached pointers, but baseline pressure differs
before transformation. Preserve `UP066-AI-isolated-before-repair.log` and
`UP066-AI-diagnostic.log`; temporary diagnostic prints must be removed before
final verification/commit. Mana Conservation is committed/pushed separately
as `266c2c175`; the random-form AI slice remains uncommitted and uncertified.

Diagnostic cause: the fixture adds synthetic native creature bonuses after
constructing its live Ogre, leaving the initial baseline stale; install scoped
prototype bonuses before stack construction. Separately, blockRetaliation adds
BLOCKS_RETALIATION (prevents opponents countering its attacks), not NO_RETALIATION
(prevents the unit countering). The supposedly non-retaliating 100,000 Pikemen
therefore invalidated the favorable-pool setup. Use explicit ONE_BATTLE
NO_RETALIATION on that fixture defender. No production scoring formula changed;
all mean, harmful-outcome, selection and live-state/RNG assertions remain.
Temporary diagnostics are removed; independent repair review has no blocker.
Both-target repair build `52390` passes. Final native `31943` passes 69/69,
zero skips, in 17.000s, including the unchanged signed-mean and accepted-cast
assertions. Reports `UP066-UP068-build-repaired.log` and
`UP066-UP068-focused-repaired.log`/`.xml`. Verified test binary SHA-256:
`c298ce41d5703153814efbd98aa522a0b4b94f83536093c36942e369b6dc2f79`.
No playable promotion occurred.

### 2026-09-30 battle-form clone/presentation checkpoint — fixture includes

Both-target compiler probe `95666` fails in the new real-clone fixture:
`BattleFormTest.cpp` cannot convert `const CSpell*` to its `spells::Spell`
interface while `CSpell` is incomplete, and cannot call `BattleProcessor` through
its forward declaration. Preserve `UP066-clone-presentation-build.log`. Add the
full spell and battle-processor includes in the fixture; do not change production
interfaces or weaken the real-cast assertions. A successful retry/native result
is pending. Polymorph activation and playable delivery are not claimed.

Include-repair build `73946` passes; final Initiative-lifetime rebuild `41454`
also passes both targets. First native run `96469` exits 139 in the existing
`CloneApplyTest.AddsNewUnit` fixture after its detached unit mock returns a null
creature type. Preserve `UP066-clone-presentation-focused.log` and the separate
`UP066-clone-presentation-crash-gdb.log` trace. Binary SHA-256:
`c7e731110774f8496cb408ea2a692d59688170b13acaa82f94caf158de54e87f`.
This is a crash, not an unrelated test to skip. Trace the generic no-form state
path and restore its original no-creature-query contract before retrying; do not
hide it by weakening or excluding the Clone fixture. Final success is pending.

GDB trace `48533` identifies the production fault in
`CUnitStateDetached::getAllBonuses`: even an ordinary unit with no form payload
unconditionally queried its creature identity. Restore immediate delegation to
its underlying bonus bearer when `!hasBattleFormState()`; retained inactive
source-form metadata must still use the replacement native-bonus path. Keep
the original CloneApply fixture unchanged as a regression guard. The same native
run also exposed two health-fixture assertions: its new explicit tree version
started at zero, equal to the bonus cache's uninitialized epoch. Initialize it
at one (the preceding mock contract), keeping HP and 17/9 Initiative assertions
intact. Repair build/native verification remains pending.

Crash-repair both-target build `57163` passes. Native retry `39543` executes
51/51 successfully with zero skips in 2.640 seconds, including the unchanged
Clone/CloneApply regressions, cloned-form cast/JSON/recast/death, Time Stop's
paused timer with current-round Initiative invalidation, and status readback.
Reports `UP066-clone-presentation-build-crash-repair.log` and
`UP066-clone-presentation-focused-retry.log`/`.xml`; binary SHA-256
`ebd913de4bb8a9f4ff5d7f726876b02910ecf97df6e55180450cbf65869dd164`.
Independent final repair review finds no blocker; module/diff checks pass.
This certifies the bounded dependency slice, not full Polymorph, graphical
rendering, ordinary playable delivery or broad save/lifecycle acceptance.

### 2026-09-30 battle-form cast/result checkpoint — fixture admission

Both-target build `70446` passes. First focused native run `54234` executes
28 cases with zero skips: 25 pass and three new fixtures fail. Binary SHA-256
`4dc1d1030b365fcc46ab217fec65c3e3954710fb608dcffe8c8847fe505249f0`.
Preserve `UP066-cast-result-focused.log`/`.xml` and the successful build log.
Both cast fixtures fail before the effect executes because their synthetic
creature-category v2 snapshot omits mandatory explicit growth lines. Add valid
Ogre/Griffin growth rows; do not relax production validation. The early-result
fixture obtains no captured BattleResult: its base fixture publishes BattleStart
directly without creating the CBattleQuery required by endBattle. Add the normal
query lifecycle in the fixture without weakening species/Necromancy/gated-count
assertions or changing production admission. The direct survivor
projection, safe reversion and existing health/native-view guards pass. No spell
activation or playable promotion is claimed. Final retry evidence is pending.

Fixture repair rebuild `32737` passes both targets; native retry `13545` passes
28/28, zero skips, in 1.996 seconds. Binary SHA-256
`0bff72dc620bb2d687e2bd5badd1c6ef4ff15f437eab1d2342b3c1f28a32d2c7`.
Retain `UP066-cast-result-build-retry.log` and
`UP066-cast-result-focused-retry.log`/`.xml`. The repaired fixtures supply valid
v2 growth rows and the production CBattleQuery lifecycle; production validation
and all behavior assertions remain intact. Source/repair review and module/diff
checks pass. A non-blocking dangling-else warning around the fixture's GTest
assertion remains, alongside existing compiler warnings; no test failed or
skipped. This certifies the bounded cast/result foundation, not spell activation
or GUI/playable acceptance.

### 2026-09-30 battle-form foundation — first compiler probe

Both-target Linux compiler probe `21374` (twelve jobs) fails in
`CUnitState.cpp`: form JSON validation reads private
`CHealth::totalHealthOverride` at two sites. Preserve
`UP066-battle-form-build.log`; this was a probe during worker implementation,
not a final frozen candidate. Repair with a narrow read-only accessor or
validation method rather than widening state mutability. The succeeding final
build and focused native evidence remain pending. Polymorph is not activated,
counted or promoted by this checkpoint.

Retry `15032` reveals a second private-field access: the detached resolver's
environment fallback reads private `CUnitState::env`. Remove that unnecessary
fallback; nested detached unit/bearer links already unwrap to the concrete
stack. Preserve `UP066-battle-form-build-retry.log`. Final native/build evidence
is still pending; source review is not a compiler pass.

Build `71635` exposes two further compile-contract errors: the qualified
`evaluator.battle::CUnitState` assignment collides with CStack's `battle` member,
and `vstd::erase_if` assumes iterator erasure unsupported by `BonusList`.
Use an explicit base-reference assignment and `BonusList::remove_if` with its
native selector. Preserve `UP066-battle-form-build-final.log`; final success
remains pending.

Both-target build `53784` passes. The first focused native run executes 21
cases, zero skips: 19 pass and two new fixtures fail. Preserve
`UP066-battle-form-focused.log`/`.xml`. The health fixture expects a destroyed
temporary resurrection to add a second permanent casualty, contrary to the
existing source-species ledger; correct those three assertions to one. The AI
fixture calls `Bonus::addLimiter` on a stack-owned Bonus, which requires shared
ownership and throws `bad_weak_ptr`; initialize its public limiter before making
the shared export. Production mechanics are unchanged by these fixture repairs.
The succeeding rebuild/native retry remains pending.

Final cached both-target rebuild `85599` succeeds. Native retry executes all
21 cases successfully, zero skips; preserve
`UP066-battle-form-build-native-retry.log` and
`UP066-battle-form-focused-retry.log`/`.xml`. Existing Health/Shadow Gift guards,
real acquired-state native/JSON behavior and nested AI rank eligibility pass.
This validates the bounded foundation, not full Polymorph or playable delivery.

### 2026-09-30 Arcane Focus — first native selection fixture

Both Linux targets build successfully (`84362`, twelve jobs). The first isolated
focused run (`67682`) executes 27 cases without skips: 26 pass, including first
Sorrow, rejected/creature casts, mine-count parity, detached first/second spell
history and direct guards. Arcane Focus's actual AI-selection case expects
HERO_SPELL but the evaluator selects an Order (action type 0x0F). Preserve
`UP067-arcane-focus-build.log` and `UP067-arcane-focus-focused.log`/`.xml`.
Inspect the fixture's spell-versus-Order utility before classifying a production
defect; do not bypass selection or weaken the accepted-action assertion.
Coverage remains unadvanced until a succeeding focused run.

Repair: fixture Spell Power 10 made focused Magic Arrow only 46 damage and
allowed a sensible Order to win. Raise fixture Spell Power to 200 (548 damage
before Overcharge) while retaining real evaluator choice, accepted submission,
and live-state/RNG assertions. Cached both-target rebuild `55273` succeeds;
native retry `49925` passes all 27 without skips. Preserve
`UP067-arcane-focus-build-retry.log` and `UP067-arcane-focus-focused-retry.log`/
`.xml`. No AI production valuation was weakened to pass the fixture.
After the user-approved canonical Polymorph amendment, the offline source-hash
guard correctly fails until config sourceSha256 and generated module match.
Retain `UP067-arcane-focus-offline.log`; repaired retry passes 76/76 in
`UP067-arcane-focus-offline-retry.log`.

### 2026-09-30 clean Linux delivery — headless Mage Guild notification crash

Clean committed source `574f0571df32359a1b41bded8a8ccfddac8aa013`
builds successfully in Release with twelve parallel jobs. Its first frozen
All for One headless smoke exits 139 during a day-2 AI turn. Preserve the
private `committed-linux-574f0571d/build-client.log` and `headless-smoke.log`.
The candidate remains unselected. A corrected GDB replay reproduces SIGSEGV
at `GameEngine::windows()` from
`ApplyClientNetPackVisitor::visitSetNewHorizonsAdventureSpellUnlock`:
this notification dereferences ENGINE, which headless mode does not create.
Guard only the graphical refresh, preserving authoritative packet/state
application. Retain `headless-debug-fixed.log`; succeeding replay is pending.

The first debugger wrapper also produced a missing-Complete-data fatal dialog
by invoking the inferior outside the launcher's asset-link runtime directory.
This was a diagnostic setup error, not evidence of missing purchaser data.
The corrected wrapper keeps the inferior in that runtime directory and unsets
DISPLAY/WAYLAND_DISPLAY. Preserve `headless-debug.log` separately rather than
mistaking its normal exit for successful validation of the original crash.

After the guarded client rebuild, `headless-fixed-smoke.log` records nineteen
turn starts and multiple Mage Guild builds without that crash, then stalls on a
Necromancy reply. Full private runtime trace proves query 804 (Necromancy) was
covered by query 805 (Hero level-up); the valid lower reply was stored but ACKed
as failed, and CNecromancyQuery lacked an answered-query exposure continuation.
This is not passing promotion evidence. Repair only the Necromancy deferred
reply/continuation path, preserve ownership and index checks, and test covered,
invalid and foreign replies before repeating the frozen-candidate smoke.

The cached shared-tree native build fails in an unrelated unfinished Shield AI
fixture (`problem` undeclared). Preserve
`UP064-delivery-query-fix-native-build.log`. Do not repair or include that slice
in this delivery. Native query verification moves to the separate committed
checkout plus only the bounded delivery fixes, using the installed googletest
source; the candidate remains isolated from all unfinished Shield changes.

Succeeding clean both-target build passes. Four focused QueriesProcessor tests
pass, covering deferred success, exactly-once callback, invalid/foreign replies
and preserved generic-query behavior; independent review has no blocker.
Fixes are committed/pushed in `7e1a50ecc` and `41d44ee49`. Final committed client
rebuild and 35-second All for One smoke pass the bounded startup gate with 36
turn starts through day 12 and no forbidden errors. Preserve
`build-final-committed.log`, `query-fix-native.log`/`.xml` and
`headless-final-smoke.log`. The final run repeats `Stack ammo overuse` diagnostics;
retain that non-blocking AI forecast/integration finding for Phase 2 rather
than claim a warning-free full-game acceptance. Snapshot `a96183639bbc0dba…`
is selected for the normal launcher; manual graphical acceptance remains open.

### 2026-09-30 Shield activation inventory preflight

Continuation build `9744` succeeds for both Linux targets after adding the
missing `Problem.h` include to the Shield AI fixture. First focused invocation
used incorrect nested XDG paths and skipped twelve mechanic cases; retain
`UP063-shield-final-focused.log`/`.xml` as invalid gate evidence, not a pass.
Corrected isolated-profile run `53765` executes all thirteen without skips:
twelve pass, friendly protection fails because its ordinary shooter still kills
the target even after reduction (both HP-capped losses are 6000). Retain
`UP063-shield-final-retry-focused.log`/`.xml`; adjust only the intended threat
fixture to demonstrate preventable damage, preserving actual AI choice and
authoritative submission assertions. Coverage remains unadvanced until retry.

That initial lethal-volley interpretation is not established: retry2 still
reports identical raw 10000 damage after increasing target HP. Inspection finds
the helper retains the pre-cast target pointer while HypotheticBattle creates a
new detached bonus-bearing unit on mutation. Re-fetch the target after castEval
and assert its actual reduction bonus before estimating damage. The production
evaluator already fetches the target after mutation. Preserve both failed runs
rather than treating a larger fixture as a mechanic repair.

Final retry `41010` builds both targets. Corrected native `61362` passes 13/13,
zero skips, retaining real AI choice/submission and live-state/RNG assertions.
Keep `UP063-shield-final-retry3-focused.log`/`.xml`; binary SHA-256
`afce95331c80f72b18d79ccf9ea3a2ce72b1d516facd57d6cd857d72af7994ca`.
The ordinary 1000-Skeleton fixture is restored. Independent final review has
no blocker; full in-battle save/reload, broader forecasts and graphical delivery
remain explicitly separate gates.

The 0.15.0 feature checkpoint exposed two stale hard-coded 0.14.0 assertions
in the content/perk tests and two incorrectly named test-module invocations.
The assertions now compare the generated module to the single authoritative
product-version config. The corrected four-module gate passes 85/85 including
menu/title/version checks; generated module and diff gates pass. This is a
test/data contract correction, not another gameplay feature.

Initial both-target build `23934` fails in the new Shield Haste-legality fixture:
`SpellID::HASTE` is an enum, not a wrapper with `toSpell()`. Wrap it in `SpellID`
before accessing the spell. Retain `UP063-shield-initial-build.log`; retry the
same serialized build after this bounded test-only correction. No production
mechanic or validation requirement is weakened.

Build retry `12359` and final warning-cleanup build `20774` succeed. Initial
native `21434` completes 13 cases: 11 pass, two fail. Binary
`c3fc356de4320859a17668a1d5bfe4ad8355ac506c602e35b9fc6d7cc31f587b`;
retain `UP063-shield-initial-focused.log`/`.xml`. The Haste-legality preview ran
after Shield had spent the Hero Action: canBeCastAt first checks canBeCast.
Move that preview after the existing explicit round advance, while asserting
Shield remains active, preserving both casting-budget and non-immunity checks.
The actual AI friendly-protection choice returns false; investigate its signed
valuation and scenario rather than relaxing the requirement. Enemy choice,
no-threat decline and saved-version exclusion already pass. Not a completed
native checkpoint until the full corrected filter passes.

Isolated score debugger `9121` confirms all three original friendly-scenario
candidate scores are zero; its driver exits zero but the inferior test still
fails. Retain `UP063-shield-friendly-score-gdb.log`; this is diagnostic evidence,
not a passing test. Source mapping identifies the only incoming threat as a
Lich's spell-like Death Cloud: BattleAttackInfo marks spell-like shots nonphysical,
while the ability is nonMagical. Neither reduction channel is exercised by that
fixture. Replace the intended physical-shooter threat with an ordinary shooter
and establish actual projected mitigation, without requiring an unprofitable
cast or changing shared classification silently. Audit nonmagical spell-like
primary/splash damage classification in Phase 2; this is not permission to
declare every creature attack covered or encode a permanent Death Cloud exception.

UP-063 first offline gate runs 78 checks and fails the Paradox Shield inventory
row: runtime registry status was activated while the CSV still said Planned.
Update that one row to Active, retaining Not done art and the neutral fallback.
The unchanged focused command then passes 78/78; module and diff checks pass.
This is an inventory correction, not native or graphical acceptance.
Source review also catches a fixture sequencing hazard before native execution:
beginCombat already starts round one, so a second cast must advance one real
round rather than loop only while round zero. Repair the fixture and retain
actual two-round refresh/expiry assertions and null guards; do not weaken the
production Hero Action requirement.

### 2026-09-30 Berserk initial native compile

UP-062 build `58076` fails in BattleEvaluator: the new floating-point expected
action value was declared const, but the existing ownership conversion negates
it in place. Independent review identified the same compile blocker. Root
retains the float expectation and makes the local mutable; no gameplay rule is
relaxed. Preserve `UP062-berserk-foundation-initial-build.log`. Retry evidence
is pending. Phase 2 review also records that the multi-activation forecast
advances one tied attack branch while scoring immediate expected value, and
does not apply forced WALK in that broader turn loop. The bounded movement
selection and next-action caster estimate do not certify that wider integration.

Retry build `34487` compiles the runtime, client objects and shared callback
tests, then fails in the new AI fixture: `acquireState()` returns CUnitState,
not a bonus-bearing node, so its `addNewBonus` call is invalid. The AI worker
repairs only that fixture to use the supported bonus path, preserving the
stationary/no-reposition assertions. Preserve
`UP062-berserk-foundation-retry1-build.log`; succeeding build/native evidence
is pending. Do not describe this test setup error as a production crash.

Retry build `16036` links both targets; native `62554` passes the first seven
callback cases then crashes in the legacy shooter fixture. Isolated gdb `92206`
attributes the null dereference to the fixture's unconfigured `unitType()` in
the existing `Unit::isCatapult` check, not to a real creature or a v3 cast.
Preserve the initial focused log and `UP062-berserk-foundation-legacy-fixture-crash-gdb.log`.
The separate movement-only gdb check `57237` exits normally; it is not evidence
for the crash cause. Worker repairs the creature mock without changing production.
Isolated runtime/AI run `86507` passes all eight v3/activation/AI cases, including
seeded authoritative target selection, own-side movement and accepted/read-only
AI cast, but four historical profile fixtures fail before casting: their synthetic
downgrade retained v3-only Quicksand `selectedPlacement`. Root removes that field
only when constructing v1/v2 fixtures; no production schema is loosened.
Preserve `UP062-berserk-foundation-runtime-ai-focused.log`/`.xml`. Final retry
evidence remains pending; no complete 21-case pass is claimed yet.

Fixture retry build `86914` fails because `CreatureID::IMP` is an enum, not
the identifier wrapper exposing `toCreature()`. Root constructs
`CreatureID(CreatureID::IMP)` before the lookup. Preserve
`UP062-berserk-foundation-fixture-retry3-build.log`; no runtime rule changes.

Final build `27932` succeeds for both targets, followed by native `9314`:
21/21, zero skips, binary
`83356a9a474e1300cbe78c66616e4dd004ef9fcab7305d44310b813f8e1bdee3`.
Preserve `UP062-berserk-foundation-fixture-retry4-build.log` and
`UP062-berserk-foundation-final-focused.log`/`.xml`. The real creature type,
correct legacy fixture schema and supported detached-bonus path are verified.
Review's compile blocker is repaired; its broader forecast findings remain
explicitly deferred rather than represented as covered by this focused gate.

### 2026-09-30 Hand of Fate initial native compile

UP-057 build 2773 failed in BattleAI before linking: the new score branch
dereferenced scalar float effect multipliers, and SpellTargetsEvaluator lacked
the direct NewHorizonsSpellAvailability include for saved-roster admission.
Root removes the erroneous unary dereferences and adds direct includes in both
the evaluator and the runtime fixture (same helper consumer). No gameplay rule
is relaxed. Preserve `UP057-hand-of-fate-initial-build.log`; retry evidence is
pending. Review must distinguish algorithm inspection from compile evidence.

Retry build 28001 successfully links both client and test. Native 77437 passes
16/19, zero skips, on binary
`925142dcbef1c1220da968f69b7a9b892478b1ddff50a5c8c355c309d42ff632`.
All six new Hand of Fate runtime cases, eight current-profile Holy Wrath cases,
and two existing AI guards pass. The new AI scenario submits a Hero Order rather
than the asserted spell; worker performs a bounded valuation/fixture review,
without forcing production preference. Two historical Holy Wrath fixtures also
fail validation because they mechanically downgrade the current rules while
retaining Quicksand's v3 `selectedPlacement` field. This is a stale fixture,
not evidence that the Hand of Fate runtime corrupts saved rules. Record that
non-blocking fixture hardening for Phase 2 and preserve both failures in
`UP057-hand-of-fate-initial-focused.log`/`.xml`.

AI fixture retries 53527, admission trace and player-view trace still selected
an Order; their uniquely named retry2/retry3/retry4 build and focused logs remain
preserved. Read-only debugger inspection finally established actual candidate
scores: Hand of Fate 852.056/801.586, Hold the Line 1215. Admission, expected
valuation and accepted projection were working; the fixture incorrectly assumed
the spell must outrank a separate Order heuristic. Do not force a production
preference to satisfy such a test. Following the existing Holy Wrath pattern,
neutralize only competing Order coefficients in the submission fixture while
retaining EV, defenses, live-state/RNG and authoritative acceptance assertions.
Build 90769 and native 54761 succeed: 17/17, zero skips, binary
`3ac2c0c602cb277c228164144ff86b2d448257b45cac526294c72cccdd7c09fd`.
Preserve `UP057-hand-of-fate-isolated-ai-retry5-build.log` and
`UP057-hand-of-fate-isolated-ai-retry5-focused.log`/`.xml`. The two stale
historical Holy Wrath fixtures are explicitly excluded from this final gate,
not silently repaired or reported passing. Broad Order-versus-spell tactical
ranking and legacy Clone projection parity remain Phase 2 findings.

### 2026-09-30 canonical School snapshot fixture baseline

UP-058 client/test build 74940 succeeds and 75 offline gates pass. Native
97607 passes 3/4, zero skips, binary
`52d3548dfbf70f47c773dc7fbfdcfc5166a3084b2f55bb9f43c03d1427150097`.
Fresh Sorcery/Nature acquisition, shared saved-classification casting and
the updated Archmage AI submission pass. The old captured-Havoc round-trip
helper grants Advanced Havoc, then incorrectly retains its previous Ice Bolt
unranked assertion; actual rank is two. Repair that conditional fixture
baseline, not production School policy. Preserve
`NewHorizonsCanonicalSchools-initial-focused.log`/`.xml`; final evidence pending.

First succeeding School checkpoint: test rebuild 76420 and native 76202 succeed,
4/4, zero skips, on
`d492ad73bae4a628efb7f91dfdf49f18328722339a55bbebbbcff61d6b23f37f`.
The corrected conditional baseline preserves the old captured School/rank
assertions. Retain `NewHorizonsCanonicalSchools-retry1-focused.log`/`.xml`
alongside the failed first run. No production rule was relaxed for the fixture.

### 2026-09-30 Archmage fixture review corrections before native execution

Independent UP-055 review found two fixture errors before the first test build:
the rank-loss case retained the Expert Wisdom cost after demotion to Advanced,
and the Expert selection helper/offer case omitted Basic and Advanced perk
selections. Recompute the Advanced baseline and prepare legal prerequisite
tiers (Mysticism or Prepared Caster, then Deep Knowledge, then Archmage).
Do not relax production progression or rank gating. Client build 43488 runs
against frozen production bytes; fixture-only corrections remain permitted
before the later serialized test build. No native result is claimed yet.

Client 43488 and test 78105 build successfully. First native filter 20838
selects 20 runtime/AI cases, passes seven, then exits 139 in
`FirstHighSpellConsumesSharedLevelFourOrFiveGate`; remaining cases are not
executed and XML completion is not claimed. Frozen binary SHA-256:
`0edf16337d01d460f7e011277a5a4f60986ebd13a9443914aaabb283ad5bc329`.
Retain `NewHorizonsArchmage-initial-principal-focused.log`. Root traces the
single failure with gdb before attributing the crash to fixture or production;
no verified coverage increase or delivery claim is made.

The isolated gdb captures now establish a dead active stack: stack ID 0,
`DEFEND`, alive-only lookup null, and both active-unit and unfiltered stack
`alive()` false. The request path admitted that unit action before `StartAction`,
whose visitor dereferenced the null stack. Add authoritative live-stack rejection
before publication and cover it explicitly; do not merely hide the crash by
changing a fixture. The cross-round Archmage fixture also needs a surviving
attacker army after Chain Lightning. Preserve the initial trace and lifetime
logs. A subsequent debugger-invoked `battleIsFinished()` call itself signaled;
that is not independent gameplay evidence and must not be attributed to the
original crash. Repair/build/native evidence remains pending.

Crash-repair build 22317 succeeds for client and test targets. Native 62809
passes 19/21 with zero skips on
`ddc9d9ee75803636b28e91eed5e6afecff3f760ea9c79e2da7b4fe0763f08ee8`.
The original crash is gone and the stale-dead request regression passes.
Two principal checks remain failing: the next-round Armageddon request is
rejected, and the AI scenario selects Magic Arrow rather than its expected
Implosion. Preserve `NewHorizonsArchmage-crash-repair-focused.log`/`.xml`;
repair the fixtures from admission/ranking evidence, without forcing production
legality or AI preference to satisfy their assumptions. No coverage increase yet.

Fixture tracing confirms that Armageddon requires the client no-location
request, not `aimToUnit`; the Initiative-4 attacker/defender tie also left
ambiguous turn ownership. Use a durable fast attacker, assert attacker control,
and submit the existing invalid-hex global request. The AI's SP 9900 scenario
let overcharged Magic Arrow reach the same capped enemy-health score as
Implosion. SP 100 retains Magic Arrow and Order competition without that cap
tie. These are fixture-only corrections; casting admission and AI valuation
remain unchanged. Three direct Time Stop/Pursuit guards pass on the repaired
production binary. Preserve the failing reports; retry evidence is pending.

Fixture-only build 26076 succeeds. Native 38685 passes 23/24, zero skips,
on `cbb9c5e3a029d73f48fc2b233865ed7043e0d8e648d5855b36f902dabbd3d2de`.
All runtime, save-mask and direct guard cases now pass, including accepted
next-round Armageddon. The AI at SP 100 legitimately prefers an Order for its
100-Angel army; the fixture still does not establish its asserted Level 4
submission. Preserve `NewHorizonsArchmage-final-focused.log`/`.xml` despite its
name; it is a failed attempt, not final acceptance. Use a scenario where the
spell offers greater actual value than the competing Orders without changing
production evaluation, then rerun the focused gate.

Build 21018 succeeds, but native 6567 still passes only 23/24 on
`18cc920669c1fd412551ac39a455739186a470be7167c6db765f0446602935e1`:
one allied Angel alone does not remove the Order preference. A bounded gdb
inspection establishes actual scores: Implosion 303.3685, Hold the Line
454.6875, Riposte 204.6875, strongest Magic Arrow 50.1675. The no-cast baseline
is -45.3125. Preserve `NewHorizonsArchmage-scenario-retry3-focused.log`/`.xml`
and `NewHorizonsArchmage-ai-values-trace.log`. Use SP 2500, where legacy
Implosion can defeat the 20,000-HP target and overcharged Magic Arrow cannot.
This proves the shared discount hook against the current saved Level 4 spell,
not canonical Implosion's still-incomplete formula/School mechanics.

First succeeding checkpoint: test-only build 13005 succeeds; native 18779
passes 24/24, zero skips, binary
`8e2222981acf90654d38166321ece81af245860deaf55a38680af0cc10dece52`.
The accepted Level 4 evaluator submission now passes alongside all runtime,
serialization and direct lifecycle guards. Reports
`NewHorizonsArchmage-score-retry4-focused.log`/`.xml` are retained separately
from every failed attempt. Client production already built successfully in
22317. Remaining broad result-query/automatic-action lifecycle interactions
are deferred; no gameplay or graphical acceptance is inferred.

### 2026-09-30 Arcane Memory Tome fixture constness repair

Initial focused test build 22635 fails in the new
`BookAndTomeSpellSourcesLeaveMatchingScrollsUnused` case at
`NewHorizonsMagicStateTest.cpp:762`: `getArt` returns a read-only artifact,
so attaching a test-only matching School bonus directly discards constness.
Use the authoritative map's mutable `getArtifactInstance` lookup for that
same instance ID. Do not weaken production constness or change shipped Tome
school data to accommodate a synthetic source-priority fixture. Preserve
`ArcaneMemory-initial-test-build.log`; repaired build/native evidence is
pending. The repaired production client build 57887 already succeeds.

Independent review also found and repaired the initial adventure source-scan
ordering defect before native verification: Town Portal may teach its own
spell at a destination Guild before completion. Capture the source before
effects, settle the exact instance only after success, and require discharge
before Arcane Memory learning. The focused Guild/query regression remains
pending, not implicitly established by source review or compilation.

Test retry 28356 succeeds. First native run 94687 executes the four new cases
plus two direct saved-profile/adventure guards: 4/6 pass, zero skips, binary
`49c29fdd1feeba5b216e9092e6ae21d153187ac5d5af609f9c46044b9cf8d42c`.
Reports `NewHorizonsArcaneMemory-initial-focused.log`/`.xml` are preserved.
The accepted scroll neither learned nor disappeared; the priority fixture's
second Haste action was rejected. Source tracing identifies a root design
assumption error: ordinary scrolls are reusable, with no charge cost, while
Arcane Memory's canonical rule requires learning from an accepted scroll cast
and says nothing about consuming it. The helper wrongly treated such a scroll
as a permanent non-scroll source and returned without learning. Correct source
classification and retain the scroll; preserve discharge only for actually
charged artifacts. Repair fixture consumption assertions, investigate the
Haste rejection, and keep this run's evidence rather than relaxing legality.
Native/build retry evidence remains pending.

Reusable-scroll client retry 56261 succeeds. Test retry 4749 fails in the
Town Portal fixture because `MAGIC_SCHOOL_LEVEL` is not a Bonus type; use the
existing `MAGIC_SCHOOL_SKILL` bonus. Preserve
`ArcaneMemory-reusable-test-retry2-build.log`. Review additionally identifies
an impossible fixture expectation: current casting rules reject an unknown
scroll spell below the required School rank, while a map ban does not remove
direct scroll sources. Correct those assertions without changing casting
policy. Adventure Mana/day/retained-scroll assertions alone do not prove the
new completion hook; add a small forwarding counting server environment to
exercise actual success, effects failure, pending query and cancellation.
Neutral Adventure permanent acquisition remains a separate policy question.

Test retry 37686 succeeds. Native retry 2974 runs nine new cases and two guards:
8/11 pass, zero skips, binary
`9b5402487c70305226128901da8d5b125e12e594dcc60d1c41bce945118724d9`.
Preserve `NewHorizonsArcaneMemory-reusable-retry1-focused.log`/`.xml`.
The remaining three failures share scroll source identity: `CMap::createArtifact`
stores a scroll SPELL bonus with `ARTIFACT_INSTANCE` source but a typed
`ArtifactID::SPELL_SCROLL` source ID, not the scroll's actual instance ID.
The shared getter returns that obsolete type token, so settlement can resolve
unrelated gear or no artifact. Normalize only that precise legacy scroll source
in `CGHeroInstance::getSourcesForSpell` to matching equipped scroll instance
IDs, deduplicated. Preserve other bonus sources and backpack exclusion; do not
rewrite serialized bonuses or only fix newly created scrolls. Existing tests'
explicit instance-ID assertions are valid regressions, not assertions to relax.
The bounded getter repair builds both client and test targets (4257).
Native retry 53830 passes 11/11, zero skips, binary
`7efa81f126a237daaf9a48bb0e47382b1de7aa9e26036a8e2de86df7f272b9bc`.
Retain `NewHorizonsArcaneMemory-provenance-retry2-focused.log`/`.xml`.
The first failed runs remain above. The generic Town Portal completion case
uses a forced legacy selection branch, not canonical nearest-town acceptance.
Neutral Adventure acquisition policy remains unanswered; production activation
is withheld, with positive feature cases explicitly enabling test-only settings.
The final dormant-registry build 89441 succeeds; native 18008 passes 11/11,
zero skips, on `1928e181a8d676e4e9fcf8a0c5dceff75c7c8c0f194de4d963fb6f1f1d7f7002`.
Retain `NewHorizonsArcaneMemory-dormant-policy-final-focused.log`/`.xml`.
74 offline cases and mirror/diff checks pass. Production stays planned;
the positive fixture override does not alter other perk entries. Independent
review finds no blocker. This closes the focused repair, not the unresolved
acquisition choice, broader interactions or playable acceptance.

### 2026-09-30 Deep Knowledge probability saturation review

Independent source review found a valid-input regression before native runs:
saved growth rules permit a 100% Wisdom opportunity, but adding ten without a
ceiling produces 110%, rejected by `calculatePrimaryGrowth`. Saturate the
adjusted existing chance at 100%; canonical Advanced 30% and Expert 40% remain
unchanged, as do row order and draw count. Add a focused 100%-row guard. Initial
client build 43182 passed compilation but does not establish this boundary;
the repaired candidate requires its own build/native evidence.

Repaired client 98557 and test build 23322 pass. The first isolated native run
77666 passes 12/13, zero skips, on binary
`f89f7f0b98ad019d9ee9c6847c5ccbfc7d24911663e46bd9d6b7f0ffd715f6a6`.
The saved-selection fixture assumes the first selected perk is Wisdom, but
real Solmyr already has an authored Havoc selection. Search for the legally
selected Wisdom identity instead of assuming vector position; preserve the
planned captured-state assertions and all production behavior. Reports
`NewHorizonsDeepKnowledge-growth-guards.log`/`.xml` are retained. The actual
query timing, exact 30/40% boundaries and 100% authoritative roll already pass.
Rebuild and use a new report name for the corrected fixture.

Test retry 8638 passes; native retry 96534 passes 13/13, zero skips, on binary
`c9b9cdfd304e9b9db3ef679da3d3d13352a73af892f519d88084d97c3dccac88`.
The four new cases now all pass, including the previously interrupted captured
planned-state guard. Reports are `NewHorizonsDeepKnowledge-retry1-growth-guards`
log/XML; first reports remain preserved. Independent review accepts the
identity lookup fix and finds no remaining blocker.

### 2026-09-30 Prepared Caster creature fixture identifier repair

The first Linux client/test build (session 91012) stops in the new Prepared
Caster runtime fixture. `BonusSubtypeID` is a variant of typed identifiers;
the bare `SpellID::MAGIC_ARROW` enum cannot construct its `SpellID` alternative.
Wrap it as `SpellID(SpellID::MAGIC_ARROW)`. The related enum `.toSpell()` calls
were caught and corrected during review before this build, but the subtype
constructor requires the same explicit typing. Do not change production
discount or Bonus types to satisfy a fixture. The shared library already links;
the remaining client/test build and native gates are pending.

Retry build 9949 reveals a second fixture compile error: `SideInBattle` inherits
an explicit callback-holder constructor and has no default constructor.
Initialize these pointer-free serialization fixtures with `nullptr`, following
existing SideInBattle wire tests, instead of adding a production default
constructor. Preserve `PreparedCaster-build-retry1.log`; the next build uses a
new log name. No production assertion or compatibility contract is relaxed.

Retry build 55809 succeeds for both targets. The first focused native run
26183 passes 9/10, zero skips, on binary
`64625e7ebf3e0b4dc5ea942423d4497aa7f6bd46c61735970136618002992ca8`.
The first-cast fixture assumes combat starts at round zero; this fixture starts
at round one, so `endRound()` correctly advances to two. Assert an increment
relative to the captured first round, retaining the later accepted-cast/Mana
assertions. All four projection/actual-AI cases and the other five runtime
cases pass. Preserve `NewHorizonsPreparedCaster-focused.*` and use a new retry
report. No production timing or cost is tuned to satisfy the fixture.

The final test-only rebuild 56500 succeeds. Native retry 48019 runs 31 cases
on binary `ea8e481c14da2395f288408bece79ae470d0d2ad9534398c57e1bb37f5e815a0`:
all ten new Prepared Caster cases and 20 of 21 existing guards pass, zero skips.
The remaining existing `WisdomDiscountsMagicArrowBaseButNotOverchargeSurcharge`
fixture fails during setup, before casting: its `savedFormula()` changes live
v3 data to ruleset v2 without removing Quicksand's v3-only `selectedPlacement`.
The unchanged validator correctly rejects that synthetic snapshot. This helper
was not modified by Prepared Caster; record its migration repair for Phase 2,
not a production cost change or reason to hold the Phase 1 coverage loop.
Preserve `NewHorizonsPreparedCaster-retry1-guards.*`; run the new ten-case
filter independently for a clean principal-path result without hiding the guard
failure. Adventure exclusion is source-reviewed but lacks a dedicated new
Prepared Caster native case. Older saves intentionally load completion false.

The independent principal filter 78487 passes 10/10, zero skips, on that same
final binary. Reports are `NewHorizonsPreparedCaster-retry2-principal.*`.
Do not describe the earlier 31-case guard run as green or overwrite its report.

### 2026-09-30 Mysticism registration description alignment

The first 74-case offline gate fails its canonical-description comparison after
the registry description was expanded without a matching specification edit.
Keeping canonical perk text but only expanding effect text then fails the
required description/effect-description equality; the apparent 301-perk count
is a cascading early-loop assertion, not missing registry entries. Restore both
texts to the canonical wording and retain Normal/Buffer semantics in runtime
and focused tests. The third invocation passes 74/74. Do not weaken the
canonical registry guard to accommodate an implementation-only clarification.

The first isolated six-case native run passes 2/6, with zero skips, on binary
`d63df8df340b09b4749b71b548c22b13bfb55d254beb01b5262b0c195cdc931e`.
Actual day-start packet application and Mage Guild precedence pass. Four
fixtures omit the existing universal +1 daily regeneration: inactive recovery
is 1, not 0, and an additional +25 regeneration source totals 26. Correct those
expectations and assert the baseline explicitly; no production tuning. Preserve
`NewHorizonsMysticism-focused.*` and use a new retry report.

Final test build 92352 passes; the isolated retry with directly relevant
capacity/pool guards passes 36/36 (six Mysticism cases), zero skips, on binary
`0b565de19b837c0f2a3bc476c59009f61c99f570ba15c507d3737df7912be080`.
Passing reports are `NewHorizonsMysticism-retry1-capacity-guards.*`; the initial
failure reports remain. Client build 55027 also passes. No production value
was tuned to satisfy the fixtures; independent review confirms the baseline
corrections and has no blocking finding.

### 2026-09-30 Combined Arms AI callback type repair

Linux build session 90955 fails at `BattleEvaluator.cpp:3935`: `getBattle`
returns a `shared_ptr<CPlayerBattleCallback>`, not a raw pointer, so
`const auto *` cannot deduce its type. Keep the returned shared pointer with
`const auto`; member access and reference consumers retain their existing
semantics. This is a compile failure, not gameplay evidence. The succeeding
build/native gate remains pending; do not count activation as execution proof.

The repaired client/test build passes (3217, then test-only 89505). The first
Combined Arms native run passes 4/5 with zero skips on binary
`7a4318405af9da53d491281cbdee27d24c54d3bd605ab6a9b84dafdeb663b4fe`.
All three runtime cases and melee-only Focus Fire actual-AI/fractional endpoint
pass; the Flank fixture expects Flank but the evaluator selects Hold the Line.
That scenario contains a large melee ally and normal competing Order scores;
this is not evidence that Flank's runtime effect is broken. Isolate the bounded
Order consumer fixture, retaining legal Magic Arrow competition; do not tune
production ranking to satisfy a forced expectation. Preserve the first
`NewHorizonsCombinedArms-focused.*` reports and use a new retry filename.

The direct guard run passes 36/37 with zero skips. The old Target Caller fixture
expects 185 damage but receives 195: selecting Basic NH Archery legitimately
adds its already implemented +10% rank premium (`archeryDamagePercent(rank)`),
in addition to the fixture's 50%, Focus Fire's 30%, and Target Caller's 5%.
Repair the stale expected value to 195 and assert the pre-Order baseline of
160, so the independent 35-point Order/Target Caller contribution remains
checked. No production rule is changed. Preserve
`NewHorizonsCombinedArms-guards.*` and use a new retry report.

The final test-only build (11676) succeeds. The isolated retry passes 5/5
Combined Arms cases and 37/37 direct guards, zero skips, on binary
`9100b7e059bbe7822bc1b8df362b444a95e660f672ea40864ce35dc65bac2a5d`.
Reports are `NewHorizonsCombinedArms-retry1.*` and
`NewHorizonsCombinedArms-guards-retry1.*`; the initial failing reports remain.
Both actual-AI fixtures isolate nonselected Order coefficients, retain legal
Magic Arrow, and use melee-only/shooter-only armies respectively. Production
AI scores are unchanged by these fixture repairs; full ranking remains Phase 2.

### 2026-09-30 Command efficiency fixture compile repair

Linux build session 67452 stops in the new AI fixture: `MAGIC_ARROW` is an
enum constant, not a `SpellID` object, so `.toSpell()` cannot be called on it.
Construct `SpellID(SpellID::MAGIC_ARROW)` before lookup. Production coefficient
and runtime fixtures compile; no gameplay failure is inferred. Preserve the
focused build/native gate rather than counting registry activation as proof.
The repaired build (session 52410) links both client/test targets. The isolated
`NewHorizonsCommandEfficiency-focused.*` run passes 11/11 with zero skips on
binary `e228836aac3189c35ed63b803c2facd57ec22b5c006161537054ee860a13a2a3`.
The first offline invocation also named a nonexistent inventory test module;
rerunning the actual `test_new_horizons_ui_perk_inventory` module alongside
content/perk checks passes 74/74. Neither invocation failure is hidden.

### 2026-09-30 Blink native fixture compilation and tie-order review

The Linux client build passes, but the first test-target build (session 35688)
fails in `NewHorizonsChaosBlinkAITest.cpp`: `const auto * liveRng` makes the
pointed-to generator const, while its saving serializer requires a nonconst
reference. Keep the original mutable pointer returned by the game handler;
saving the RNG state does not advance it. No gameplay failure is inferred.
Independent review also catches an incorrect distribution assertion: the
equal-distance endpoints have IDs `{59,127,95}`, so lower-hex tie selection
gives weights `{5,1,3}` in the fixture's supplied vector order, not `{5,3,1}`.
Repair the fixture rather than changing correct production tie-breaking.
The first succeeding test-target/native gate remains pending.

The repaired test target builds (session 43106), but the first isolated Blink
run fails 5/11 with zero skips (binary SHA-256
`7c4a4e8b4772faa5e12e979347d04160a69c2c6eb61cf668015f6ae1f699db81`).
The footprint fixture incorrectly uses the single-wide Angel as a double-wide
unit; use the real Centaur footprint and assert it. Both 100%-resistance cast
fixtures are rejected, whereas hostile Mirror relocation succeeds: investigate
pre-cost immunity/resistance admission before changing casting rules. Both
actual AI submission fixtures find legal targets but decline to submit Blink;
trace candidate score/allowance/Order selection rather than claiming passing
AI support or disabling competing Orders. Logs/XML remain under the isolated
runner as `NewHorizonsChaosBlink-focused.*`. Runtime and AI owners are
diagnosing separately; no passing execution gate is claimed.

The resistance cause is the default `ResistanceCondition` admission predicate:
negative magic at 100% Resistance is non-receptive before cost. That ordinary
hostile behavior remains unchanged. Friendly saved-v3 Blink now skips only that
default Resistance predicate while retaining absolute, elemental, configured
immunity and Spell Lock checks. Add an explicit absolute-immunity guard, fix
the double-wide fixture to Centaur and correct hostile 100% Resistance to
expect rejection without cost. The AI owner adds temporary candidate/allowance/
winner diagnostics for a bounded two-case rerun; remove them before delivery.

Build session 30535 links both targets. Retry2 runs 12 cases: all ten runtime/
rule cases pass, both AI cases fail, zero skips (binary `8063bf25d8b5087f7f2995536d49e066c98c58ace8351a79825b2ccb39ce6d21`).
Candidate diagnostics show legal positive Blink values, but evaluation never
reaches allowance or winner scoring. Inspection identifies the earlier
forecasted-battle-finish gate: the large friendly Pikeman can already walk to
and kill the only Peasant enemy before the enemy acts. The fixture immobilized
the active Golem and enemy, not that Pikeman. Immobilize the friendly target
too so Blink creates an actual melee opportunity while ordinary movement does
not. Keep all Orders and the legitimate no-need-to-cast production gate.
This diagnosis needs the repaired actual-submission rerun; remove diagnostics
before final build and preserve `NewHorizonsChaosBlink-retry2.*` evidence.

The diagnostic-free repaired build (session 35699) passes both targets.
`NewHorizonsChaosBlink-final-focused.*` still reports 10/12 passing, zero
skips, but now both AI cases actually submit a Hero action: an Order rather
than Blink. The battle-finish precondition is repaired; the competing action
ranking must be inspected before modifying the fixture again. Preserve the
strict `HERO_SPELL` assertion and legal Orders; do not count ordinary Order
submission as Blink coverage or inflate its production score just to pass.

The bounded ranking run (`NewHorizonsChaosBlink-ai-ranking.*`, session 89021
test build) confirms baseline 0 and two positive Blink candidates at about
1.21–1.53. Existing Order heuristics score Hold the Line 1.7, Riposte 6.2 and
Flank 8.8, so Flank wins. Keep that evidence rather than interpreting this
fixture as missing Blink registration or failed legal targeting. The next
fixture must give relocation a genuinely favorable tactical opportunity;
production spell/Order tuning is not a test-fix shortcut.

The Grand Elf/Arch Devil escape-and-shoot fixture also fails both actual AI
submission assertions after clean full build session 10235. The isolated
`NewHorizonsChaosBlink-final-retry2.*` run passes 10/12, zero skips, on binary
`b59553d04e8f2984ba9b7049258e09100739980bdd4454b55057666cc17d4f8e`;
both remaining actions are still Orders. Runtime coverage passes, but actual
Blink AI submission remains unverified. Reassign the bounded ranking/fixture
diagnosis to the AI owner; retain legal Order competition and strict spell
submission assertions. Do not increment the verified coverage snapshot yet.

The bounded `NewHorizonsChaosBlink-ai-ranking2.*` diagnostic (test build
session 98001) confirms that shooter escape is valued: Blink scores about
17.41 normally and 19.15 with Blinkmaster. Hold the Line scores 35 and wins;
Riposte scores 17.5. Detached shooting is not lost to the old occupied hex.
Inspect the inherited hero command attributes and use an ordinary low-command
magic specialist fixture if appropriate; do not boost the spell score or
disable competing actions. Temporary diagnostics must again be removed.

The clean 5/5 Attack/Defense fixture build (session 29935) links both targets,
but `NewHorizonsChaosBlink-final-retry3.*` remains 10/12 passing, zero skips
(binary `645766143b3f16ad19b9a703ee16ce77542d616593d6fc01bf9f863ddc743267`).
The base battle fixture already resets attributes to zero, so 5/5 does not
reduce its inherited command strength. The next bounded adjustment increases
the shooter's army size to create a materially stronger relocation opportunity,
without changing production valuation or disabling Orders. Preserve the failed
submission evidence and rerun before declaring completion.

First complete succeeding gate: test build session 90047 succeeds after the
fixture-only shooter-count change; production source is unchanged from the
successful clean client/test build 29935. Final binary
`d5e161815ab6ab3ce33c82a43b9de49ebe5962d9fb7f32f071df1950653baa68`
passes `NewHorizonsChaosBlink-final-retry4.*` 12/12 and its focused immunity/
Entangle guards 24/24, zero skips. Actual AI spell submission, live-state/RNG
immutability and authoritative legal landings are now verified. All ordinary
Orders remain enabled; no production score inflation or assertion weakening.
Temporary diagnostics are absent. Broader tactical quality and cross-system
interactions remain Phase 2 rather than expanding this focused gate.

### 2026-09-29 Vengeful Vines content coefficient encoding

The initial 51-case content check failed two schema validations because
`powerCoefficient: 1.1` is not an integer in the saved direct-damage model.
This model already divides raw Spell Power by 10; coefficient 11 therefore
implements the canonical 1.1 multiplier without a new schema or precision path.
Correct the data and add an explicit encoding assertion. The repeated content
suite passes 51/51, exit 0. Native formula execution remains a separate gate.

Independent pre-build review found a core UI mismatch: hovering another
orientation changed the displayed six-hex footprint, while Enter submitted
the stored orientation. Keep preview/status on the committed orientation;
hover may highlight a rotation control but changes the path only after click
or Rotate. The focused source guard passes after repair, and independent
final-source review has no remaining blocker. Both Linux client/test targets
then compiled and linked on the first native build attempt, exit 0. The
focused runtime filter and rendered keyboard acceptance are separate gates.

First focused native run: 13 cases, 10 passed, three failed, zero skips,
exit 1. Two actual server casts reduced Initiative from 4 to 2 alongside
Speed, contradicting the canonical separation. Investigate the Initiative
fallback rather than weakening those assertions. The AI fixture's legacy
`CSpell::calculateDamage` call returned 1109 instead of the saved-rules 130;
use the canonical BattleSpellMechanics forecast and retain actual projected
versus resolved damage assertions. These failures block this slice's commit
until repaired and the complete focused filter passes.

Cause/repair: creatures without explicit Initiative retain the classic
Speed-based fallback, so a STACKS_SPEED penalty also changed Initiative.
Vines now uses the existing STACKS_MOVEMENT_RANGE additive bonus, already
defined to affect movement only; retain classic fallback behavior unchanged.
Creature UI's Speed readout uses getMovementRange and therefore displays the
penalty. Native fixtures check that accessor alongside unchanged Initiative.
The AI fixture removes the legacy calculation shortcut but retains real
hypothetical damage and authoritative-resolution parity assertions.

First retry: 13 cases, 12 passed, zero skips, exit 1. Movement/Initiative
separation now passes. The AI projection still wipes both enemy stacks instead
of dealing 130. The fixture set `useCommands=false`, which explicitly disables
New Horizons primary profiles in HeroCommandFixture; the hero consequently
uses divisor 1, not the canonical divisor 10, and forecasts 1120 damage.
Restore the active primary/command configuration and assert divisor 10 plus
the saved-rule effect value before projection. This is a fixture configuration
error, not evidence of duplicate hits or a production projection failure.

First complete succeeding native gate: both Linux targets link after the
bounded repairs; the isolated New Horizons active-profile retry2 filter passes
13/13, zero skips, exit 0. Actual AI selection/submission, hypothetical damage
and authoritative resolution match. The 51-case content suite, two perk
inventory tests, UI source guard, module mirror and diff checks pass.
Independent review finds no remaining blocker. Full save/load, combined
movement effects and rendered keyboard acceptance remain outside this gate.

### 2026-09-29 Entangle pre-build review — classic lookup and stale rules

Independent review found two blocking source defects before native execution.
The real and hypothetical move paths decoded a module-scoped spell on every
position change; decoding throws if New Horizons is not loaded. Use exact,
silent optional identifier lookup and do nothing when the spell is absent.
The authoritative cast admission also accepted a v2 roster that retained
Entangle while Lua discarded the unsupported effect. Reject that request
before spending Mana or actions. Focused regression evidence and the first
succeeding target build remain pending. These findings are not observed
playable crashes and must not be described as passing native tests.

The first local Entangle build over parent `326fd24ad` stopped in both move
helpers: `CIdentifierStorage` was only forward-declared by `GameLibrary.h`.
Include `modding/IdentifierStorage.h` directly in each translation unit.
The other changed runtime, AI and client objects compiled; the full target
and native execution gates were still pending at this failure.

The retry compiled the repaired production helpers and Entangle AI/UI tests,
but stopped in the server fixture. Its direct `CGameHandler` calls need the
declaring header, and `BonusSubtypeID` requires a `SpellID` object rather than
the `SpellID::MAGIC_ARROW` enum constant. Add the direct include and typed
identifier; rebuild before claiming native verification.

The next fixture compilation found `.empty()` on a shared bonus-list pointer
in the adjacent-attack assertion. Use `->empty()`. This is a fixture API
error, not evidence of production root behavior; full target success remains
pending until the retry finishes.

First succeeding target gate: the incremental Linux `vcmitest` and
`vcmiclient` build over parent `326fd24ad` plus the Entangle slice completed
with exit 0 after these repairs. Focused native execution remains a separate
gate; no GUI, CI or promoted-snapshot success is inferred.

First active-profile Entangle execution exited 1: 17 cases, six passed,
11 failed, zero skips. Nine server-fixture casts were rejected despite
target-legality checks passing; inspect fixture turn ownership before
changing production validation. Both AI cases used the nonexistent
case-sensitive creature key `core:hellhound`; the real key is
`core:hellHound`. Four status and two legacy-roster cases passed. These
partial results do not establish a passing native gate; rebuild repaired
fixtures and rerun the entire focused filter.

The rejected v3 casts were fixture turn ownership: the defender Phoenix
acted first, while the fixture attempted an attacker hero cast. Advance via
bounded legal Defend actions until the requested side is active, then submit
the real hero action. Legacy rejection checks now use that valid turn and
cast allowed Haste afterward to prove the rejected request preserved its
Hero Action. The AI creature key is corrected. Rerun evidence is pending.

The second active-profile run exited 1: 17 cases, 13 passed, four failed,
zero skips. Actual AI choice/submission now passes. The remaining fixture
assumptions need correction: Echoed Duration applies to a Metamagic follow-up,
not an ordinary cast; the shooter was adjacent to an enemy (ordinary shot
blocking); Teleport needs a destination as its second target; and recasting
must occur after a real round's Hero Action reset. Inspect and repair those
preconditions without weakening production rules, then rerun the full filter.

Final repaired gate: both Linux targets link and the complete isolated
`NewHorizonsEntangle*` filter passes 17/17, zero skips, exit 0. All four
fixture corrections now exercise the intended execution paths, including a
live root broken by actual Teleport and an actual Metamagic follow-up.
Content passes 50/50, perk inventory 2/2, status wiring 4/4, module mirror
and diff checks pass. No rendered, CI or promoted-snapshot result is claimed.

### 2026-09-29 Crusade development build — incomplete battle-info type

The local incremental Linux client/test build failed in
`lib/battle/NewHorizonsPlague.cpp:133–134`: the new fractional-reduction
flag accessed `IBattleInfo::getMagicRules()` with only a forward declaration
available. This is a compile blocker, not a gameplay failure. Repair by
including the declaring `IBattleState.h` directly and checking other updated
`adjustRawDamage` callers for the same dependency. At this failure, rebuild
success was pending; passing offline checks did not prove compilation.

The next retry passed the repaired Plague/SoulChain files, then failed in
`AI/BattleAI/BattleEvaluator.cpp`: the new Crusade scoring block called
`.get()` on `unit`, which is already a `const battle::Unit *`. Pass that
pointer directly in the three new calls. Compile the actual evaluator and
focused native AI test; whitespace checks alone do not establish API/type
correctness. Full target-build success was still pending at that retry.

The following retry compiled the client and runtime, but stopped in the new
Crusade fixture: `SpellID::MAGIC_ARROW` is an enum constant, not a `SpellID`
object, so `.toSpell()` requires `SpellID(SpellID::MAGIC_ARROW)`. Repair the
fixture, rebuild the actual test target, and run the active-profile filter;
this is a test compile failure, not evidence of spell damage misbehavior.

The next retry passed the repaired server fixture and stopped in
`test/battleAI/NewHorizonsCrusadeAITest.cpp`: its `HypotheticBattle.h` include
used the wrong subsystem directory. Use the existing BattleAI header and
compile the fixture itself before counting native AI evidence. Full target
success was still pending at that retry.

Compiling that corrected fixture then exposed a missing direct include for
`newHorizonsMagic::spellAllowedBySavedRoster`. Its declaration belongs to
`NewHorizonsSpellAvailability.h`, not `NewHorizonsMagic.h`. Include the
declaring header explicitly rather than depending on an incidental transitive
include.

First succeeding local target gate: the final incremental
`cmake --build build/new-horizons-linux --target vcmitest vcmiclient -j8`
completed with exit 0 after all these repairs, linking both targets over
parent `997388ccd` plus this Crusade working-tree slice. Native execution is
a separate gate and remained pending at build completion. No CI or playable
snapshot success is inferred from this local build.

The first active-profile Crusade run exposed two fixture errors. Pikeman
Morale already included +1, so adding -2 produced -1 rather than the assumed
-2; use a controlled strong negative precondition. Recast immediately
refreshed the effect to three turns correctly, but the test assumed an extra
grace round after a round-two recast. Actual later durations were two, one,
then expired. Its next unchecked null bonus dereference caused exit 139.
Correct the round-boundary assertions and guard bonus presence before reading
it. This failure must not be reported as a production Crusade crash or a
passing native gate. Rerun the complete focused filter after repair.

Before rerunning, source review also corrected the AI fixture's magical
reduction lookup to explicit `BonusSubtypeID(SpellSchool::ANY)`. A default
`BonusSubtypeID` stores a different variant alternative and is not equal to
that school subtype; integer coincidence is not identifier equality. This
was a fixture correction, not a production reduction change.

Final repaired gate: both Linux targets link and the complete isolated
`NewHorizonsCrusade*` filter passes 19/19 with zero skips and exit 0. This
includes refresh/expiry, negative-Morale protection, both Echoed Duration
combinations, and actual BattleAI submission/casting. The 49-case content
suite, two perk-inventory checks, two UI wiring checks, module mirror and
diff checks pass. No rendered, CI, or promoted-snapshot result is claimed.

User requested durable notes after successive Windows release failures. New agents
must read this with [NH_DELIVERY_PIPELINE.md](NH_DELIVERY_PIPELINE.md) before
changing packaging or dispatching CI. This is an incident/regression index, not a
substitute for current [NH_BUILD_HANDOFF.md](NH_BUILD_HANDOFF.md) and independent
[NH_WINDOWS_ACCEPTANCE.md](NH_WINDOWS_ACCEPTANCE.md) evidence.

Different errors appearing in successive runs are not necessarily recurrences of
the same bug. They show incomplete end-to-end coverage and serial discovery of
prerequisites. A successful fix proves only its tested scope. A regression test
listed below is a coverage location, not a claim that the latest CI passed it.

## Growth release follow-up

### September 29 local Sorcery Slow native-gate incidents

- The first combined Linux build stopped in the new Slow test file because its
  bonus reader accepted `CStack *` while a hypothetical battle returns the
  shared `battle::Unit *` interface. Widening the test helper to `Unit` let
  `vcmitest` and `vcmiclient` link; no production object failed compilation.
- The first active-profile preview assertions read a pre-cast pointer after
  hypothetical battle copy-on-write, then assumed Slow always costs four
  Mana. Refetching the projected unit by ID and reading the hero's actual
  Wisdom-adjusted cost repaired the fixtures. The final focused Slow and
  Temporal Field filter passes 19/19, zero skips, including help and v1/v2
  fallback checks.
- A deliberately broad `*Slow*` filter also ran four unrelated existing
  cases that fail under current rules: two old Ice Bolt test-rule builders
  leave the `selectedPlacement` field in v1/v2, and two AI Temporal Field
  fixtures attempt a perk selection without its earlier tier. These are
  recorded for Phase 2; they are not green and are not counted in the focused
  Slow result.

### September 29 local Life Drain native-gate incident

- The first combined Linux `vcmitest`/`vcmiclient` build over `cf1af09ea`
  plus the uncommitted Life Drain slice stopped in
  `client/battle/BattleActionsController.cpp`: a `const` target-legality helper
  called non-const `getStackForHex`, and the shared `battle::Unit` interface
  does not provide `getName()`. The client owner repaired both, the isolated
  controller object compiled, and the succeeding combined Linux build linked
  both `vcmitest` and `vcmiclient`.
- The first native Life Drain filter then rejected every valid enemy/friendly
  cast at `canBeCast`: the generic scripted-effect availability check did not
  establish a pair. The authoritative Life Drain branch now checks that both
  sides have a living, receptive target; exact-pair validation still runs
  before spending a Hero Action or Mana.
- The next filter exposed a more serious registration error: `lifeDrain` was
  already the core combat-event script identifier. Registering a spell effect
  under the same identifier made lookup ambiguous, so the effect silently
  disappeared and casts changed no health. The spell-effect script is now
  `core:lifeDrainEffect`; the existing combat event remains unchanged. Its
  dedicated script fixture, authoritative cast, and current-profile focused
  filter pass **12/12 with zero skips** after the repair. The module mirror and
  Linux `vcmitest`/`vcmiclient` targets pass. Rendered/playable acceptance is
  not claimed. Keep script-kind identifiers unique across registries.

### September 29 local Malediction native-gate incidents

- Initial `vcmitest` regeneration stopped before compilation because the
  activated perk row had not yet been copied into generated
  `Mods/new-horizons/mod.json`. Regenerating the curated module repaired this
  gate; the subsequent Linux native target linked.
- The first broader Bless filter found a pre-existing assertion expecting
  `Light School coefficient`, while unchanged production help describes the
  `combined Spell Power coefficient`. This is an adjacent stale fixture, not
  evidence that the new Malediction cast failed; retain it for the next
  appropriate fixture cleanup. The focused Curse/Sorrow/Malediction and saved
  v1/v2 server filter passes 8/8 with no skips.
- The perk registry data test initially expected all Shadow perks to remain
  planned. Its explicit active-perk list now includes Malediction; the focused
  17-test data-contract suite passes. Do not interpret its first 300/310
  aggregate count after an early subtest assertion as ten missing definitions.
- The first two new AI cases failed fixture setup, not valuation: the fixture
  lacked castable spellbook/Mana state. After that repair, they read a stale
  unit pointer from before hypothetical cast copy-on-write. Refetching by
  unit ID repaired the test. The final focused active-profile AI filter passes
  5/5 with zero skips; it proves projected 3-to-4-round value, not full
  end-to-end spell selection.
- The global active-perk art checker remains red on 21 existing neutral-icon
  gaps (Archery, Bulwark, Benediction, Herbalist). Malediction has its own
  named binding, unique 44x44 normal image and four distinct state hashes;
  do not treat the unrelated global failure as this icon's failure or claim
  that the global guard passed.

### September 29 local Shadow Sorrow native-gate incident

- The first Linux `vcmitest` build of the new Sorrow test stopped on two
  test-fixture type errors: raw `SpellID::SORROW` was passed where
  `BonusSourceID` needs a wrapped `SpellID`, and a `MasteryLevel` parameter
  shadowed a non-type identifier. The fixture now wraps the ID and renames the
  parameter; the later integrated rebuild and curated focused filter passed.
  No production-code diagnostic appeared in that first pass.
- The next integrated `vcmitest` compile reached the concurrently edited
  BattleAI Sorrow valuation and found the same raw-ID-to-`BonusSourceID`
  conversion at `AI/BattleAI/BattleEvaluator.cpp:147`. The AI owner wrapped
  the `SpellID`. The following compile reached
  `NewHorizonsMagicAITest.cpp:3897`, where a local `MasteryLevel` name made a
  lambda's intended type resolve as a non-type. The AI owner corrected the
  fixture. After those repairs, the integrated Linux `vcmitest` target linked.
  The first default-profile Sorrow filter self-skipped because it requires the
  curated New Horizons profile; the subsequent active-profile run executed.
- The first active-profile run showed the new Sorrow effect wrapper was not
  reached: the inherited core effect is registered under `timed`, not
  `morale`. The worker stopped an expensive sibling AI case after the runtime
  failures appeared, corrected the wrapper binding and fixture assumptions,
  and reran the runtime-only filter with a bounded timeout: 7/7 active-profile
  Sorrow cases passed in 1.61 seconds. No result from the skipped or
  interrupted run is counted as a pass.
- The first active-profile v3 AI Sorrow case consumed 45 seconds of CPU and
  hit its strict timeout (exit 124) without an assertion; the saved-v1 AI case
  did likewise, and the v2 case was stopped early. Bounded stage probes found
  the hang before projection or valuation: both fixtures erased spellbook
  entries while iterating the same spell container. The fixtures no longer do
  that invalidating clear. A further v3 fixture failure came from not adding
  Sorrow back to the hero's spellbook, then from an overly strict full-action
  assertion where a different action could legitimately win. The final bounded
  active-profile filter checks the real hypothetical effect and production AI
  target scorer: v3 plus saved v1/v2 pass 3/3 in 1.00 second, with no skips.
  Full `attemptCastingSpell` selection is recorded for Phase 2, not claimed
  here. No unbounded rerun was accepted.

### September 29 local Quicksand native-gate incidents

- An early isolated object build stopped at CMake regeneration because the
  edited `config/newHorizonsMagic.json` had not yet been copied into the
  generated `Mods/new-horizons/mod.json`. Regenerating with
  `tools/update-new-horizons-module.py` restored the module-consistency gate;
  the subsequent Linux `vcmitest` build linked.
- Independent source review found two pre-build blockers: the obstacle Lua
  script called an unregistered selected-Quicksand Mechanics method, and the
  client controller called a renamed window-control method that did not
  exist. Registering the Lua method and using the existing window update API
  repaired both. The first client compile then exposed a same-name data member
  and accessor, `repeatedPlacementSelectedHexes`; renaming the accessor to
  `getRepeatedPlacementSelectedHexes` resolved that C++ collision. The resumed
  Linux `vcmiclient` build linked.
- The first nine-case focused native run passed eight tests. The synthetic
  selected-placement obstacle fixture omitted the ordinary content `hidden`
  setting, exposing that canonical Quicksand did not force concealment in its
  effect descriptor. The selected mode now stores `hidden=true` irrespective
  of that content default; the same focused filter passes 9/9. This is not
  rendered/playable acceptance.

### September 29 local Holy Armor native-gate incidents

- The first Linux `vcmitest`/`vcmiclient` build over committed
  `c0fcec5d6` plus the uncommitted Holy Armor slice stopped in
  `AI/BattleAI/BattleEvaluator.cpp`: `std::clamp` received a `si16`
  duration with `int` bounds. Explicitly converting the duration to `int`
  repaired that production compile error.
- The incremental test build then stopped in
  `NewHorizonsMagicalDamageReductionTest.cpp`: a raw
  `SpellIDBase::Type` enum was passed to `BonusSubtypeID`, which requires a
  `SpellID` value. The explicit wrapper repaired the test fixture. Both Linux
  targets then linked.
- The first curated active-profile 25-case filter passed 24 and failed the
  positive Holy Armor AI choice. Instrumentation established that the
  player-specific battle callback rejected `canBeCast(CREATURE_ACTIVE)` for
  a visible opposing creature on the friendly turn, even though its public
  SPELLCASTER/CASTS bonuses identified a magical damaging spell. The threat
  forecast no longer asks whether that enemy can cast *now*; it uses visible
  remaining casts and spell traits. It also accepts damaging spells lacking
  the older `offensive` flag. A bounded health-value fallback makes protection
  of a temporarily nonattacking stack worth considering. Temporary diagnostics
  were removed. The final curated filter passed 25/25; 39 focused Python
  tests and the generated-module check pass. Windows compilation, visual
  rendering, Fire Shield interaction, and playable delivery are not claimed.

### September 29 local Empower Spell native-gate incident

- The first Linux `vcmitest`/`vcmiclient` build over committed head
  `603a68db3` plus the uncommitted Empower slice reached
  `lib/spells/ISpellMechanics.cpp` and reported a five-argument call resolving
  to the three-argument `Mechanics` member scaler. The build was stopped after
  independent review also found a potential signed-overflow product in the
  Regeneration rate and generic duration changes leaking into old snapshots.
- The repair uses the contextual three-argument member call, the checked
  rational scaler for Regeneration's `1500 × Spell Power` term, and a saved-v3
  Spellcraft-field gate for generic duration rescaling. A maximum-input
  arithmetic test and older-v3 duration assertion now cover the defects.
  The resumed Linux build linked both targets. A further adjacent filter
  exposed Spell Lock saturation arithmetic overflow under a maximum-int32
  Warcasting input; the repair caps the positive product before multiplication,
  preserving the existing duration cap. A narrow independent review found no
  blocker, and the final focused/adjacent spell filter passed 69/69 cases.
  These results do not prove
  Windows compilation, Mass Slow interaction, broad Lua parity, or playable
  rendering.

### September 28/29 local Basic Spellcraft native-gate incidents

- The first coordinated `vcmitest`/`vcmiclient` build stopped on a test-only
  `SecondarySkill::decode(string_view)` call in `FocusMagicSpellTest`; this API
  requires `std::string`. The incremental build linked after explicit conversion.
- The first active-mod 15-case filter failed only the new Cure fixture: 100
  damage to 100 Pikemen left no surviving wound, so Cure correctly rejected
  an attempted resurrection. The fixture now uses a wounded Archangel stack;
  the resumed 15/15 run passes.
- A broader 32-case saved-rules/Focus Magic filter found a stale synthetic v1
  fixture retaining New Horizons common spells (v1 forbids them), plus two
  Regeneration expectations copied with a 100000-millionth base rather than the
  actual 250000 base. Corrected fixture/expectations pass 32/32. The offline
  perk-data gate also lacked the previously activated Herbalist in its expected
  set; correcting that inventory restores its 64-case bundle. These were
  fixture/expectation errors, not a claim of playable delivery.

### September 28 local Nature Poison native-gate incidents

- Failure ID/stage: local `vcmitest -j12` over committed base `e6545354a` plus
  the uncommitted Nature Poison slice. The first compile stopped in
  `NewHorizonsCureTest.cpp` because a test called non-public
  `newHorizonsMagic::spellAllowedBySavedRoster`; a later incremental compile
  stopped in `NewHorizonsMagicAITest.cpp` because `SecondarySkill::decode`
  needs `std::string`, not the `string_view` constant. Both were test-only API
  errors. The owners switched to the public availability header and explicit
  string conversion; the incremental `vcmitest` target then linked.
- Focused execution initially discovered nine tests but skipped all nine
  because two existing TEST presets omitted `new-horizons`. A fresh isolated
  `testModSettings.json` with `core`, `vcmi`, `vcmi-test`, and `new-horizons`
  is required; ordinary `modSettings.json` is not enough for `vcmitest`.
  Never count a zero-failure run with all cases skipped.
- The first active-profile eight-case run passed five and failed three. Two
  recast fixtures expected Basic Nature's 77-damage base without giving the
  hero Basic Nature; the real unranked base is 70. The AI fixture enabled
  expanded hero ratings while disabling required hero-command rules. Repairs
  changed only fixture setup. The subsequent incremental link and active-
  profile focused run passed 8/8 with zero skips; saved-v2 roster exclusion
  and adjacent Magic Arrow AI cases each passed 1/1. No playable or Windows
  target result follows from this local native gate.

### September 28 local Phase 1 Blacksmith/Regeneration build

- Failure ID: local `cmake --build build/new-horizons-linux --target vcmitest vcmiclient -j12` over source head `82567abae` plus the uncommitted UP-024/Regeneration checkpoint.
- Observed stage/error: compilation stopped at `AI/Nullkiller2/AIGateway.cpp:91`; `const auto * stack = slot.second` could not deduce a pointer from `std::unique_ptr<CStackInstance>`. No linked target or runtime test result was produced by this attempt.
- Confirmed cause/fix: the new AI machine-utility scan iterated an army map whose values are owning pointers; use `slot.second.get()` to inspect the stack without transferring ownership.
- Guard/result: the exact AI translation unit compiled in the resumed build, and both Linux `vcmiclient` and `vcmitest` linked successfully. The focused AI source guard passes, but it did not catch this C++ type error; native compilation remains the necessary gate. The subsequent focused runtime filters are recorded separately from this build repair.

- Full34089398757 passed the real CRT gate, then MSVC rejected the level snapshot's
  unsigned-to-int brace conversion (C2397). GCC's permissive build had not made it
  fatal.35119d534 changes only that field to the actual hero level type, ui32.
  `test_windows_level_snapshot.py` extracts the real declaration and compiles it
  with MSVC or GCC -Werror=narrowing before the expensive client build. Local old
  reproduction fails; fixed declaration/native client and85 package cases pass.
  Corrected FULL34093695275 is monitored separately, not presumed successful.

- Full34087844032 atbc376 failed **before compilation** in the new CRT gate:
  `ModuleNotFoundError: pefile`. The82 Windows package tests and complete source
  preflight passed, but synthetic CRT tests mocked the parser and did not provision
  the real CI dependency.322cbe025 pins `pefile==2024.8.26` alongside Conan, runs a
  real import/version smoke before regressions, and adds an ordering control.
  83 offline tests pass; corrected FULL34089398757 is separately monitored. Do not
  report it successful until actual terminal evidence. The retained failure report
  and logs remain authoritative; no compilation/game archive occurred in340878.
- Actual a0 CRT versions14.29.30157.0 differed from the notice collector's available
  redist directory14.51.36231. Future provenance now separates the environment from
  byte-matched CMake-selected runtime sources and retained terms. An actual decoder
  and synthetic VS-tree tests do not reconstruct a historical runner's copy origin.
- Activating the exact GUI-tested growth metadata exposed two separate controls:
  the CMake canonical comparator omitted Heroes, and the older full-book export
  assumed legacy200mana for authored Knowledge20. Add Heroes to configure inputs/
  equality; preserve explicit legacy200/new20 expectations and manaLimit checks.
  Final11 data checks and36/128/39 native scopes pass; original REDs retained.
- A clean-prefix Linux build can still lose build RPATH during ordinary install.
  First staged ldd could not resolve adjacent libvcmi without launcher environment.
  Explicit CMake install `$ORIGIN` and build-with-install-RPATH required only normal
  relinks. Do not patch frozen ELF strings. The copied developer launcher also has
  a development-tree default; packaged entry-point behavior needs its own test.

## September 27 feature-build incidents

### September 28 local School-rank build check

The later September 28 full native build deliberately reconciled the curated
module against all current canonical config sources with
`tools/update-new-horizons-module.py`, then verified its `--check` output
before normal CMake regeneration. This supersedes the earlier dirty-tree
blocker for this checkout; it does **not** make that earlier partial link a
validated release. The Linux `vcmi`, `vcmiclient`, and `vcmitest` targets now
link. Several first-discovered test compilation errors were stale fixture API
references: typed `SpellID` wrappers are required for bonus IDs;
`getEffectDuration()` mocks return an integer, not `optional`; bonus collections
iterate `shared_ptr<Bonus>`; a creature Defense bonus is
`PRIMARY_SKILL`/`PrimarySkill::DEFENSE`; and battle AI fixture tests must
include complete callback/handler types. The corrected object builds are
recorded separately from runtime pass/fail.

Running `vcmitest` from the ordinary build root skipped every New Horizons
Bless/Bulwark case because the TEST preset did not activate `new-horizons`.
Native gameplay evidence requires a private profile with the curated module
and `vcmi-test` both mounted. The isolated v3 profile under
`build/nh-current-v3-native` exposes the actual failures; a green build or a
zero-failure test invocation with all relevant tests skipped is not success.

The first September 28 attempt to deliver the Linux garrison split UI used
the active dirty build tree with a filtered Ninja manifest. The client objects
compiled, but final linking failed on unresolved `newHorizonsArchery::*`
symbols: `lib/CMakeLists.txt` lists the newer `NewHorizonsArchery.cpp`, while
that build tree's stale `build.ninja` had no object rule for it. Normal CMake
regeneration was also blocked by the independently dirty creature-category
module mirror. Do not promote or treat that mixed build as playable. Delivery
used a clean detached checkout at `8c4ad7e5f` and a fresh CMake build instead;
that client linked and its frozen snapshot passed the bounded headless smoke.

The dirty-tree Linux `vcmi` production target linked successfully with the
version-3 School-rank and Transfigure Matter code using a Ninja manifest that
skipped only CMake regeneration. Normal regeneration was blocked by a stale
combined module settings mirror: its magic/perk sections match their canonical
sources, but its creature-category section does not match separate dirty work.
Do not run the broad module generator merely to clear this guard.

The full `vcmitest` link remains unverified. Compilation exposed several
pre-existing test fixture gaps (missing complete serialization types, an
unknown `STACKS_DEFENSE` bonus name, a const stack passed to a mutating
helper). The Demonic Gating, Unique Building Training, and Hero Command AI
test translation units were repaired and compiled individually; the next
unrelated full-target failure has not been pursued. These test-target repairs
are not evidence that the newly linked production library or a playable client
passed. To avoid delaying gameplay work on a chain of stale fixtures, use
focused data/source checks and the production-library build for the current
School-rank slice, while retaining the missing native execution/full-suite
validation as an explicit release gate.

These failures came from successive gameplay-feature commits rather than from the
Windows packaging route. They are retained because a later successful run does not
make the failed compiler evidence disposable.

| Failure / evidence | Cause and repair | Prevention / later target evidence |
|---|---|---|
| Run `36279452019`, head `db4905a5f`: MSVC reported that `spells::Mechanics::isMetamagicFollowup` did not exist while compiling the Lua registrar. | The Metamagic refactor left the Lua-facing interface without the virtual method used by its binding. `9cd3ac7e6` restored the interface contract. | `client/tests/check-new-horizons-metamagic-prompt.py` now checks the binding/interface seam. Run `36282269395` completed successfully with the repair. |
| Runs `36293431069` (head `1dd212f05`) and `36295633007` (head `e960efa1e`): MSVC rejected mutation of `masterGateUsed`, `pendingGateHexes` and `pendingDemonicGates` through a const battle object. | The Master Gate packet visitor obtained a const state while applying an authoritative mutation. `20ac211fe` uses the mutable battle-state path. Both runs exposed the same defect; they are not two independent diagnoses. | The repaired source passed this compile point and run `36296409606` completed successfully. Any packet visitor that mutates battle state must obtain a mutable state explicitly; `const_cast` is not the default repair. |
| Run `36298149952`, head `132b7ff9e`: MSVC reported that `pursuitMovementRemaining` was not a member of the abstract `battle::Unit` interface. | Pursuit read concrete mutable-stack state through the narrower unit interface. `b0b03b796` accesses the concrete state at the authoritative server seam. | Later full run `36311249815` compiled the repaired route successfully. New state fields must be read through the interface that actually owns them, or be deliberately promoted to a shared interface. |
| Run `36301307205`, head `b0b03b796`, and run `36303570540`, head `0d99e1580`: MSVC rejected two mixed-type `std::min` calls in Cleave hex selection, first in authoritative lib code and then in the duplicated AI projection. | `int` and `BattleHex`'s signed storage type made template deduction ambiguous. `676b221c5` fixed the authoritative expression and `fe144fecc` fixed the remaining AI expression. | Run `36311249815` completed successfully. When authoritative and AI calculations mirror one another, review both call sites together and use an explicit common type/helper instead of relying on platform-specific template conversion. |
| Run `36308724192`, head `71de5cd37`: MSVC rejected `BonusList::empty()` because `NewHorizonsOffense.h` only saw the forward declaration. | An inline helper dereferenced `BonusList` without including its complete definition. `b863215c5` added the owning header. | Runs `36311249815` and `36315781168` completed successfully. Inline header code that calls members requires the complete type; successful compilation in an unrelated translation unit is not sufficient evidence. |
| Run `36343003125`, head `b916c75ab`: MSVC rejected Spell Lock AI valuation in `SpellTargetsEvaluator.cpp` because `CSpell` was incomplete. | The translation unit called concrete `CSpell` methods while including only the public spell interface/forward declaration. The repair adds the owning `lib/spells/CSpell.h` header at the use site. | Exact-head Windows run `36353181774` succeeded, compiling BattleAI and publishing package artifact `10943952550`; this proves the compiler repair, not playable behavior. |
| Run `36347971057`, head `444250b5f`: after the `CSpell` include repair, MSVC reached the same Spell Lock AI translation unit and rejected `.value_or(1)` on `spells::IBattleCast::Value`. | `spells::Mechanics::getEffectDuration()` returns a plain `int32_t`; the implementation had confused it with the optional duration on a different cast interface. Commit `ddcd57391` removes `.value_or(1)` and clamps the integer result directly. | Exact-head Windows run `36353181774` succeeded, compiling BattleAI and publishing package artifact `10943952550`; playable Spell Lock behavior remains a separate gate. |
| Targeted Linux compile-database syntax check of committed transfer/UI head `3865be695`: `CExchangeController.cpp:154` reported `LIBRARY` undeclared. | The new exact-one gameplay explanation used `LIBRARY` without including its declaring `lib/GameLibrary.h`; another UI translation unit's include did not make this declaration available here. Add the direct owning header. | The repaired translation unit passed a repeated `-fsyntax-only` check; the other six transfer/UI units and three focused source guards passed. A succeeding exact-head Windows client build remains required. The queued `3865be695` job predates this repair and cannot prove it. |
| Run `36350655648`, head `ddcd57391`: MSVC reached `BattleActionProcessor.cpp` and rejected the Counterfire `AttackDescriptor` initializer because `.archeryRangedDamageMultiplierPercent` followed `.counter` and `.archeryCounterfire` (C7560). | C++20 designated initializers must follow member declaration order; the previous Linux syntax check accepted this ordering. The reviewed Archery commit `007510b61` places the damage multiplier before the counter fields. | Exact-head Windows run `36353181774` succeeded and published package artifact `10943952550`; no playable Counterfire acceptance is inferred. |

For every future terminal CI failure, append the run/head, exact failing stage,
confirmed cause, repair commit, regression guard (or an explicit statement that no
focused guard exists), and the first succeeding target-platform run. Do this before
the incident is considered closed; do not rely on chat history or a green successor
run as the only record.

## Recorded failures

| Failure / evidence | Cause or supported diagnosis | Guard and verification scope |
|---|---|---|
| `avformat-63.dll -> ncrypt.dll`, run 33991718723 | PE audit did not classify a legitimate Windows system DLL. | `tools/tests/test_windows_pe_audit.py`: narrowly classified system imports; unknown missing runtime DLLs still fail. Do not extend a blanket DLL allowlist. |
| Microsoft toolchain/SDK notice discovery | Notice lookup did not reliably supply full redistribution terms; a guessed URL redirected elsewhere. | Pin/review the official full document and hash; require installed SDK terms. `bf7dec157` records repair. A successful HTTP response is not proof of the correct document. |
| `opengl/system` lacked a conventional dependency license | Metadata-only Windows system provider was treated as a redistributable implementation. | `test_windows_system_dependency.py`: exact reference/platform and empty-payload checks; unexpected payload/missing real dependency licenses remain failures. |
| Old Windows artifact omitted dav1d, Brotli and PlutoVG source/notice coverage | Auditing shipped DLL names did not establish the full statically linked dependency inventory. | Full Conan graph plus binary identity, static nodes and exact source roots; `test_windows_notice_overlay.py` and independent old-payload repack audit. An accepted repair of old bytes is not a new mod build. |
| md4c missing `patches/0.5.2-0001-honor-vc-runtime.patch` in `source()`; run 34040567531 | Copied recipe lacked required exported source files. | `test_windows_dependency_notices.py`: manifest-safe paths/checksums, recover exact `md4c/0.5.2#3d7106721e458f9f799b87d4d50d02e0` exports in an isolated cache, retain patches in source archive. `98bd74f52`; later CI explicitly downloaded/applied the patch. Never skip the patch or upgrade the dependency to conceal missing exports. |
| StepSecurity subscription failure; run 34070234107 | CI action required a subscription for a private repository. | Check action plan/visibility compatibility before dispatch. `30db54ce6` replaced both subscription-gated providers with pinned upstream ilammy/msvc-dev-cmd and hendrikmuhs/ccache-action; run34070750674 passed those actions. User independently changed visibility back to public. Workers must not change visibility or disable security controls as an automatic workaround. |
| Setup 50 checks passed, then Managed-Preset smoke failed; run 34070750674 | `$PSScriptRoot` was empty when a parameter-default `Join-Path` expression was evaluated on Windows PowerShell 5.1. | `tools/windows/tests/Managed-Preset-Smoke.ps1`: resolve default in script body; preserve explicit override and all assertions. Default/explicit-path local PS7 checks passed; they alone do not prove PS5.1. Execute both smoke scripts with the real supported Windows shell. |
| SQLite `3.53.4#89fcf5cda598966acb7f3e185b19c58d` missing dependency license text | Conventional license-filename discovery missed the public-domain dedication embedded in upstream source. | Build has added header-notice extraction and tests in `test_windows_dependency_notices.py` for no standalone LICENSE, missing notice and incomplete notice. Verify exact-source header retention and verbatim complete notice; recipe MIT text is not a substitute. Current test/CI disposition belongs in Build handoff, not an assumed PASS here. |
| Local MinGW SDL_ttf unresolved `__imp_plutovg_*` | Static/shared declaration mismatch in the Windows dependency route. | Supported shared PlutoVG route repaired dependency build. Keep import/export configuration and graph identity in cross-build evidence. MSVC success never proved MinGW compatibility. |
| Local Windows nested link expressions, wide filesystem path constructor and `SDL_main` link failure | CMake/compiler/backend-specific compatibility defects encountered on the new MinGW route. | Separate fixes `b6f58bbae`, `f573a59bf`, `95a7001e3`; actual Windows build/install exit0. Preserve Windows incremental compile/link gates after public-header, SDL, filesystem and CMake changes. These failures are not evidence that the earlier MSVC executable was absent. |
| GNU runtime DLL omissions and Ogg DLL-name mismatch | Successful link used development-machine dependencies not yet closed in the deploy directory. | `test_mingw_runtime.py`, PE import closure on actual packaged files, exact DLL provenance. Do not ship an EXE alone or guess filename aliases without verifying the binary. |
| Six-school schema errors and unresponsive All tab after successful compilation | Runtime schema used unsupported validation features; hover state changed before toggle click dispatch. | Fix `034238454`; actual config-loading and normal-click GUI retest closed those observed defects. Add production-validator checks and real input routes early; compilation cannot establish either behavior. |
| Local034 ZIP contained173 home-path strings in nine PE data sections | Binary `__FILE__` strings and FFmpeg configure-tool paths escaped a text-file-only privacy scan. | `test_binary_privacy.py`: whole-binary ASCII/UTF16 scan, data-section and alignment controls; exact9/173 RED reproduced. No publication, debug-only stripping or binary string patching. Rebuild/remap and re-audit required. |
| Local remapped libiconv configure failed77 | Autotools split a space-bearing prefix flag; quote characters inside CFLAGS were not shell syntax. | Dependency remap must use space-free source roots, and real dependency compilation must verify it. A tiny compiler probe is not whole dependency acceptance. |

## Cross-platform fixture lesson — case-collision regression

A later Windows regression run passed 70 of 71 tests but errored in
`test_mingw_runtime.MinGWRuntimeTest.test_case_collision_rejected` with missing
`pefile`. The fixture tried to create `VCMI_lib.dll` and `VCMI_LIB.DLL` as separate
files. On case-insensitive Windows storage this overwrote one entry, so the
collision guard was never exercised and the test fell through into PE parsing.
Installing the parser alone would not repair the missing collision condition.

Build changed the test to provide an explicit case-colliding input listing on
either host and assert that `inspect_pe` is never called. Dispatcher independently
ran the then-current aggregate regression suite: 72 tests, exit0 (one additional
case had been added since the failing 71-case CI). Evidence is
`build/new-horizons-windows-cross/package-regressions-dispatcher-review.log/.json`.
This local result is not the subsequent Windows CI result; Build records that
separately. No production collision check was removed.

**Prevention:** filesystem-case, symlink, path-separator, locale and shell-version
assumptions in fixtures must be explicit. For a synthetic early-rejection test,
model the impossible-on-this-host input and assert later parsing/execution is not
reached. Keep real target-platform integration checks as separate evidence.

## Required prevention workflow

Build owns implementation of these checks; documentation does not mean automation
already exists. Use this list as the readiness review before the next dispatch:

- Run all relevant existing package/source/PE/setup regressions, not only the
  newly failing dependency's test. Record invocation, exit and source identity.
- Audit the entire resolved dependency graph, including static and header-only
  dependencies that need notices/source coverage. Preserve exact recipe/package
  revisions, options and source hashes for the frozen payload.
- Where failures are independent and safe to collect, inspect all dependencies and
  emit one structured preflight report rather than stop after the first missing
  notice/source. At the end, fail the gate if any entry failed. Never treat a
  partial report/archive as publishable. Abort unsafe extraction immediately;
  aggregation is not permission to continue executing untrusted broken inputs.
- Keep source/notice and shell smoke preflight ahead of the expensive client
  compile. Use exact Windows PowerShell 5.1 for its supported launcher/test path;
  PS7-on-Linux is supplementary evidence. Do not infer CMD/GUI success from it.
- Retain concise failure reports as CI artifacts even on failure. Reuse matching
  compiled artifacts for packaging-only corrections, with explicit base/source/
  packaging identities; never substitute an older feature payload unnoticed.
- Compare a changed graph against the last accepted graph before reuse. A new
  dependency version or compiler route invalidates the relevant prior evidence.
- Add positive and negative tests with each repair. Test that genuinely missing
  sources/licenses/DLLs still reject; do not solve a failure by weakening a gate.
- Read the actual final exit and independent acceptance report before pushing a
  ready claim or promising a release time. Build, package, launch, gameplay and
  publication are separate stages with separate evidence.

## How to append an incident

### 2026-09-29 — Verdant Prison pre-build review

First Linux build over `dd73972b7` stopped in the new runtime fixture at
line 243: the School coefficient helper expects `SpellID`, but the fixture
passed `const CSpell *` from `spell.toSpell()`. Pass the existing typed ID
directly. The production shared library and Lua binding had already linked;
the test/client build is not successful until the retry exits zero.
The first retry exposed the same incorrect argument in the Warden case at
line 410. Correct both coefficient calls and check all occurrences in the
new fixture before retrying; the first repair was incomplete.
The second retry builds and links both `vcmitest` and `vcmiclient`, exit 0.
The isolated runtime/AI filter is the next gate; compilation alone does not
establish ring geometry, pool allocation or reflected-cast correctness.

First isolated native run: 11 cases, six pass, five fail, zero skips, exit 1.
The wide-target fixture used a Green Dragon immune to this Level-3 spell;
Warden selection used Basic Nature despite requiring Advanced; and the MR
fixture expected a paid cast against 100% resistance, which the ordinary
negative-spell target condition correctly rejects before spending. Repair
these fixture assumptions without weakening immunity, perk eligibility or
resistance guards. Two actual AI submission cases also declined to cast;
their production/fixture cause is under investigation. Logs and XML are
`build/new-horizons-linux/guardian-active-5jftEx/runner/NewHorizonsVerdantPrison-focused.*`.
The conditional Trolls guard was not run after this failing gate.
Runtime fixtures now use nonimmune wide Centaurs, legitimate Advanced Nature
for Warden (`13000` basis points, still exactly 229 HP at SP 1), and pre-cost
rejection for full resistance. Lua behavior is unchanged. AI diagnosis and
the focused retry remain pending; do not increment completed coverage yet.
After the fresh fixture build, retry3 runs 11 cases: seven pass, four fail,
zero skips. Both AI cases now prove detached exact ring/HP creation before
failing actual action selection. Full-resistance pre-cost rejection and
reflected-friendly ring placement pass. Wide head/tail previews agree, but
the rear-hex authoritative cast still rejects; Warden setup additionally
needs a legitimate earlier-tier perk. Preserve progression and rear-hex
acceptance requirements while repairing these paths. Retry3 logs/XML are
retained alongside the initial failures; the conditional Trolls guard remains
unrun while this gate fails.
Further fixture repairs preserve actual rear-hex targeting while explicitly
giving the attacker its turn (the Centaur otherwise acts first), and select
Rootcaller at Basic before Advanced Warden. The rear-hex lookup path showed
no source defect. AI baseline investigation also found the existing sensible
early-decline guard when allied blockers can kill a lone Peasant before it
acts; verify a nontrivial surviving enemy fixture before changing production
selection logic. Native evidence for these repairs remains pending.
Retry4 passes nine of 11, zero skips: actual rear-hex casting and both
positive AI submission/forecast-resolution cases now pass. Remaining failures
are assertion errors: measuring the legal ring after summons occupy it, and
forbidding all AI Hero Actions when only Verdant Prison is unavailable.
Capture Warden's ring before spawning and assert no Verdant Prison submission
while permitting legal Orders. Keep exact HP, target viability, unchanged
live Mana and absence of summons assertions. Retry4 logs/XML are preserved.
First complete succeeding native gate: retry5 passes 11/11 Verdant cases and
10/10 Summon Trolls shared-path guard cases, zero skips, both exit 0. Both
Linux targets link, content/inventory passes 55/55, module mirror and targeting
source guard pass. Independent review has no remaining production blocker.
Earlier failure logs/XML remain alongside `NewHorizonsVerdantPrison-focused-retry5.*`
and `NewHorizonsSummonTrolls-shared-guard-retry5.*` in the isolated runner.
Rendered/playable, full combat save/load and broad interactions are not proven
by this gate and remain tracked separately for Phase 2.

The first proposed reflection binding called `Mechanics::getMode()`, but that
method belongs to `IBattleCast`, not the shared effect facade. Review caught
this before compilation. Add a narrow read-only `isMagicMirror()` facade
with a BaseMechanics override; do not expose mutable cast mode or bypass
ordinary resistance/reflection. Source review verifies the corrected binding;
native compilation and focused reflected-cast execution remain separate gates.

Candidate base: `dd73972b7`, UP-040 working slice; not a playable/published
head. Independent review caught premature integer rounding in the initial
pool draft: at SP 1, Basic Nature and Verdant Warden, rounding the scaled SP
term first gives 228 HP instead of the canonical final-floor result 229.
Require hundredths precision through the whole-pool modifier, as in Summon
Trolls, plus a hardcoded 229-HP regression rather than an expected-value helper
that could repeat the same bug. No passing native gate is claimed yet.
The reviewer also flagged stale raw-unit precedence during target resolution;
resolve from the current battle/target rather than trusting a stale pointer.
Runtime and AI work are still in progress; subsequent findings, repairs and
first successful build/native evidence must be appended to this checkpoint.

Review found a second core-path mismatch: transforming the enemy into only
empty ring locations bypasses the ordinary affected-unit Magic Resistance
filter, while generic Magic Mirror redirects to a friendly anchor that the
draft rejected, producing a paid no-op. Preserve the resolved unit anchor
through transform/filter and derive placements in apply/preview instead.
The standard resistance/Spell Lock/Mirror pipeline then filters the actual
target; allow a caster-side anchor only for a reflected cast via a tiny
read-only Lua mode binding. The battlefield overlay still obtains the same
shared ring from `adjustAffectedHexes`. Do not disable resistance or mark the
spell nonmagical to mask this issue. Focused native evidence remains pending.

### 2026-09-29 — Summon Trolls pre-build review and content fixture repairs

Candidate base: `4805d2c7c`, UP-039 working slice; not a published/playable head.
Independent review caught a Lua selected-hex helper using unbound `self`, which
would fail target checks. The worker threaded the instance into the helper.
Review also identified that adding flat hero `STACK_HEALTH` to base creature HP
does not model percentage/limited health artifacts, including Elixir of Life.
The slice must use a prospective stack's ordinary bonus evaluation, not an
artifact-name exception or an incorrect count preview. Native evidence is
pending while that repair is integrated.

The content dimension assertion initially used a legacy RGBA-only helper for
the purpose-made opaque RGB spell exports; the 54-check content/inventory run
failed with `Expected authored eight-bit RGBA artwork`. Replaced only that
new dimension assertion with PNG IHDR dimensions, as used by Vengeful Vines;
the unrelated RGBA helper remains unchanged. The same focused command then
passes 54/54 (52 content plus two perk-inventory checks). Runtime copies are
non-interlaced 44/32/30 RGB, and their hashes match retained exports. This is
content evidence, not native or rendered acceptance.

The first Linux client/test build stopped in `BattleSpellMechanics.cpp` because
the recorder accessed `natureSummoned` on abstract `battle::Unit`; that flag
belongs to `CUnitState`. The same access in the new AI slice was identified
before its object compiled. Repair uses the existing acquired unit state and
public `isSummoned()` interface, without inventing new saved fields. Rebuild and
native execution remain pending; log: `summon-trolls-build.log` under the local
build directory.

Follow-up source review found a foundational detached-AI mismatch: newly
created `StackWithBonuses(UnitInfo)` inherited only its creature template,
not the army graph. With Elixir and SP 57, a count of five Trolls projected
200 HP instead of the authoritative 242. This is not deferred as tactical
polish: the ordinary army-bonus inheritance and owned bearer lifetime must
be repaired for new hypothetical units, with an Elixir projection/resolution
case. Existing builds/runs remain serialized while the repair is prepared;
no passing baseline result may conceal this known variant failure.

The second Linux build linked the shared library and client library, then
stopped in the new runtime fixture: `CStack::getCreature()` is not an engine
API (`unitType()` is), and a restored abstract `battle::Unit` was again queried
for its state-only Nature flag. Only the new fixture accesses are repaired;
the test contract is not weakened. Log: `summon-trolls-build-retry.log`.
No baseline native result is claimed from this interrupted build. The next
candidate also includes the reviewed hypothetical-army inheritance repair.

The third build compiled the repaired production paths but stopped in the new
AI fixture's two hypothetical-battle constructors. Its
`shared_ptr<CPlayerBattleCallback>` could not convert to the base callback
because the concrete callback type was incomplete in that translation unit.
Repair adds the missing concrete callback header, matching the existing
Vengeful Vines fixture; no explicit unsafe cast or assertion removal is used.
Log: `summon-trolls-build-retry2.log`. Native evidence is still pending.

The fourth build passes both client/test targets. The first active-profile
Troll filter runs ten cases with zero skips, but only one passes. Inspection
isolates two fixture assumptions: `BattleTestFixture::addStack` deploys its
baseline Troll through `BattleInfo::addUnit` at the summoned slot placeholder
(even though its `UnitInfo.summoned` flag is false), and `Unit::isSummoned()`
tests that slot identity. The result finder must therefore also require
Nature-summon provenance; and Elixir's component artifacts really do contribute
four flat HP, giving a living Troll 54 max HP, not the assumed 50. Both AI
projection and authoritative resolution already agree on 54 max HP and the
242 aggregate pool. Repair changes the fixture finder and exact expected
health, not production behavior or the core assertions. This corrects the
earlier unverified explanation that component metadata did not add bonuses.
The two existing targeted Phantom Army/Transfigure Matter AI guards pass 2/2,
zero skips. Logs: `NewHorizonsSummonTrolls-focused.log` and
`NewHorizonsSummonTrolls-ai-guard-focused.log` in the isolated local runner.
The Troll filter still requires a fresh passing rerun before closure.

The fifth build passes both targets; the refreshed Troll filter passes 9/10,
zero skips. Its only remaining failure expected mutable health and Nature
provenance from `CMemorySerializer::deepCopy(BattleInfo)`. Independent review
confirmed `CStack::serialize` explicitly omits runtime unit state. The real
spawn/update contract is `UnitInfo` JSON plus `CUnitState::save/load` JSON,
also exercised by the existing Phantom Army tests. Replace the wrong-layer
assertion with those two roundtrips and an authoritative `BattleUnitsChanged`
UPDATE replay, retaining exact HP, count, position and provenance assertions.
No production serialization redesign is inferred from this fixture failure.
Full mid-combat binary save/reload remains separately unverified Phase 2 work.
Log: `NewHorizonsSummonTrolls-focused-retry.log`. The corrected test still
requires a fresh build and native rerun before claiming success.

A root validation rerun mistyped the perk-inventory module as
`test_new_horizons_perk_inventory`, producing one import error after 52 content
checks passed. The actual module is `test_new_horizons_ui_perk_inventory`;
rerunning the correct content/inventory pair passes 54/54. This was a command
error, not a product or test-suite failure; no assertion was changed.

The sixth build explicitly exits 0 for both Linux client/test targets after
compiling the corrected JSON roundtrip fixture. The fresh isolated active-profile
`NewHorizonsSummonTrolls*` rerun passes 10/10, zero skips; both targeted
Phantom Army/Transfigure Matter AI guards pass again, 2/2, zero skips.
Logs: `NewHorizonsSummonTrolls-focused-retry5.log` and
`NewHorizonsSummonTrolls-ai-guard-retry5.log` in the local runner. The tester
verified the copied binary and refreshed resources while preserving its
profile-only test mod. No GUI, purchaser assets or normal play profile changed.
This closes the focused gate, not rendered/playable or full save/reload gates.

2026-09-29 Hydra independent source review found a pre-build compile defect:
the new shared effect-value override referenced a School coefficient declared
inside a preceding `else` block. Move Hydra's formula into that existing shared
scope, before Cure, preserving the explicit event/caster-value override
contract. No failed native build is claimed for this source finding. A fresh
build and focused cast/preview checks remain required. Review also identified
cohort-incompatible AI health reconstruction in `calculateDamageReduce` and
an unset projected Metamagic target ID; the AI owner has the bounded repairs.
Broad forecast quality and full interaction matrices remain Phase 2.

The first compact-health review additionally found an exact count-integrity
defect in temporary resurrection cleanup: `takeResurrected` used
`resurrected * maximumHP` as damage against partially filled health cohorts,
which can remove more than that many creatures. Although initially described
as deferred reward integration, root classified known troop deletion as
BLOCKING. The runtime owner must remove the exact recorded number of survivors
and verify the principal cleanup case before this slice can be delivered.
No native failure is claimed yet; this is a concrete source-review finding.

2026-09-29 Hydra registration checkpoint: the content test command again used
the nonexistent `test_new_horizons_perk_inventory` module. The correct
`test_new_horizons_ui_perk_inventory` pair passes 56/56; no product assertions
were weakened. A separate `test_new_horizons_perk_data` run exposed its stale
explicit activation inventory: six already implemented Light perks and three
already implemented Nature perks still expected `planned`. This also caused a
misleading 291/310 count because each subtest stopped before appending all IDs.
Update the explicit expected set to the nine previously delivered activations;
retain canonical roster, rank, description, count and source-hash assertions.
This is a test-maintenance repair, not a new perk activation or coverage gain.
The repaired content, UI inventory and canonical perk-data checks pass 73/73;
the module-mirror check and diff check also pass. These are source/data checks,
not a Hydra runtime or native-build result.

Hydra's initial Lua target transformation accepted only destinations already
containing a unit pointer. Human hex-only selections require the shared
`unitEffect` range resolver before single-target eligibility checks. Root also
found side-based friendliness disagreed with authoritative controller-aware
ownership, and the oversized-capacity rejection occurred after cast costs.
The runtime owner is repairing target resolution and pre-cost validation and
adding a raw-hex cast guard. These are pre-build source findings, not observed
playable failures; native verification remains required.

Root's detached-state audit found two principal AI omissions: the hypothetical
activation path lacked the capacity-regeneration tick, and bonus-change
normalization used the legacy `getStacksIf` surface, which hypothetical battles
delegate to original stacks. Read projected units through `getUnitsIf` instead
and keep activation healing inside the same genuine-activation gate as the
authoritative flow. Otherwise forecasting can omit healing or overwrite the
detached health ledger with original state. The runtime owner has both repairs;
the focused AI forecast/resolution test must verify them before delivery.
Also widen per-creature regeneration additions before clamping to avoid signed
32-bit overflow at high valid capacities. These remain source findings until
the native suite is built and executed.

The first Hydra Linux client/test build stops at `CCreatureWindow.cpp:1445`:
`bonusToGraphics` accepts a `shared_ptr<Bonus>`, but the new fallback status row
passed `.get()`. Pass the shared pointer itself, preserving the existing API.
Core library/AI objects linked before this client error; neither complete client
nor test success is claimed. Restart the stopped build after this one-line
repair, also recompiling the pre-cost and fixture fixes that landed during the
first pass. The focused data/UI-source guards do not replace this compile gate.

The repaired Linux build passes both client/test targets; the incremental
follow-up also exits 0. The isolated refreshed Hydra filter runs 8 cases,
zero skips: 5 pass and 3 fail. The raw wide-tail hex action is rejected; the
saved-v2 fixture throws `unknown field selectedPlacement`; and actual AI
submits a Hero Order rather than Hydra. Preserve the raw-hex and actual AI
submission assertions. Root confirmed the hex failure: generic target
transformation intentionally leaves raw hexes unresolved, but the Hydra C++
eligibility guard requires a unit pointer before Lua resolution can run.
Resolve a living occupant only in the Hydra guard; do not globally redirect
other creature spells or corpse selections. Runtime also owns removal of the
v3-only field from the v2 fixture; AI owns its selection/forecast diagnosis.
No production AI cause is inferred from the fallback action alone.
The separate existing health/Regeneration/Cure guards pass 16/16, zero skips.
Logs/XML are retained as `NewHorizonsHydrasVitality-focused` and
`NewHorizonsHydrasVitality-health-guards-focused` in the isolated runner.
The tester verifies identical build/runner binaries and refreshed resources,
preserving profile-only test content. Repair, rebuild and rerun before
delivery or coverage changes.

2026-09-30 Hydra retry2: the corrected client/test build passes. Refreshed
isolated binaries/resources match; all six server cases now pass, including
raw-tail targeting and saved-v2 pre-cost rejection. The invalid-target AI case
passes, but actual friendly spell selection still submits an Order (`0F`)
rather than a Spell (`04`), so the filter is 7/8, zero skips. Reducing the
distant enemy fixture from 100 Peasants to one did not resolve that assertion.
Do not infer that reach-unaware Order valuation alone explains the failure;
collect actual candidate/forecast scores before changing implementation or
the strict submission assertion. Existing health/Regeneration/Cure guards
again pass 16/16. Retained evidence is `NewHorizonsHydrasVitality-retry2` and
`NewHorizonsHydrasVitality-health-guards-retry2` (log/XML). No Hydra delivery
or coverage increase is claimed yet.

The stderr diagnostic run resolves the remaining selection cause: Hydra
forecasts two genuine 50-HP healing ticks, each worth 9.9, for score 19.8;
Brace receives 55 and wins. Hold the Line is only 0.1 with one Peasant.
The forecast is not missing, and the strict fixture's melee-heavy composition
does not establish that Hydra is the best legal action. Retain all legal
Orders and use a stationary opposing stack in the favourable selection
scenario, removing Brace's actual advancing/preemptive opportunity. Changing
to a shooter would not solve this reliably: `isMeleeAttacker()` includes all
non-siege units. Keep exact submission, detached/live immutability and real
activation parity assertions. Temporary diagnostic output is removed before
the final build; no production scoring was changed for this fixture repair.
Evidence: `hydra-ai-diagnostic-stderr.log`/XML, one failing test, zero skips.
Reach/path-aware Order valuation remains explicitly deferred Phase 2.

Final focused gate, 2026-09-30: the corrected client/test build passes;
`NewHorizonsHydrasVitality*` passes 8/8 and the existing health guards pass
16/16, zero skips. Refreshed build/runner binaries match SHA-256
`81d50a90ca12a5a83845dfc5cdd36c118b054331359d117734e0de2761065354`.
Resources match, profile-only test fixtures are preserved, and temporary
diagnostics are absent. Retained log/XML pairs are
`NewHorizonsHydrasVitality-final-focused` and
`NewHorizonsHydrasVitality-final-health-guards`. This closes the focused
principal gate, not full save/load, rendered/playable or tactical-quality gates.

### 2026-09-30 UP-059 acquisition compile admission

Local client/test build 4073 (`UP059-acquisition-build.log`) stopped at the new
purchase guard's missing availability header and two unqualified test helper
calls. Root added the explicit header; the reviewer's namespace finding was
repaired by qualifying both calls. No runtime failure is implied by this compile
failure. Retry 64057 builds both targets successfully. The final historical-data
test clarification then exposed the same missing direct header in its own test
file (62149, `UP059-acquisition-test-final-build.log`); that include is now
explicit too. Final test retry/native execution remain pending; preserve both
initial failure logs rather than treating the intervening successful link as
proof of the final test source.

Final build 30330 succeeds. Initial focused native 75561 runs 31 cases, with
29 passing and two fixture failures: the new actual Guild test used the no-town
fixture, and historical Countermage selected Advanced without its Basic perk.
Use `startGame(true)` for an actual town and select Basic before Advanced.
No production admission defect was exposed. Preserve `UP059-acquisition-focused`
log/XML and retry the corrected cases plus the full scoped filter.

Fixture rebuild 18175 succeeds; native 3064 passes 36/36, zero skips, on
`dd7c39ce4bea3388621a149a7d6bcc8562f52b3c53925b77a42513e3ce05825b`.
Retain `UP059-acquisition-fixtures-retry1-focused.log`/`.xml`. This proves
the existing bounded admission and compatibility slice; the subsequently
identified random-reward default-pool gate still needs its own build/run.

Final random-pool/client/test build 73711 succeeds. Native 27381 passes 42/42,
zero skips, on
`e5a3ed12075a54ed827d1acb60c84b2b647263528ff33fc90eb17592f068f14e`.
Retain `UP059-acquisition-final-focused.log`/`.xml`; the default pool excludes
specialty spells while explicit names and historical admission remain intact.
Offline gates pass 76/76; module/diff checks pass; review has no blocker.
No playable/profile promotion or Windows completion claim follows from this
Linux native gate.

### UP-061 activation inventory gate, 2026-09-30

Initial offline command: `python3 -m unittest tools.tests.test_new_horizons_perk_data
tools.tests.test_new_horizons_content tools.tests.test_new_horizons_ui_perk_inventory`.
On the Misfortune working tree above base `4e6bb51e2`, 77 cases ran with three
failures: expected planned status for Weaver, a consequent incomplete collected
perk count, and the stale planned UI inventory row. This is activation-manifest
drift, not a discovered gameplay failure. Add Weaver to the explicit active
allowlist and mark its inventory active with bespoke art still Not done.
The repaired offline gate passes 77/77; native/build results remain separate.

### UP-061 initial native compile, 2026-09-30

Build session 61712 fails in the new probability fixture: direct Bonus
serialization instantiates forward-declared BonusParameters, limiters,
propagators and updaters. Preserve `UP061-misfortune-initial-build.log`.
Add their direct headers to the fixture, as required by the existing native
Bonus round-trip tests. Production objects compiled up to this point; neither
target's successful link or native execution is claimed. Retry both targets
with a uniquely named log and record the succeeding gate.

Header retry 63744 compiles the Bonus round-trip fixture, then finds the runtime
fixture also dereferences a forward-declared CGameHandler. Add its direct header
in `NewHorizonsMisfortuneTest.cpp`; preserve
`UP061-misfortune-headers-retry1-build.log`. This is a second distinct fixture
include failure, not a production mechanic relaxation. Retry remaining objects
and both target links before native execution.

Handler retry 40475 compiles the runtime fixture, then fails in the AI fixture:
wire `BattleAction::DestinationInfo::unitValue` is an integer ID, unlike the
pointer-valued spell-mechanics Destination; the fixture also lacked the direct
CSpell header. Compare the wire ID directly, include CSpell, and use the actual
battle cost callback rather than the hero-only cost helper. Preserve
`UP061-misfortune-handler-retry2-build.log`. No production rule is changed.

AI fixture retry 22804 links both targets successfully. Root then adds missing
v3 spellbook help so the new effect is not described as legacy -Luck. The new
help assertion exposes the same required direct CSpell include in the runtime
fixture (build 58455); preserve `UP061-misfortune-help-final-build.log` and add
that header. No successful native run is claimed until the final help build.

Final help/header build 86586 links both targets. Native initial 30712 runs
23 cases on `7418a515fcde769d7dba1106d750281681143d698a4b7d4e2bb73560cf76f203`:
20 pass, three fail, zero skips. The new favorable-chance marker lacks explicit
`N_TURNS` duration and defaults to PERMANENT despite its turns field: a real
production lifetime defect. Set its Lua duration explicitly; keep the cast,
Dispel and expiry assertions. The other failures are fixture admission:
the positive/negative Luck case attempts a second hero cast without a fresh
Hero Action, and the defender Dispel setup lacks its spellbook. The direct
`canBeCast` path does not impose an active-stack-side check; no artificial
active-side override is needed. A bounded runtime worker repairs only those fixture setups without
loosening casting rules. Actual detached/accepted AI, Death Stare and seven
existing guards pass. Preserve `UP061-misfortune-initial-focused.log`/`.xml`
and rerun the complete focused filter on the corrected candidate.

Lifetime fixture rebuild 63595 links both targets. Native retry 31261 on
`ce80cbac04aabb114ee1d0115f7a964b51a888dddcdd595da9a2916522a118e4`
passes the formula, chance, Luck and Dispel cases, then exits with signal 11
in `OrdinaryExpiryRemovesBothTimedEffects` before the remaining filter finishes.
Preserve `UP061-misfortune-lifetime-retry1-focused.log`; the interrupted XML is
not a passing report. Diagnose with the isolated `UP061-misfortune-expiry-crash-gdb.log`
before attributing the crash to runtime or claiming native completion.

Isolated gdb 16088 identifies the crash at the fixture's unchecked null Bonus
pointer, not inside game processing. `beginCombat()` already starts round one;
two legal `endRound()` calls correctly expire a two-round effect, whereas the
fixture assumed a setup-to-round-one transition that no longer occurs.
Assert the starting/current rounds, check each pointer before dereferencing,
verify one remaining turn after the first legal round and expiry after the
second. Preserve the debugger trace and repeat the complete focused filter.

Final rebuild 84281 succeeds and native 42389 passes the complete filter:
25/25, zero skips, binary
`01f92c0564da87a2d21e2a471f692f2f95c6af4ce86df7370dbddcba9589629e`.
Retain `UP061-misfortune-expiry-retry2-focused.log`/`.xml`. This proves the
timed-marker repair and legal expiry/Dispel rather than suppressing the failing
assertions. Offline gates pass 77/77; module/diff checks and bounded repair
review pass. Current native gate has no blocker; recorded Phase 2 breadth and
the unanswered innate-resistance classification remain separate.

```text
Failure ID / CI run or local command / frozen source identity:
Observed error and affected stage:
Confirmed cause (or explicitly unconfirmed hypothesis):
Minimal fix / regression test names:
Local result / actual target-platform result:
Remaining gate and next action:
```

### UP-089 scripted hostile-proc compile gate

Late-collateral fixture build fails at its new include: `PacksForServerBattle.h`
does not exist. Preserve `UP089-collateral-build.log`. Root replaces the guessed
header with the actual `battle/BattleAction.h` declaration; no production change
or assertion is weakened. Retry build/native result remains pending.

Retry36367 builds both targets. Principal native run fails before the cast:
the fixture hypnotized its only attacker-side stack, so every active unit is
currently controlled by the defender and Player0's hero action is correctly
rejected by the authoritative current-owner check. Preserve
`UP089-collateral-principal.log`/`.xml`. Add one genuine attacker-controlled
escort with explicit initiative so ordinary turn flow supplies a legal casting
window; include it in the seeded collateral pool and assert its HP unchanged.
Do not override the active stack or bypass player/hero validation.

Retry31690 builds and the repaired principal native case passes (703 ms total).
Activation data gate then catches the still-planned UI inventory row (18/19).
Update that row to Active while preserving art Not done and neutral fallback;
do not treat activation as artwork/visual approval. Repeat the data gate and
the focused native filter on the activated candidate.

The first inventory repair transposed its UI/Art columns, so the neutral-fallback
guard correctly still fails18/19. Root corrects the column order (UI Provisional,
Art Not done). Preserve the distinction; do not relax the guard.

Both-target build18304 fails in `ServerSpellCastEnvironment`: the new bridge
calls `playerToSide` / `battleGetOwner` on the narrower `IBattleInfoCallback`,
and uses Lua-facing `isAlive` rather than the C++ Unit's `alive` method.
Preserve `UP089-scripted-build.log`. The Lua-count bridge has the same callback
mismatch. Root repairs both with the existing richer callback's checked cast,
retaining one legacy draw for callbacks without that interface, and aligns the
AI Unit query. The delegated repair retry was service-rejected. No gameplay
scope or assertion is weakened; compile and native gates remain pending.

Retry23124 builds both targets successfully (`UP089-scripted-build-retry1.log`).
Native26641 passes39/39 with no skips (`UP089-scripted-regressions.log`/`.xml`);
the new scripted test file is not yet part of this candidate. Keep the failed
attempt as interface-contract evidence rather than attributing it to gameplay.

### UP-093 Fearless principal fixture perspective

Both-target36048 and frozen follow-up build exit0. Principal46396 fails1/5:
the detached controller fixture queried defender Fearless through Player0's
visibility-limited callback. BattleProxy correctly withholds the opposing hero;
live current-controller behavior and the other four cases pass. Preserve
`UP093-principal.log`/`.xml`. Repair the fixture to use the protected controller's
Player1 perspective and assert visible ownership. Do not remove hero visibility
checks or weaken controller/branch-isolation assertions. Repaired24535 builds
both targets; principal13527 passes5/5. Activated30226 passes27/27 with no skips
in9.008s; data/inventory19/19 pass. The changed branch asserts ownership and that
the opposing hero remains hidden, rather than requiring a hidden hero pointer.

### UP-096 focused integration findings

Client70423 and combined42241 exit0. Principal18354 passes4/4. Activated21661
runs11 cases,9 pass; two existing Archery fixtures throw before their damage
assertions: ArmorPiercingAndHighArcModifyOnlyTheirAuthoredRangedTerms and
NullkillerProjectsDeadeyeForOnlyTheFirstMarksmanStrike report "Earlier New
Horizons perk tier is still required". Their setup directly selects Advanced/
Expert perks without prerequisite earlier-tier perks; UP-096 does not change
perk-selection validation. Retain the failed log/XML. Record coherent legal
progression fixture repair for Phase2 rather than weakening the rule or calling
this entire batch green. All4 new Piercing Bolts cases and4 Surgeon cases pass
in this activated run; coverage of those paths is separate from the two failures.

### UP-095 physical-affliction foundation compile gate

Repaired both-target67115 exits0. Principal native90035 exits139 in the first
foundation case. GDB74245 traces enumerate → acquireState → detached assignment
→ battleFormCreature: UnitInfoMock returns a null unitType. Establish a valid
fixture creature and its lifetime using the existing CUnitStateTest convention;
do not bypass the production state-copy path or weaken assertions. Retain the
crash/debug logs and rerun the17-case gate after the fixture-only repair.
Incremental80241 exits0. Principal retry4916 runs17 cases:13 foundation pass,
all4 Surgeon cases reject fixture setup because it sets legacy capability v3
while retaining current warMachineShop data. Repair the fixture to the current
capability schema without deleting inherited rules or weakening assertions;
actual Tent acceptance remains unproven until the repaired native gate passes.
Incremental24147 exits0. Principal retry37773 passes15/17: all13 foundation,
no-perk healing and actual stored-Poison cleansing pass. Two fixture preconditions
remain: priority test asks for healing output before wounding its target (0),
and the negative-target test has no wounded friendly recipient so the Tent is
automatically skipped. Verify those contracts and repair setup, not production
validation. Keep four-heal priority and rejected/no-heal/enemy assertions intact.
Final incremental64786 exits0; principal29476 passes17/17, activated59354 passes
34/34 with zero skips. The fixture-only repairs preserve all principal/negative
assertions and were independently reviewed without a blocker. These are the
first successful actual-healing and activated gates for this slice.

Combined retry72892 exits1 (`UP095-build-retry2.log`): the actual Surgeon fixture
uses CGameHandler, BattleProcessor and SetStackEffect through forward declarations.
Add their direct headers in the fixture without changing assertions or gameplay.
The foundation fixture now compiles; native acceptance remains pending until the
combined targets build successfully.

Both-target31895 exits1 (`UP095-build.log`): PhysicalAffliction.cpp iterates and
queries BonusList while its existing Unit/Bonus headers provide only a forward
declaration. Root adds the direct BonusList.h include; no mechanic or assertion
is weakened. Retain the failed log and require the repaired target build and
focused native gate before claiming acceptance. Retry remains pending.

Retry88256 exits1 in the foundation fixture: parseBonus requires JsonBonus.h,
the marker-only remove overload needs an explicit vector, and STACKS_ATTACK is
not a registered BonusType. The tester adds the missing declaration, disambiguates
the call and uses an unrelated valid STACKS_SPEED effect; assertions are unchanged.
No production failure is inferred from those fixture compile errors. The actual
Surgeon fixture's callback type and custom-source declaration were independently
repaired before registration. Both fixtures are frozen for the next combined build.

### UP-118 Earthquake legacy-fixture conversion gate

Both-target build86453 exits1: the AI helper calls a nonexistent Mechanics
getMode accessor. Use the declared cast-mode member/API and rerun the compile
gate; this is an interface integration failure, not accepted AI behavior.

Retry11522 reaches the new actual-cast fixture but fails compilation: SPEED is
not a registered BonusType, battleGetSpellCost takes a Spell pointer rather than
an ID, and HypotheticBattle exposes obstacles via its callback rather than a
public vector. Repair those test API uses with their real declarations and
preserve the assertions. Native acceptance is pending; no coverage is added.

Retry17282 builds both targets successfully. Principal38004 runs58 cases in
7.090s:51 pass,7 fail. Five old v2-rule fixture cases still inherit v3 Mass
variant metadata, a prior synthetic-conversion omission. Two Earthquake field
cases assume unranked damage38 while their hero has Basic115% School scaling
(actual39). The field case also reports an allegedly immune friendly stack
taking damage; establish the bonus/level/receptivity preconditions before
classifying or repairing it. All siege/Geomancer and terrain foundation guards
pass. Do not activate Geomancer until the repaired principal gate passes.

Diagnostic82464 runs31 in3.499s:29 pass. Its new preconditions prove the friendly
stack has level3 immunity and is non-receptive, but the cast still damages it.
Root traced Effects::prepare: custom transformTarget must apply receptivity;
filterTarget is not automatically invoked there. Fix only the Earthquake
transform while preserving its terrain anchor. The AI signed-projection case
also sees defender value0; verify defender's legal casting preconditions before
changing AI side semantics. Actual AI-selected paid siege cast already passes.

Repaired60626 passes58/58 in7.056s, including the now-proven immune-target
exclusion with terrain preserved. Final build22190 exits0. Activated98184 runs
63 in8.383s:62 pass; the defender structural-projection fixture still reports0
despite a legal live target. Investigate the projected callback's player
perspective/hero state and do not weaken the signed-value assertion. All actual
casts, Geomancer, AI-selected paid siege, saved-profile and terrain guards pass.

Read-only trace identifies the remaining fixture error: HypotheticBattle
inherits its subject's player perspective; a Player0 callback cannot preflight
a Defender cast. Use a Player1 subject for that forecast, assert detached side
and legal target, and keep the negative structural-value assertion. No
production permission boundary is relaxed.

Final perspective-fixture rebuild80978 exits0. Activated retry91000 passes
63/63 in8.411s, zero skips; the signed Defender projection now uses its own
player perspective. Actual AI-selected paid cast remains green. Python data,
schema and inventory35/35 and generated-module drift check pass. Independent
Astra review has no blocking finding; special Metamagic-event forecast modifiers
are deferred to Phase2. Verified native binary SHA-256:
`fd3227d571340042be8ee857c94891c7f97942ea775f783f2dea3ad25a0e9191`.

The first focused Python gate ran34 tests with6 errors: synthetic v2 snapshots
retained the newly added v3-only earthquake object and were correctly rejected
by the strict v2 schema. Strip that field when deriving legacy fixtures, including
native fixture adapters; do not relax the old schema. Production v3 validation
and actual spell acceptance remain separate gates.

No credentials, workstation paths, purchaser content or raw research dumps in
these notes. Keep historical failures even after repair, but label their scope.
Do not claim the pipeline is future-proof: tests reduce recurrence and catch more
failures earlier; new dependencies and environments can still reveal defects.
