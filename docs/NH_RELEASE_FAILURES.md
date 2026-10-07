# New Horizons — release failures and regression lessons

## Purpose

### 2026-10-07 — Initiative marker used image-only visibility API

UP272's first combined build11477 fails in StackQueue because CLabel has no
`visible` field. CAnimImage's visibility API is not interchangeable with text
widgets. Use CIntObject's `setEnabled` for the label instead; do not hide this
compile failure behind passing source guards or pure-helper tests. Preserve
build.log and the subsequent build-repaired.log under ignored testing/
queue-origin-20261007.nyYSMQjB. Executed client dispatch, event locking and
rendered marker placement remain separate Phase2 checks.

### 2026-10-07 — Adjacent Wild Chance fixture bypasses prerequisite tier

UP271's eight-case Luck check passes7/8; WildChanceScopesSylvanLuckToNatureSummons
throws "Earlier New Horizons perk tier is still required" before its Wild Chance
behavior assertions. Its fixture promotes Sylvan Luck directly to Advanced,
then selects wildChance without selecting a Basic perk. UP271 changes only
client presentation, not progression or the tested production effect. Keep the
failed native-luck.log/XML under ignored testing/spell-cost-20261007.fvX8ZAvh.
Phase2: repair this fixture through legal prerequisite progression and rerun
the Nature-summon behavior; do not relax runtime validation or infer a passing
summon test. The seven direct readback/target-neutral cases are separately run
as native-luck-principal. This non-blocking fixture issue does not consume the
Phase1 implementation loop.

### 2026-10-07 — Quick-recruitment UI missing direct includes

UP268's first client build93863 fails because new consumers depend on declarations
not provided by the existing translation-unit includes: GameLibrary/global
LIBRARY, general text, hero usesRules, Resource and CCastleInterface. Add direct
headers rather than relying on another source file's or precompiled header's
transitive includes. Rebuild87978 succeeds. Preserve both logs under ignored
testing/quick-recruitment-20261007.3AUTx3Xr. Source guards passing alone did not
prove compilation. Source review also caught discarded stat-icon ownership,
wrong label anchors and assumed16px sizing for native48x32 buttons before build;
retain ownership and use verified-size existing controls.

### 2026-10-07 — Reflection fixture assumed zero casualties

UP266's first three-case native run passes the non-reflecting and fully
Guardian-absorbed controls but fails the ordinary reflection fixture's arbitrary
zero-casualty assertion: the actual resolved injury kills four Angels. Its exact
attributed log and HP-loss assertions already pass. Capture the pre-hit creature
count and compare packet casualties with actual count loss instead; retain the
nonzero result, exact log, names and HP checks. The failed principal log/XML are
preserved in the isolated bulwark-log-20261007.DFu6TN2q test profile. This is a
fixture correction, not a gameplay or damage-formula change.
The repaired five-case focused run passes with zero skips in1.767s, including
the two adjacent Vengeful Mire/Toxic Spines cases. Source review also caught
missing `%s` tokens before `addNameReplacement`; these were repaired before
the first native run. Exact creature-name assertions prevent recurrence.

### 2026-10-07 — Stale active-perk expectations in the data fixture

The focused registry-inventory test initially fails on Armorer and Battlecraft:
its explicit active whitelist omitted the already source/native-verified Last
Stand and Battlefield Mastery. Those failed subtests also leave a misleading
307/310 accumulated-ID assertion. Add the two evidenced IDs, alongside the newly
verified Perfect Rhythm, rather than relaxing status/total assertions. All three
focused source-identity, complete-registry and module-parity tests then pass.
Future activation commits must update this fixture's explicit expectation set.

### 2026-10-06 — Stale canonical-specification hash in the perk registry

The focused source-identity test fails because the registry still records the
previous document hash after b9fe600c4 changed Last Stand/Battlefield Mastery
wording. Git history confirms the current canonical bytes were already committed;
this is not an uncommitted user edit or line-ending workaround. Update only the
registry hash to the actual canonical SHA256 and regenerate curated module
metadata. The same focused test and module drift check then pass. No perk behavior
or coverage changes from this metadata repair. Future specification edits must
refresh this provenance field and run its focused identity check.

### 2026-10-06 — Last Stand native fixture observation boundaries

Initial native run89965 passes7/12, zero skips. A rescued stack may immediately
receive its next queue activation, expiring Defend and applying Veteran recovery;
the resulting3HP is not the original hit's capped survivor. Capture accepted
attack state and arrange another next stack to verify actual Defend bonuses.
Retaliation tests must not grant BLOCKS_RETALIATION to the acting attacker.
AI keeps that actor in attackerState, not affectedUnits. Isolate Last Stand
descriptor markers from existing Veteran-history rejection. Fixture repair19224
also catches CUnitState::save being nonconst; use a mutable test state reference.
Final build23204 and principal13/13/activated22/22 pass, zero skips. None of
these fixes weakens the intended production survival or termination assertions.

### 2026-10-06 — Last Stand fixture typed source and retaliation setup

Client/test build11858 stopped in the new Guardian fixture because
BonusSourceID's variant needs SpellID(SpellID::HASTE), not the raw enum value.
The same checkpoint's independent review catches BLOCKS_RETALIATION accidentally
given to the acting attacker in the own-player lethal-retaliation case. Remove
that grant rather than weakening the survival assertion. Both are corrected in
serialized retry45899; logs remain in last-stand-20261006.Y5mzBuFn.
That retry stops at the AI fixture's missing defining include for testHeroRules;
Root's first include correction incorrectly guessed SpellPointTestUtils.h;
retry73215 proves the symbol is still missing. Locate the actual definition:
test/hero/NewHorizonsHeroRulesFixture.h. Include that defining header explicitly,
not an unrelated neighboring helper. Preserve both failure logs.

### 2026-10-06 — Last Stand shared interface and detached-state semantics

Client build71389 fails in applyBattleEffects because getBattle() exposes
IBattleInfo, which has getActiveStackID(), not the concrete activeStack member.
Use the existing interface rather than casting away the abstraction. The retry
is49003; both logs remain under testing/last-stand-20261006.Y5mzBuFn.

Pre-build review also catches nextTurn editing acquireState()'s detached copy.
That helper does not return the live state; use the authoritative stack fields
for an actual activation reset, and accepted UnitChanges for fixture transitions.
Do not let descriptor tests pretend that mutating a discarded copy changed the
stored marker. Native acceptance remains required. Other repaired contracts:
zero capped damage is valid for a1HP rescue, carried termination state is not a
new trigger, and SPELL_LIKE_ATTACK excludes its ranged mode, not ordinary melee.

### 2026-10-06 — Battlefield Mastery AI fixture defining include

The subsequent build stops in the new AI fixture at levelUpHero/sendAndApply:
CGameHandler is only forward-declared through BattleTestFixture. Include
server/CGameHandler.h explicitly in that fixture; do not rely on PCH/transitive
includes or remove the legal perk-acquisition path. Full captured retry log:
build/new-horizons-linux/testing/mastery-20261006.afdT3E6Y/build-retry.log.
Focused native execution remains required after the compile repair.

The first native run passes3/4 but the historical-stamp binary check uses
getStack(id)'s default alive-only filter. Binary CStack descriptors intentionally
omit CUnitState/health, so query with onlyAlive=false and assert descriptor
identity, historical stamp and absent consumed marker. This does not certify
midcombat save restoration; active markers still fail closed. The16 relevant
Battlecraft/Defend-lifetime/Reserve regressions pass with zero skips.

### 2026-10-06 — Battlefield Mastery free-function callback context

The first client/test build stopped in BattleEvaluator's free Defend-scoring
function: unqualified playerToSide is a callback member, not a free function.
Use defendedPreview->playerToSide with that branch's effective owner. Root fixes
the one call and starts the incremental build; do not change controller policy
or fall back to original unitSide merely to satisfy compilation. Focused runtime
and detached tests remain required before coverage credit.

### 2026-10-06 — Private magic-art lookup and native-image validation

Raw image existence checks did not follow the renderer's SPRITES/, DATA/, raw
lookup and could silently disable valid module art. Match resource lookup before
claiming an optional hook works. The client/fixture build and opt-in native check
pass after correction; actual BattleHero lifecycle remains separately unverified.
The first native fixture wrongly required visible glow pixels on every frame;
the supplied final frame intentionally clears the glow. Assert sequence-wide
changes while retaining per-frame geometry and transparent-pixel invariants.
The supplied representative alpha is binary; conditional blend checks are not
evidence of a partial-alpha sample. Do not weaken real assertions or invent one.
Offline TPMAGE reconstruction initially decoded 24-bit PCX as RGB instead of BGR.
Follow the actual loader before judging palette fit. The corrected opaque book
crop still has a visible rectangle and remains visually unaccepted. Clean-plate
composition must export the local67x85 alpha patch, never the full800x600 room
positioned at378,344. Preserve pixels outside restoration/cutout masks.

### 2026-10-06 — Optional magic-art metadata and battle defining include

The first casting-hook compile exposed an incomplete IBattleInfo at the
getMagicRules call. Its definition lives in IBattleState.h. Root's initial
attempt incorrectly guessed an IBattleInfo.h filename; verify defining headers
with rg before patching includes. The repaired client/native fixture build passes.
Independent review also caught nested optional JSON object access before shape
validation: JsonNode const Struct can assert on malformed non-object values.
Validate each object before indexing and catch optional metadata parse errors;
missing/broken optional art must preserve original visuals. The correction is
reviewed and builds. Logs UP263-magic-assets-build/retry/repaired and
UP265-cabir-resource-build remain under ignored build storage.

### 2026-10-06 — Counterpressure fixture requires the skill-handler definition

The first UP180 client/test build fails only in the new server fixture: calling
`LIBRARY->skillh->size()` requires `lib/CSkillHandler.h`, not the forward
declaration in GameLibrary.h. Add the explicit defining include rather than
depending on transitive headers or weakening the fixture. The incremental retry
succeeds. The active-module focused run executes15 cases with zero skips, but
four fail because actual Magic Arrow damage does not arm the new response in
live or detached resolution. Correct actual damage provenance; do not remove
the assertions or misreport the11 passing state/debuff controls as acceptance.
Runtime repair and retest remain pending. Retain both `UP180-build.log` and
`UP180-build-retry.log` in the ignored Linux build directory.
Subsequent correction: the cast-local recorder supplies spell provenance even
when Lua `damageUnit` packets omit SPELL_EFFECT/spellID. Positive recorded damage
and opposing-side checks fix live arming. Retry passes14/15; the remaining test
attempts an enemy cast through an attacker-only callback, so general legality
correctly rejects completion despite castEval applying prepared effects. Use the
existing spectator callback and assert legality before both projected casts;
do not weaken production visibility. Final client/test build and native15/15
pass in2.099s, zero skips/errors. Keep all three native receipts; no-op policy
and full activation remain unresolved.

### 2026-10-06 — Cabir candidate profile overlap and known schema diagnostics

The first candidate smoke chooses a profile below the repository, itself below
the original installation root. The launcher correctly rejects overlapping input
and writable-profile paths before execution. A new /tmp parent with nonexistent
profile child fixes the test setup; do not bypass the overlap protection.
Corrected20s true-headless smoke reaches day8; timeout124 is intentional, with
owned process/runtime gone and profile lock released. Snapshot30bc6ea from
360b91d06 is subsequently promoted only after independent review and focused
native13/13 plus resource1/1 success. The four cabirRepair schema warnings reject
the effect discriminator field `type`; actual authoritative repair tests pass.
Keep this non-blocking schema integration finding for Phase2, alongside existing
Shield of Chaos neutral-default and redundant namespace diagnostics. Do not
describe the smoke as a completed match, rendered acceptance or proof of no bugs.

### 2026-10-06 — Cabir portrait guard assumed no other graphics fields

The combined Cabir map/battle/portrait content check found two failures in a
legacy portrait guard: it compared the entire graphics object to only iconLarge
and iconSmall, despite dedicated battle and map graphics already being present.
Scope that assertion to the exact icon-field projection and retain byte/size
parity. Dedicated animation and map checks cover their own fields; do not delete
those settings to satisfy an obsolete portrait-only shape. The revised focused
combined suite passes31/31. Native map/encounter/template checks also pass1/1,
zero skips. A map-preview fixture dimension expectation was corrected separately;
neither test issue required changing artwork or gameplay.

### 2026-10-06 — Incoming-element packet lambda capture

Build18091 and single-object reproduction60457 fail in SetStackEffect.h: a
capture repair was applied to containsNoQuarter instead of containsBonusType.
The former referred to an absent type; the latter still failed to capture it.
Root restores NoQuarter's captureless lambda and adds the value capture to the
specific type-filter lambda. Inspect contextual diffs, not an unqualified first
matching lambda replacement. Retry and focused native acceptance remain pending.
Retry96133 reaches the new native test object, then fails because its active-mod
check dereferences forward-declared CModHandler without its defining header.
Root adds the explicit CModHandler include; a fixture's transitive includes are
not an API guarantee. Next build remains pending at this entry.
Subsequent builds35239 and93239 succeed. Activated focused execution runs nine
cases with zero skips: the five incoming-element cases and Magi melee-penalty
case pass, while all three Repair cases fail. Eligible targets are rejected and
the health forecast is zero. Keep the repair assertions intact and trace actual
Lua/content execution before claiming the ability works. Receipt: ignored
UP253-cabir-mechanics-native.log/XML under the Linux build root.
Trace identifies a fixture mismatch: injected units use the summoned placeholder
slot even when UnitInfo.summoned is false. Repair intentionally rejects that
slot. Replace those fixtures with original army-backed stacks; do not remove
the production exclusion. Fixture rebuild70255 then catches a const-pointer
return mismatch in the new lookup. Use the authoritative mutable stack lookup,
and keep unusable-remains controls friendly so ownership rejection cannot mask
the provenance assertion. Separate adjacent creature-spell controls pass3/3
for Puppet Master ownership, Sanctuary removal and Hold Fast continuation.
Fixture retry97219 builds. Native retry passes9/10: real healing, permanent
restoration, forecast parity, four-species targeting, friendly unusable-remains
and phantom healing all work. The remaining failure is production: an exhausted
creature cast request still resolves because target mechanics do not enforce
CASTS availability. Root adds the existing stack.canCast() check before random
spell preparation/StartAction, covering charges, suppression and per-activation
casting restrictions without a Cabir-specific branch. Keep the exhausted-request
assertions; rebuild and rerun remain required.
Build6174 succeeds. Its first combined13-case run passes11; two pre-existing
synthetic spellcaster fixtures lack CASTS. Add the explicit allowance to those
fixtures and assert cached total refresh; do not weaken the authoritative gate.
Fixture build31314 succeeds; final activated13-case run passes13/13 in3.478s,
zero skips. Four content guards and module drift also pass. Actual Repair and
the three adjacent creature-action controls are verified; no GUI/playable-art
acceptance is inferred. Final receipt: UP253-cabir-final-native.log/XML.

### 2026-10-06 — Split-owner geometry lambda capture

UP261 build90062 fails because ownerMarkerY is moved from inside a captureless
lambda to its surrounding scope and then forwarded into make_shared. That
forwarding odr-uses the local constexpr, so it needs an explicit value capture.
Root adds `[ownerMarkerY]`; retry55262 builds client/native successfully and
the focused seven-case filter passes with zero skips. Preserve the geometry and
callback semantics; do not duplicate constants or weaken build gates. A source
guard/review alone did not catch this compilation defect.

### 2026-10-06 — Hovered-frame source-description formatting

UP260 caller tracing shows updateHoveredStacks refreshes an unchanged inspected
stack every frame. UP257's source-list equality prevented widget rebuilds but
not repeated Bonus::Description formatting. This is a source-proven avoidable
cost, not a reproduced cause of the user's previous game-lag incident. A
per-panel unit/bonus-tree-version/player-callback key now guards both Morale and
Luck formatting, after the saved-NH gate. Shared values still refresh in the
existing path; no new polling loop/state/global scan. Both-target build76103,
14/14 focused native cases, source guards and independent reviews pass. The
native key case tests key equality, not actual GUI call counts or FPS. Rendered
performance and name/visibility changes without a version/callback change
remain Phase2 validation/invalidation work.

### 2026-10-06 — Morale readback direct-header integration

UP257 client build25692 exits1 because StackInfoBasicPanel uses GAME without
its declaring GameInstance header. The player-interface and concrete CCallback
headers are also required for scoped Bonus::Description conversion. Add those
direct headers rather than weakening visibility or changing callback types.
Retry39366 builds client and native runner successfully. The exact six-case
native filter passes6/6 in3.639s, zero skips. The failed gate remains recorded;
no assertion, gameplay rule or visibility boundary was weakened. Rendered UI
and delivery acceptance remain separate.

UP258 focused School text/effect checks pass. The broader Skill-entity file
retains six unrelated subcase failures in
test_non_school_skills_use_canonical_rank_text_and_declared_effects:
Spellcraft and Diplomacy's three ranks are active while its test-only expected
registry still classifies them as planned. Preserve this Phase2 test-maintenance
finding separately; do not claim the full file passes or revert live ranks to
satisfy an outdated assertion.

### 2026-10-06 — Candidate smoke omitted explicit headless mode

The exact adf92c01d candidate536e86 passes its2278-file manifest check, but
the dummy-SDL invocation omitted --headless. It loads the map and opens battle
UI before repeating missing-player-red and invalid-battle-side diagnostics;
bounded shutdown requires exact-child containment. This is not a passing smoke.
Source inspection finds a possible nonparticipant spectator callback route:
testmap enables AI-only play, and without headless the client auto-enables
spectator UI. Do not attribute the error to economic help or relax battle-side
visibility without a producing callstack. Normal4af281d5 remains selected.

One bounded managed debugger attempt cannot attach under the host's ptrace
restriction. No callstack is captured; it is stopped before the first error and
is not a reproduced diagnostic. No permission change or repeated attach is
attempted. The exact owned wrapper/client are gone, private profile lock is
released and candidate hashes remain unchanged. Private receipts are retained
under `build/new-horizons-linux/nh-up256-debug-tdtQGp/`; original failed smoke
receipts remain under `nh-candidate-smoke-536e86-IkNA8q/` in the same build root.

The documented scenario-smoke harness already passes --headless. Candidate
documentation now spells out equivalent explicit client/resources, fresh
private profile, dummy SDL and correctly separated --savefrequency 0 arguments.
Independent review finds no command/permission blocker. A corrected true-headless
candidate run is assigned separately; do not treat the documentation review,
attach failure or earlier timeout as playable acceptance.

Corrected headless-path receipt: one20-second run advances13 turn starts through
day5,12 NK2 cycles and BattleAI, without either target diagnostic. Timeout124
is deliberate and followed by verified client exit, unlocked profile and removed
runtime links. The full client log retains Shield of Chaos's default-NEUTRAL
positiveness warning. Manifest and hashes remain unchanged. Independent review
finds no demonstrated Phase1 promotion blocker; root selects the same536e86
snapshot and normal-launcher verify-only passes. This is development-candidate
delivery, not rendered/manual acceptance or a spectator diagnosis. Keep the
unattributed spectator failure and warning as Phase2 findings. Receipts:
`build/new-horizons-linux/nh-up256-headless-H9DlTa/`.

### 2026-10-06 — Flank focused control compares casualties against full strength

UP245's client/native builds pass and its exact seven-case run82491 is6/7,
zero skips,2.053s. Both new projected-position cases, wide geometry, actual Flank,
Formation Fighting/Combined Arms and direction formatting pass. Existing
EncirclementChangesOnlyAdditionalSideDamageAfterARecordedFlank fails pre/post
damage equality5900 versus4897. Root traces its blockRetaliation(defender)
setup: that helper grants BLOCKS_RETALIATION, while authoritative attacks and
forecasts check this bonus on the attacker. Attacker casualty count is not yet
directly observed; do not claim a numerical casualty diagnosis from damage
alone. The tester owns a narrow fixture correction with explicit count evidence;
do not weaken side/bonus assertions or mark the initial run green. Receipts:
ignored `build/nh-flank-readback.xSWIhS2Q/native.log` and `native.xml`.

Repair evidence: all three strike fixtures now block retaliation on the acting
attacker and explicitly assert unchanged attacker count. Independent review
finds no weakened damage/history assertions. Repaired client/native build83993
passes; correct-bin execution98399 passes7/7, zero skips,2.325s. Preserve initial
failure receipts alongside `native-repaired-bin.log/xml`.

The intervening root invocation from repository CWD loaded the wrong development
resource root, omitted New Horizons and produced two resource failures/four
skips. It is a harness failure, not fixture acceptance. Restore the isolated
test preset's New Horizons entry and run from `build/new-horizons-linux/bin`,
where development resources are registered. Retain `native-repaired.log/xml`;
never accept a green test subset after missing-content skips.

### 2026-10-06 — Nonfatal Dispel preview invalid-spell diagnostics

The exact bea86a2c3 frozen Linux candidate passes file verification and its
20-second dummy-SDL All for One smoke through AI day7, but emits three paired
`spell id -1` / `DISPEL.transformTarget` invalid-index diagnostics during AI
evaluation. Execution continues; no crash/assertion/server-rejection marker is
observed. The trace does not yet establish the invalid source or affected
Dispel behavior. Track recipient/bonus provenance and transformTarget handling
for Phase2; do not infer a root cause or call the smoke clean. This bounded
finding does not itself establish corruption or a foundational regression.
Ignored receipts: `build/nh-action-sequence.QSR769/candidate/run.4pT5lm/`.
The previous ammunition diagnostic is not observed in this seed/run, not proven
fixed. No host display/input or user-profile mutation occurred.

### 2026-10-06 — Hero Action sequence synthetic reader

UP244's first native rebuild failed because MalformedStateReader assigned its
scalar integer input to every template field, including the new fixed-size
action array. Even a runtime-disabled serialization branch must instantiate a
valid array operation. Preserve the production history representation; repair
the synthetic reader to handle the array rather than weakening serialization
or removing malformed-state coverage. Client build passed before this fixture
compile failure. The repaired array reader is independently reviewed; matching
client/native builds pass with12 jobs and30/30 focused cases pass, zero skips,
in2.319s. Receipts remain under ignored `build/nh-action-sequence.QSR769/`.
Do not run a stale runner or treat source review as execution acceptance.

### 2026-10-06 — Roof candidate headless CLI harness

The first frozen-roof smoke invocation used unsupported `--savefrequency0`
instead of the supported two arguments `--savefrequency 0`. It never reached
the test map and timed out in the error path; this is a harness failure, not
evidence of a candidate gameplay regression. Preserve the initial and corrected
logs separately under ignored `build/nh-up241-validation/`. Killing the timeout
wrapper alone initially left its child client; the tester identifies the exact
fresh-profile process before terminating it and must confirm cleanup before
accepting or starting another run. Reuse verified CLI syntax and inspect child
processes, not only wrapper exit status.

### 2026-10-06 — Sylvan Luck client battle-interface include

The first client compile dereferenced IBattleInfo with only its forward
declaration available. Add lib/battle/IBattleState.h explicitly rather than
depending on unrelated transitive includes. Repaired client/native builds
pass, and eight focused presentation/adjacent cases pass with zero skips.
Initial/repaired logs remain separate under ignored
`build/nh-sylvan-readback.M9yNqddv/`. Synthetic test Luck inputs include the
ready Gambler bonus; assertions match actual temporary-Luck/expiry wording.

### 2026-10-06 — Authored rewardable pre-visit tooltip key

The first required training-state UI native run passed six perk cases but failed
the Brotherhood case: available text resolved to an internal localization key.
Rewardable::Info registered authored notVisitedTooltip under notVisitedText,
while configuration consumed notVisitedTooltip. Registering the canonical key
alongside the historical alias repairs readable output without changing reward
rules or explicit @references. Repaired build and7/7 focused cases pass, zero
skips/errors. Initial/repaired evidence stays separate under ignored
`build/nh-development-ui.AL3Xx2lM/`. Do not weaken text assertions to accept keys;
trace registration and consumption identities when exposing authored messages.

### 2026-10-06 — Chain Lightning preview rendering include

The first frozen client build failed because the new hop-label renderer used
Colors without its explicit header. Added render/Colors.h, render/EFont.h and
gui/TextAlignment.h rather than relying on transitive declarations. The repaired
client/native build passed, as did15 focused presentation/mechanic cases with
zero skips. Initial and repaired build receipts are separate under ignored
`build/nh-chain-ui.VHicYRM1/`; this is not rendered or playable UI acceptance.
Keep explicit rendering dependencies when adding drawing to a controller that
previously needed only image/render-handler interfaces.

### 2026-10-06 — Arcane Reservoir manual AI visit gap

The unique-building audit finds that the Reservoir's manualHeroVisit setting
excludes it from automatic town-entry rewards, while Nullkiller2 never requests
visitTownBuilding. Human/source weekly availability is not minimum AI coverage.
An explicit normal request in the AI town-interaction path and actual Buffer/
Normal acceptance are in progress. Source inspection also finds the earlier
weekly server fixture still asserts the obsolete twice-maximum total. Replace
those assertions with unchanged Normal, exactly50 Buffer and their combined
total; no production balance or restoration rule changes are authorized.

Production client builds5 steps; the repaired existing weekly server case
passes1/1 in0.653s, zero skips/errors/disabled. The new AI fixture's first
compile catches an incomplete PlayerState in resource setup: include
lib/CPlayerState.h explicitly, as the adjacent Adventure Spell unlock fixture
does. Preserve `reservoir-ai-test-build.log` and its distinct repaired successor;
AI execution acceptance is still pending at this correction checkpoint.

Repaired successor: new fixture increment builds3 steps; combined current
filter passes6/6 in2.027s, zero skips/errors/disabled. It includes actual AI
town-entry interaction/validated manual request, unchanged Normal/+50 Buffer,
repeat suppression, unbuilt/inactive controls, the weekly reset case and three
adjacent Guild unlock cases. `reservoir-ai-native.log/.xml` are new receipts;
the first compiler log and earlier weekly receipt remain intact. Source review
finds no blocker; full autonomous town-goal valuation and wider visit/resource
composition remain Phase2, and playable/graphical acceptance remain separate.

### 2026-10-06 — Castle Gate AI first-use coverage

The principal-path audit finds no Nullkiller2 gate planning or execution. The
new explicit action uses the normal CastleTeleportHero request. Integration
inspection also finds occupied destinations previously consume Movement/use
without relocation, a live-day filter using stale source turns after rollover,
and gate Movement costs incorrectly applied to spell edges from a gate town.
Source corrections and bounded server/AI fixtures are in progress.

A premature server-only compile encounters CMake registration before the new
action file exists; this is an integration checkpoint race, not a finished-source
compiler defect. Wait for the explicit worker source freeze even for a narrow
target, because regeneration reads all registrations. The first frozen client
build then fails on nonexistent CGTownInstance::getNameTranslated in debug text;
use getNameTextID, matching TownPortalAction. Preserve distinct original/repaired
logs. Repaired client build passes53 steps; focused native acceptance is pending.

Test-target build passes29 steps. First native principal run is1/2, zero skips,
in1.327s: actual server gate travel/rejection/day reuse passes, while the AI
fixture wrongly expects New Horizons Dimension Door to retain Movement and its
gate request does not complete. Preserve `castle-gate-native.log/.xml` before
repair. Trace the request rejection and fixture world state; do not weaken
command/query validation or equate the generated route with executed travel.

Repaired acceptance: the AI fixture now retains a live opposing town/hero and
asserts the acting player survives source-town setup. A sole-player fixture can
end the game during town visitation, leaving its subsequent request ineligible;
the original rejection is not separately captured, so this is a source-backed
fixture correction, not a claimed logged server rejection. Review catches a
second setup mistake: TinyH3MBuilder's chained hero setters configure its last
object. Append the opponent only after the tested hero's army/stats/spells/book
are configured. Remove the invalid positive remaining-Movement assertion for
Dimension Door; retain its separate spell-edge classification assertion.
The repaired current binary passes2/2 principal tests in1.509s and9/9 adjacent
pathfinding tests in0.903s, zero skips/errors/disabled. Original/repaired native
receipts are distinct; each fixture-only increment builds3 steps. Source review
has no remaining blocker. Full autonomous goal selection/compound paths and
graphical delivery are deferred, not established by this focused run.

### 2026-10-06 — Astronomy construction forecast

The town-building audit finds a first-use gap: construction never authors the
preview until NewTurn, so a last-day Tower build cannot reveal the upcoming
week before it begins. Carry the forecast in the authoritative NewStructures
packet, before the existing client building refresh, without advancing time.
Review also identifies a day-zero forecast reroll on the initial NewTurn;
preserve a known forecast there while advancing it at ordinary week boundaries.

Initial focused client build fails because the new packet template names
ESerializationVersion without a guaranteed declaration. Match adjacent packet
templates with `Handler::Version` rather than relying on incidental includes.
The original failure log is retained as `astronomy-construction-build.log`;
the repaired client build passes. Test compilation then catches two fixture
uses of `.getNum()` on the enum constant `BuildingID::DWELL_LVL_1`; wrap the
constant in BuildingID before reading its number. The original test-build
failure and incremental successor have distinct logs. Repaired increment passes
100 steps; principal focused acceptance passes7/7 in0.919s with zero skips.
No playable promotion is inferred from these source/native corrections.

### 2026-10-06 — Rebirth metadata fixture acceptance

Review catches an older-format CMemorySerializer buffer read as CURRENT.
The buffer carries no version header: the reader must use the exact writer
version to test legacy default-zero behavior. After the full build, fix the
reader to PREEMPTIVE_STRIKE and rebuild. A valid-profile focused run then finds
a second fixture assumption: the seven-Peasant Basic Rebirth produces1HP,
so the planned nonlethal injury is impossible. Use17 source creatures in that
test, without changing production HP rules. Repaired focused25/25 passes with
zero skips. Preserve the valid24/25 failure receipt alongside the successor.

Harness lesson: isolated XDG paths must be absolute when changing cwd to the
test bin directory. One wrong-profile attempt used bin-relative paths and
its receipts were accidentally overwritten by the corrected-profile run.
Do not count it as gameplay evidence or reconstruct a claimed receipt.

### 2026-10-05 — Windows libiconv source download timeout

Successor receipt2026-10-06: full run `37405475795` at exact repair source
`975c6f011445049690b39c501128684a0cc64e5b` completes successfully, including
compilation, package closure/license/source checks and downloadable-artifact
upload. Artifact `11389625197` carries that exact source in its name. This
resolves the observed download blocker; it is not Windows gameplay acceptance
and excludes later Rebirth/Astronomy changes. Historical failure details follow.

Full Windows run `37403632207`, source `9c7c4880f`, failed before compilation
in the complete dependency graph/source archive preflight. Both configured GNU
endpoints timed out downloading `libiconv-1.17.tar.gz` after bounded retries.
The matching cheap notice run `37403527818` passed; that does not prove source
archive availability. Retained artifact `Windows-preflight-reports-9c7c4880f00eaaeb722abdd3ea034025af81c241`
contains the preflight evidence. No C++ compiler failure is established.

Repair in progress: seed the existing content-addressed Conan source cache from
official GNU mirrors with the exact Conan recipe SHA-256, following the dav1d
seed pattern. Do not trust a newly computed mirror hash, weaken source/license
gates, change dependency versions, or blindly retry the unchanged workflow.
Repair source gate: both mirror downloads match Conan Center Index's libiconv
1.17 SHA-256 `8f74213b56238c85a50a5329f77e06198771e70dd9a739779f4c02f65d971313`.
The existing cache helper re-verifies bytes and rejects corrupt entries. All
92 package regressions pass with zero skips, including the new workflow guard;
YAML/Bash syntax and independent review pass. Windows runner execution and a
successful successor remain unproven. No dependency version or gate changed.

Reviewed repair pushed as `975c6f011`. Cheap notice successor `37405378013`
succeeds on that source; full build `37405475795` is confirmed in progress.
Its source-cache and complete preflight steps pass; client compilation is active.
Retain exact-source identity and inspect its terminal result before retrying.

### 2026-10-05 UP239 — Repeated renderer fixture shutdown

The four-portrait dummy-SDL fixture initially segfaulted during its scale-4
iteration after passing scales 1–3. A GDB rerun and ten unchanged repetitions
passed; no crash backtrace was captured. Do not claim a confirmed crash cause.
Source review nevertheless finds an unsafe fixture lifecycle: queued SDL scaling
tasks read global ENGINE, while unique_ptr reset clears it before destruction
joins those tasks. Production EntryPoint already drains async work before reset;
the fixture omitted that established shutdown contract. Add the same drain at
each scale transition and exceptional cleanup, retaining asynchronous scaling
and all foreground/background/alias pixel assertions. No production scaling or
portrait logic is weakened to make the test pass.

Separate Phase 2 finding: SDL2/SDL3 createScaled ignores its integerScaleFactor
argument and reads the live global screen scale inside its worker instead.
Track that coupling separately; it is not a proven cause of this crash. Private
GDB and initial runtime evidence remain under ignored build validation outputs.

### 2026-10-05 UP237 — Built-town icon recursion at scaled UI

Academy Town Hall construction succeeds in the simulation, then crashes the
client while refreshing the built-today town-list icon. Loading a compositor's
original DEF frames at nominal scale 1 is insufficient to avoid recursion:
`ScalableImageShared` eagerly loads the current screen's scale. At scales 2–4,
the scaled DEF lookup resolves the faction's animation alias and re-enters the
same compositor before its cache entry has been stored. Repeated normal-image
loads and a runNetwork stack-overflow-shaped kernel fault identify this path.

Original-frame locators must retain their raw-frame intent through scaled loads
and have a separate cache identity. Both SDL backends now bypass aliases and HD
substitution for these locators, scaling the decoded native pixels instead.
Headless new-game initialization cannot cover this client-renderer path; require
the focused dummy-SDL consumer check at screen scales 1–4. Keep user crash logs
and purchaser-native comparison images in ignored validation directories.

Focused runtime verification passes with zero skips: all four built icons at
scales 1–4, expected native dimensions and marker differences confined to the
lower-right region. The standalone fixture links the client/backend libraries;
its initial setup needed the concrete screen interface, a standalone fatal-error
handler and the standard bin output directory for the matching shared library.
These were fixture bootstrap failures, not additional production regressions.
The SDL dummy driver and temporary profile avoid host-display/input automation.

### 2026-10-05 UP235 — Academy resource consumer and validation paths

Animation JSON aliases do not replace siege images loaded through `ImagePath`.
Register generated siege PNGs under the requested SGTW image resource names;
keep original gate resources external. The focused Academy check covers this.

The first headless Academy load starts a game but reports four missing built-icon
images in faction schema validation. Runtime-only generated images are invisible
to that validator. Supply generated-normal-art fallbacks containing no original
badge pixels, and prefer the runtime compositor only for these four names in
both native-base and 1x scaled renderer paths. A filesystem fallback alone would
suppress the badge. The repeated headless resource check loads cleanly without
the Tower validation warning. Logs remain under `build/nh-up235-validation/`.

The old Tower progression guard assumes HALLTOWR DEF-frame aliases. Its failure
after the art import is an obsolete representation assertion, not a lost semantic
swap. Keep all 44 frame checks and the 33/34 and 40/41 swaps, but assert authored
PNG aliases and resource existence instead. The revised focused guard passes.

### 2026-10-05 UP012 — Biography guard invocation

The package-style `python3 -m unittest tools.tests.test_new_horizons_hero_biographies`
invocation fails because this script imports a sibling test module directly.
Run `python3 tools/tests/test_new_horizons_hero_biographies.py -q` instead;
that invocation passes all five focused checks. This is an invocation repair,
not a gameplay or biography-content failure.
The first native build fails because the fixture uses CHeroHandler's public
HeroType interface, whose biography methods are private. Use the concrete
CHero accessor for the test; do not widen the public engine API. Initial log:
`build/nh-up012-validation/build.log`.
The repaired client/test build passes; the two isolated native cases pass with
zero skips. Evidence: `build-repaired.log` and `native.log`/XML in that directory.
The prior suspected Mods-prefixed resource path was removed during source review
before execution; actual localization provenance supplies the override check.

### 2026-10-05 UP232 — Arcane Ballistics ranged fixture legality

The first registered-fixture build succeeds, but the principal native case
fails before mark generation: both test shooters start adjacent to the hostile
target and ordinary shooting is blocked. The two eligibility controls pass.
Independent review identifies the same geometry defect. Repair the fixture's
positions, not production shooting legality or the perk. Retain
`build/nh-up232-validation/build.log` and `native-first.log`/XML; require the
accepted shot, independent PDR source/cap oracles and detached parity to pass
before committing the implementation.
Repair moves the target outside both shooters' adjacency and separates the
two-hex control stacks. The repaired both-target build succeeds; all three
new cases plus three related Sorcery/Focus Magic cases pass 6/6, zero skips.
Final evidence: `build-repaired.log`, `native-final.log` and XML in the same
directory. Production legality and the substantive damage assertions remain
unchanged; the failure was fixture setup, not a perk defect.

### 2026-10-05 UP234 — Content audit baseline and UI acceptance boundary

The existing68-case content suite has65 passes and three unrelated failures:
its curated hero-spell inventory oracle omits newer Mass variants/Puppet Master,
which also makes two legacy-schema validation cases fail. The focused Vengeful
Vines metadata case and generated-module drift check pass. Do not change live
spell availability to satisfy an outdated test inventory; reconcile that oracle
in Phase2. Source layout checks cannot certify native rendered appearance.
House of Wisdom's Haste/Cure/Bless screenshot is not evidence of an acquisition
bypass: retained identities remain eligible; audit stale saved offers separately.
The initial native build fails in the new friendly-exclusion fixture: the
bonus query returns a shared BonusList pointer, so the assertion must use
`->empty()`, not `.empty()`. This fixture is repaired. The first native run
passes 14/15: the historical v2 fixture retains v3-only `heroAccess` and
`restoration` fields, rejected by the strict saved-profile parser. Strip those
fields only in the pre-v3 test snapshot; production validation stays unchanged.
After repair, Linux client/test targets build and the focused native filter
passes 15/15, zero skips. Retain initial and repaired logs under
`build/nh-up234-validation/`; final evidence is `native-final.log`/XML and
`build-fixture-final.log`. Rendered acceptance remains pending, not inferred
from the successful build or source guards.

### 2026-10-05 UP233 — Linux delivery startup and AI warning boundary

Clean committed-source256176fae Release/Ninja build passes744 steps, followed
by a4-step correct-version relink. The legacy CMake Git helper misreads an
absolute worktree .git pointer; changing only the disposable worktree metadata
to the equivalent relative pointer makes the generated version match its actual
commit. No gameplay source correction or shared-worktree reset is involved.
The first private smoke profile was an already-existing unmanaged empty directory
and was correctly rejected before client execution. Retain UP233-headless-smoke.log;
retry with a new child profile initializes All for One and runs several AI turns.
Bounded20-second stop returns124 intentionally, without GUI or pointer input.

That smoke also logs2851 `Stack ammo overuse. total: 0, used: 0, requested: 1`
warnings, starting in a day2 AI garrison encounter. AI continues afterward;
no crash is established. Retain UP233-headless-smoke-repaired.log and private
profile for reproduction. Investigate live versus detached ammunition spending
as a Phase2 integration finding; do not present startup acceptance as full AI
correctness. Snapshot6e1e8ce3 is promoted at the user's explicit delivery request.

### 2026-10-05 UP231 — Damage fixture contracts and information views

Initial both-target build33121 fails: DamageRange has no equality operator.
Compare min/max explicitly; the new fixture also needed concrete class/randomizer
headers and a baseline name that does not shadow its helper. Retain
UP231-breakthrough-build.log. Independent review caught a separate oracle error:
real Defend changes Creature Defense too. Capture separate with/without-perk
baselines with Battlecraft temporarily absent, preserving the real stance bonus,
then assert the new90%/80% reduction channels independently.

Native40482 passes7/10 in3.332s, zero skips. Two existing OffenseRank cases throw
"Earlier New Horizons perk tier is still required": their direct Advanced perk
setup needs a legal Basic prerequisite, not relaxed production validation.
Record those stale fixtures for Phase2. The new detached assertion compares a
player-scoped callback that hides the defending hero with an all-knowing live
oracle; the missing enemy Battlecraft contribution is an information mismatch.
Use an aliasing all-knowing callback for equivalent-view parity. Do not infer
that player-scoped AI knows private enemy skills. Retain initial native.log/XML.

Repaired both-target52099/53299 pass. Focused30605 passes8/8 in2.745s, zero
skips; retain UP231-breakthrough-native-repaired.log/XML. This run excludes,
rather than falsely marks green, the two stale Offense fixtures. Broader
passive-source interactions and independent legacy-factor execution remain
deferred; no GUI, world-save acceptance or playable promotion is claimed.

### 2026-10-05 UP157 — Isolate combat-perk fixture prerequisites and AI options

Both-target build67320 passes732 steps. Initial focused native24408 passes17/22
in6.333s, zero skips; retain UP157-preemptive-native.log/XML. Four new tests
fail at the fixture's accepted end-Tactics/Defend setup after selecting Tactics
as their required Basic perk. Use a legal nondeployment Basic perk to isolate
the Advanced combat reaction; do not weaken deployment or action validation.
The AI selection fixture puts a zero-Movement creature next to a melee target:
zero movement still permits that adjacent attack, which AI selects legitimately.
Place a reachable future threat beyond immediate attack reach and avoid lethal
overkill before asserting Defend selection. No principal perk acceptance or
count increase follows from the initial failure; repairs and rerun are required.

Fixture-only build4257 passes4/4. Repaired focused99767 passes22/22 in6.291s,
zero skips, with all seven new principal cases. Retain repaired.log/XML beside
the initial failure. The change to legal Entrench acquisition isolates combat
without bypassing prerequisites; distant, smaller threat geometry preserves
the actual AI Defend assertion rather than accepting any action.

### 2026-10-05 UP023 — Active Perfect Moment diverged from its specification

The active registry said automatic first eligible attack at current Luck+5,
but production required a manual declaration and allowed negative Luck; its
server fixture reinforced that wrong behavior. Audit executable consumers,
not just active flags or passing assertions. Shared target-aware eligibility
must exclude only Serendipity's explicitly chance-only bonus, and authority/AI
must agree on automatic strike-time use. Remove the obsolete local arming UI.
Do not copy the whole Sylvan history on every attack when filtering the
chance-only bonus; use the existing referenced state and subtract that bonus
before normal caps. Fixture isolation must avoid inherited rank Luck and retain
eligible+5 in pre-emptive-death controls. Client26695 passes; native compilation
59248 and focused execution remain pending at this checkpoint. Broader
movement/reaction and whole-battle save acceptance are separate Phase2 work.

Final native build59248 passes302/302. Broader native21000 passes31/35
in8.997s, zero skips, with four guard failures: GenuineActivation manually
constructs an invalid Second Wind Order shape; AuthoritativeMultiTarget and
WildChance select Advanced perks without Basic prerequisites; LegacySave writes
a whole modern battle at a version unable to retain initial Army Value.
Independent review traces these to unchanged guards, not Perfect Moment's
automatic trigger. Preserve UP023-perfect-moment-auto-native.log/XML and keep
the four fixture repairs in Phase2. Do not label that broader run green or relax
guards. Principal22333 passes19/19 in4.540s, zero skips; focused.log/XML retain
the narrower acceptance evidence. Positive post-hit battle deep-copy passes,
without claiming every world-save or old-version path. Eleven client guards pass.

### 2026-10-05 UP023 — Unmanned tower fixture build and setup

Native build62771 fails at the new fixture's nonexistent BattleField.h include.
BattleField is already declared by the identifier headers; remove the guessed
header rather than changing production. Preserve UP023-unmanned-tower-native-build.log.
Root also finds manual BattleStart bypasses the actual BATTLE_SETUP script event;
the legacy control must use the production BattleProcessor::startBattle path,
not merely compare uninitialized creature-damage bonuses with themselves.
No native or legacy-script acceptance from the failed fixture/build. The owner
repairs only its new fixture before the next serialized build.
Repaired build96872 succeeds. Native69107 passes5/8 (real legacy setup plus
four Engineer controls) but its three v3 cases throw because map overrides
merge with the installed v4 rules. Erasing warMachineShop from an override
does not remove the inherited value: explicitly set it to JSON null. Preserve
UP023-unmanned-tower-native.log/XML; do not weaken production version validation.
Final build44376 passes; native22240 passes8/8 in2.703s, zero skips, including
the true legacy setup, automatic no-hero shot, custom77 and Engineer controls.
Keep native-build-merge-repaired.log and native-merge-repaired.log/XML. The
runtime fix was unchanged across these fixture repairs. Older-v2 and wider
save/siege combinations remain Phase2 rather than additional Phase1 gates.

### 2026-10-05 UP023/UP200 — Machine durability retained legacy resource data

Initial native27834 passes all3 Glyphs siege controls but fails machine HP:
Ballista250 instead of300, Catapult1000 instead of500, Tent75 instead of250,
Cart100 instead of250. Keep UP200-machine-hp-before.log/XML. Correct the
four scoped creature definitions and generated module registration, not the
authored expectations or purchaser resources. Final88293 passes4/4 in1.466s,
zero skips; both-target final build and exact offline guard pass. This proves
loaded definitions, not every machine battle/save interaction. Those are Phase2.
The first Glyphs build also exposed macro dangling-else warnings and review
found fragile relative include paths: add braces and normalize../../../ paths.
Repaired build55816 exits0; preserve its separate build log.

### 2026-10-05 UP023 — Canonical growth retained vanilla fallbacks

Initial loaded64-row audit compiles (67485) but fails11 authored expectations:
Archer, Monk, Cavalier, Unicorn, Naga, Skeleton, Wolf Rider, Air, Water, Magic
and Phoenix. Legacy resource fallbacks are not evidence of canonical coverage.
Preserve UP023-canonical-growth-before.log/XML. Repair the production saved
growth-line rows, not the expected table or proprietary resources. Existing
historical snapshots must not be merged with newly installed rows.
Final both-target83259 exits0; native47814 passes3/3 in1.177s, zero skips,
including checked upgrades and current/historical rule-object serialization.
Keep UP023-canonical-growth-final-build.log and final.log/XML. Weekly stocks,
full saves and recruitment remain separately scoped acceptance work.

### 2026-10-05 UP023 — Castle loaded-definition fixture interface

Both-target build6413 exits1: CreatureService getById exposes the Creature
interface, not concrete CCreature fields/bonus methods. The new Castle fixture
must use its declared upgrade and bonus-bearer interfaces; preserve all exact
stat and retained-ability assertions. Brimstone's fixture compiles. This is a
test API failure, not evidence against the frozen data changes or native
acceptance. Keep UP023-building-castle-build.log and use a separate retry log.

Accepted repair uses getBonusBearer for interface abilities and the declared
CreatureID::toCreature accessor for the concrete upgrade set, with every original
assertion retained. Build91972 exits0; native85598 passes5/5 in1.765s, zero
skips. The actual Brimstone grant/cleanup and four loaded Castle definitions
pass. Evidence: UP023-building-castle-build-repaired.log and
UP023-building-castle-native.log/XML. Review acknowledged its missed interface
return type; compilation remains the definitive API gate. No production
mechanic was weakened to obtain a pass.

### 2026-10-05 UP023 — Creature siege AI callback mismatch

Both-target build23954 exits1: CBattleInfoCallback has no battleGetStacks
method. Use the declared stack-filter API instead of assuming the player
callback convenience API exists on its base. Preserve the failed
UP023-creature-siege-build.log; no native acceptance from this compile attempt.
Independent review also caught the field control removing every defender before
beginCombat, which could conclude the battle before callback checks. Restore a
living defender in that fixture without weakening the no-wall assertion.
Production WAIT preservation and normal action-bookkeeping findings were
repaired before this gate. Retry and authoritative native execution pending.

Retry71445 builds client and native targets successfully. Native98352 exits139
in the first real AI case. Batch debugger63884 localizes the crash to the test
recording callback's inherited surrender/retreat transport request, which uses
a null session. Give that fixture callback an explicit neutral decision,
matching existing recording callback practice; do not alter production retreat
rules. Preserve UP023-creature-siege-native.log and UP023-creature-siege-gdb.log.
The failed native run establishes no accepted wall-shot result.

Callback-repaired build83087 exits0. Native52203 runs6 cases in2.154s:
4 pass, both gate positives fail because the real evaluator selects WAIT.
The production policy deliberately preserves WAIT; enum06 is WAIT, not SHOOT.
Keep that tactical guard and repair the positive scenario to exercise a genuine
non-Wait activation before expecting a wall shot. Ordinary-shot, field and
both Hero Action controls pass. Keep the failed repaired log/XML separately.

Accepted repair adds a separate nearby hostile stack to the positive siege
fixture, retaining the inside defender and allied ground force. It asserts a
legal ordinary shot before actual AI evaluation; no fake Wait state or relaxed
production guard. Both-target build92551 exits0; native35852 passes6/6
in2.190s, zero skips, including both authoritative gate-damage cases and
activation completion. Independent review accepts the bounded repair. Evidence:
UP023-creature-siege-build-geometry-repaired.log and
UP023-creature-siege-native-geometry-repaired.log/XML. Earlier failed logs remain.

### 2026-10-05 UP023 — First native execution finds rollover regression

Repaired native build69581 exits0. The first focused execution runs20 cases
in6.296s:16 pass,4 fail, zero skipped. Preserve
UP023-adventure-artifacts-native-focused.log/XML. The v2 fixture incorrectly
retains v3-only heroAccess data; repair historical profile construction without
removing legacy passive assertions. Three real next-day path cases fail,
including the pre-existing low-Movement Water Walk rollover case. Current-day
paid artifact casts, exact costs and daily rejection pass. Diagnose production
layer admission before accepting the slice; do not weaken next-day assertions
or claim native acceptance from successful compilation.

Accepted repair: remove premature day0 cast-used suppression from generic
layer eligibility, which runs before MovementPreparationRule advances the
destination day. NK2 retains its final plannedTurn/dayFlags action validation.
Down-convert the historical v2 fixture by stripping v3-only row metadata and
disabling variant rows before strict validation, matching existing historical
builders; no assertions changed. Both-target build79874 exits0 and repaired
native70394 passes20/20 in6.541s, zero skips, with separate repaired log/XML.
The reviewer explicitly corrected the earlier missed ordering risk. Generic
whole-route hypothetical cast-budget forecasting remains deferred, not claimed
implemented by this repair.

### 2026-10-05 UP023 — Adventure artifact native fixture include

Client35563 passes. First native build61323 terminates with an incomplete
TurnInfo type in the two new AI cases: the fixture uses getTurnInfo but only
had the hero's forward declaration. Add the explicit lib/pathfinder/TurnInfo.h
include; no production rule or test assertion changes. Preserve
UP023-adventure-artifacts-native-build.log. Incremental retry69581 uses
UP023-adventure-artifacts-native-build-repaired.log; native acceptance remains
pending until actual execution. The unsuccessful first apply_patch matched a
relative include style that this file does not use and wrote nothing; the
correct repository-root include is applied.

### 2026-10-05 UP021 — Exact-fit fixture diagnosis and accepted repair

The successful partial merge applies a garrison-operation pack, which checks
victory. A map with only one active team immediately wins and removes that
player before the following exact-fit request. The rejection was therefore
the active-player guard, not Leadership arithmetic. Add a second active player
after assigning the original hero's army; assert the original player remains
active/INGAME with no end-game/turn pack or query before the exact-fit request.
Keep all exact counts and success assertions. Record actual localized modal
InfoWindow feedback for empty and occupied cross-army last-creature rejection,
not complain's serverProblem wrapper. The third source guard also needed its
obsolete count-minus-one branch assertion replaced with the new ordinary-route
contract. Client39538/native build88200 pass; native26444 passes13/13 in3.908s,
zero skips, and all three source guards/review pass. Retain the first failed
log/XML and repaired evidence; no production invariant was weakened. Rendered
feedback and broader combinations remain separate Phase2/delivery obligations.

### 2026-10-05 UP021 — Ordinary transfer route and feedback gaps

Bounded source audit finds radial/Alt+Ctrl last-stack moves still require the
entire count-minus-one numeric split to fit, instead of requesting a server-
clamped whole-stack intent. Exact-one radial moves silently return and occupied
same-creature clicks can reach complain's generic serverProblem broadcast.
The one-creature native fixture currently asserts that broadcast, which is not
the requested normal gameplay explanation. Preserve explicit numeric split
semantics; use ordinary transfer intents and existing localized InfoWindow for
valid last-creature rejection. First native75985 passes11/12 in3.595s, zero skips;
OrdinaryMerge's third exact-fit request rejects without transferring. Diagnose
the actual rejection, retain assertions and UP021-last-stack-native.log/XML.
No verification or playable acceptance is inferred from passing wrong oracles.

### 2026-10-05 UP062 — Final Frenzied Curse gate accepted

Boundary preflight uses(4,5)->(12,5), verifies ordinary WALK and boosted shared
WALK_AND_ATTACK, then removes its test-only bonus before the authoritative
activation. The server must independently grant, attack and clean up. Final
build64062 exits0; native42075 passes30/30 in6.265s, zero skips. Retain all prior
failed logs and assertions; no production/schema contract was weakened.
UP062-frenzied-native-build-repaired-v4.log and
UP062-frenzied-native-final.log/XML are the accepted evidence. Data/inventory19/19,
module drift and independent review pass; coverage224/310, Chaos Magic5/10.

### 2026-10-05 UP062 — Principal geometry and historical control repairs

Repaired native build97008 exits0; first focused run65113 passes22/30 in5.413s,
zero skips. All seven AI cases pass. Three activation cases expose fixture
assumptions: the chosen near geometry is already in ordinary melee range;
the final attack packet may be retaliation; the helper reports eligibility,
not whether the temporary bonus remains attached after an action. Preserve
expanded-reach/accepted-action/actual-marker cleanup assertions while correcting
those assumptions. Four old V1/V2 profile cases copy the V3-only heroAccess
field; repair historical snapshots, not the strict schema. The Puppet resistance
control is unexpectedly receptive and remains under diagnosis. Retain every
original filter case and UP062-frenzied-native.log/XML. No coverage credit yet.

Repairs retain all30 cases: near geometry moves to(3,5)->(12,5), own attack is
identified by attacker ID, and WALK cleanup checks attached marker/range. V1/V2
snapshots strip heroAccess/restoration. Puppet's nominal100 resistance source
is capped to75 in NH; resistance is rolled at accepted resolution, not target
receptivity. Its control now requires the cast packet's resistedCres entry and
preserves both Berserk effects with no control marker. Production unchanged;
repaired build64193 exits1 in UP062-frenzied-native-build-repaired-v2.log:
BattleAttack stores targets inside bsa entries, not a direct stackAttacked field.
Keep attacker-ID selection, assert bsa is nonempty, then check its target field.
Do not remove the actual-target assertion. Owner repairs only that fixture line;
another sequential build is required before running the full retained filter.

Build63588 exits0. Rerun27862 passes29/30 in6.314s, zero skips; all historical,
Puppet, AI, owner/inactive and movement-only cleanup controls pass. The active
attack geometry at(3,5) is beyond even boosted reach. Derive the fixture position
from actual movement/path cost and preflight both ordinary and boosted shared
candidates instead of guessing distances. Retain the actual accepted attack and
post-action cleanup assertions. Preserve UP062-frenzied-native-repaired.log/XML.

### 2026-10-05 UP062 — Typed spell source in Frenzied AI fixture

Client78683 exits0. Native build89833 exits1 while compiling the new AI fixture:
BonusSourceID cannot hold a raw SpellIDBase enum; wrap BERSERK in SpellID before
constructing the source variant. Preserve UP062-frenzied-native-build.log.
Repair the fixture constructor, not the variant schema or production contracts;
retain all assertions and rerun the focused native gate before acceptance.
Independent frozen runtime/AI and fixture reviews found no blocking issue.
Broader stopped-unit Berserk valuation is existing Phase2 work, not a new
Frenzied reach regression: Time Stop returns zero movement before Speed bonuses.

### 2026-10-05 UP108 — Allowance kind is not grant source

Repaired native build37739 failed in the ordinary Purify AI fixture because
`GrantSource::HERO` does not exist. HERO is an allowance kind; ROUND is the
source of the ordinary round allowance. Correct the assertion to ROUND without
changing production or dropping the ordinary-cast control. Both historical V2
fixtures now also strip the V3-only restoration field. Preserve
UP108-purifying-native-build-repaired.log; the replacement build uses
UP108-purifying-native-build-repaired-v2.log. Repaired build30070 exits0;
native4954 passes43/43 in7.802s, zero skips. All original filter cases retained;
production validation was not loosened. Source/native acceptance established.

### 2026-10-05 UP108 — Purify fixture version and AI-choice assumptions

Client15739 and native71940 build successfully. First focused run54853 passes
40/43 in7.393s: all five new server cases and the selected Divine Mandate AI
case pass. Two existing V2 compatibility fixtures reject a copied V3-only
`heroAccess` field; strip it from their historical snapshots rather than
loosening the strict gameplay schema. The new ordinary-AI control wrongly
assumes Purify must win against legal Orders: the evaluator selects an Order.
Keep its no-extra-cleanse/source/parity assertions, but isolate ordinary Purify
behavior from global heuristic preference. Preserve UP108-purifying-native.log
and XML; acceptance remains pending the repaired focused run.

### 2026-10-05 UP108 — Spell-effect transport is not magical provenance

Pre-build Purifying Mandate review found that legacy Poison/Disease can travel
as SPELL_EFFECT source groups while the shared affliction system classifies
them as physical. Treating every removed spell-source group as a magical
trigger would wrongly award an extra cleanse after a physical-only removal.
Runtime and detached AI now use the same pre-removal classification helper;
ordinary Purify eligibility remains unchanged. The focused legacy Disease
regression must leave an unrelated physical affliction intact. Do not infer
provenance from packet/bonus source category alone. Native acceptance is pending
under UP108, not established by this source review.

### 2026-10-05 UP108 — Order-preview source guard arity

Knightly build79514 and native77528 pass32/32. The separate Order source guard
still required the three-argument preview coefficient from before Sacred Command,
although the accepted implementation already passes captured Divine Mandate
efficiency as its fourth argument. Update that exact assertion and require the
prepared snapshot aggregate getter; do not weaken the guard to mere symbol
presence. The repaired guard passes. This is a stale source assertion, not a
runtime failure or reason to omit Sacred/Knightly from previews.
Pre-build fixture review also caught Advanced selection without a Basic perk;
the fixture now legally selects a Basic prerequisite rather than bypassing offer
legality. Production behavior was not changed for either fixture/guard repair.

### 2026-10-05 UP108 — Sacred fixture active-unit lookup

Client build3475 passes. First native-target build45324 fails at the new
fixture's active-stack lookup: `battleActiveUnit()` returns a read-only unit
pointer, while `battleGetStackByID` expects an integer ID and returns a read-only
stack. Use the actual unit ID and preserve constness in the fixture; do not cast
away constness or loosen the production callback. Keep
`UP108-sacred-native-build.log`. Native acceptance remains pending the repaired
build and principal cases; no coverage increment is inferred from compilation.
Repaired both-target44913 passes. Native86438 passes26/26 in4.654s, zero skips,
including all six new cases; retain UP108-sacred-native-build-repaired.log and
UP108-sacred-native.log/XML. Production semantics and fixture assertions were
unchanged by the compile repair.

### 2026-10-05 UP108 — UI inventory column order

The focused two-case UI/perk inventory check caught six newly reconciled rows
whose Implementation/Art classifications had been reversed. Restore the actual
CSV column order: Implementation=Provisional, Art=Not done for neutral fallback
icons. The unchanged guard then passes both cases across all 310 perk rows;
do not mark a generic fallback as purpose-made or approved artwork.

### 2026-10-05 UP108 — Consecrated fixture header and projection baseline

Client97140 passes. First native-target build2604 exits1 on an incorrect fixture
include: CPlayerBattleCallback.h lives in lib/battle, not lib/callback. Retain
UP108-consecrated-native-build.log; locate headers with rg --files before using
a guessed subsystem path. Production compiles and remains unchanged.
Independent review also catches two fixture-oracle issues before native testing:
re-fetch a projected target after castEval because copy-on-write can replace the
pre-cast live fallback; set an explicit creature enchant-duration baseline because
CUnitState defaults to3, rather than assuming a HERO-only Bless formula applies
to a creature. One imp's CREATURE_SPELL_POWER is divided by100; configure7500
to exercise75 power. Repair setup/oracles without weakening the perk exclusions.
No native acceptance or coverage increment until a frozen rebuilt candidate passes.
Repaired build55168 passes. Native18598 passes21/22; all three new Consecrated
cases pass. Its sole exception is Grand Formula's final CMemorySerializer::deepCopy
after casualties: BattleInfo.h explicitly rejects binary casualty-health provenance
since756d225818 (2026-10-03). Independent review classifies this existing whole-
battle save/fixture expectation as Phase2, not a Consecrated regression. Preserve
UP108-consecrated-native.log/XML. The accepted focused filter excludes only that
case and passes21/21 in5.491s (5534), zero skips; retain
UP108-consecrated-focused-accepted.log/XML. Do not weaken the protective save guard
or describe the original22-case batch as fully green.

### 2026-10-05 UP108 — Callback interface and concrete spell includes

First Reserve build47952 exits1; retain UP108-reserve-build.log. The generic
IBattleInfo pointer does not expose battleGetDivineMandateStatus; use the existing
battle callback for the before-count just as for the after-count. Both modified
fixtures need the direct CSpell.h include to establish the CSpell-to-spells::Spell
inheritance conversion when calling battleGetSpellCost. Do not expand the PCH,
loosen signatures or weaken recovery assertions to hide either compile error.
The minimal owner repairs are frozen; native acceptance requires the repaired
build and actual-action cases, not successful compilation of earlier objects.
Repaired build25165 exits0 for client and native targets; focused native96092
passes22/22 in2.672s, zero skips. Preserve UP108-reserve-build-repaired.log and
UP108-reserve-native.log/XML. Gameplay semantics and assertions were unchanged.

### 2026-10-05 UP046 — Strong SpellID wrapper in the perk fixture

First client/native build64934 exits1 in the new runtime fixture:
`SpellID::MAGIC_ARROW` is an enum, not an object with `toSpell()`.
Preserve UP046-perks-build.log. Construct `SpellID(SpellID::MAGIC_ARROW)`
before resolving the entity. Pre-retry source review also distinguishes the
cast's optional effect value from mechanics' plain Value64; do not call
`has_value` or dereference the latter. No production failure or acceptance is
inferred from this fixture compile error. The expanded three-perk candidate
must be frozen, rebuilt and exercised before coverage increases.
Core-only build63831 then exits1 in the new ability helper: iterating BonusList
needs its concrete header, not the forward declaration exposed by Unit/Bonus.
Preserve UP046-perks-core-build.log. Add the direct BonusList.h include; do not
expand the precompiled header or change mitigation semantics to hide the error.
Frozen full build66091 then fails only in the new AI fixture's prepare signature:
MasteryLevel is a namespace; the parameter type is MasteryLevel::Type. Retain
UP046-three-perks-build.log. This repeats the recorded UP108 fixture lesson;
check existing enum declarations when extending test helper signatures rather
than relying on the spelling of qualified enumerator values. Production objects
compile, but the test binary is not accepted until repaired and rebuilt.
Repaired full build68374 passes; native92485 passes23/23 in6.366s, zero skips.
UP046-three-perks-build-repaired.log and UP046-three-perks-native.log/XML retain
the acceptance evidence. Neither production semantics nor fixture assertions
were weakened to repair the compiler errors.

### 2026-10-04 UP224 — Bless control cast requires the spell environment

Client38169 passes. First both-target60053 fails at two new fixture control
casts: BattleCast::applyEffects takes ServerCallback*, not CGameHandler*.
Keep UP224-adela-native-build.log. Use the existing battle spell environment
adapter for control application; do not loosen production casting interfaces
or drop the accepted principal cast/damage comparisons.
Repair build99683 succeeds. First native64226 passes6/8 in5.327s, zero skips;
only the two new save-shape checks fail. Their helper compares limiter/updater
pointer identities against the global prototype after deserialization rather
than the reconstructed objects' semantics. Preserve UP224-adela-native.log/XML;
repair that test oracle while retaining source/type/value/marker and actual
duration/damage checks. Existing Cure/Res cases and the new principal checks pass.
Final build48936 passes; native6322 passes8/8 in5.310s, zero skips, recorded in
UP224-adela-accepted.log/XML. Independent review accepts the semantic oracle;
no production or gameplay assertion was weakened.

### 2026-10-04 UP227 — Construction-state enum and test invocation

First both-target build45584 fails only in the new Conflux fixture: the
construction prerequisite state is PREREQUIRES, not PREREQUISITES. Preserve
UP227-vault-build.log and correct that test identifier without weakening
construction validation. Use unittest discovery with tools/tests as the search
directory; package-style invocation cannot resolve the existing flat imports.
Discovery passes all five Conflux data guards. Array merge indices are explicitly
one-based in JsonUtils::getIndexSafe; the second horde slot is modify@2.
Repaired both-target84391 succeeds; native97534 passes3/3 in2.695s with zero
skips. Keep the first failed build log alongside the repaired evidence.

### 2026-10-04 UP225 — Legacy schema fixtures and target setup

First offline magic/schema invocation through python -m unittest lacks the
flat tools/tests import path; run the script directly. The direct20-case run
then passes13/20: seven errors come from a downgrade helper leaving the new
v3-only restoration marker in data it submits to the unchanged v2 schema.
Retain UP225-magic-data-first.log. Strip restoration in the v2 and v1 fixture
helpers, not the production schemas. Two new strict marker-shape/absence and
v3-only guards are added. Repaired run passes22/22 in1.967s, recorded in
UP225-magic-data-repaired.log.

Independent fixture review before registration catches typed SpellID source
wrappers, overlapping double-wide Archangel footprints, uninjured temporary
targets making rejection trivial, and a corpse cast with its only active ally
dead. Repair only the unregistered fixture while baseline5018 compiles frozen
production. Ensure living legal active context and meaningful target injury;
do not weaken authoritative validation. Permanent-health ledger evidence is
not separate postbattle execution; that lifecycle remains Phase2. Repaired
fixture build14793 passes; final native8017 passes9/9 in4.758s, zero skips
(UP225-resurrection-native.log/XML). No production validation was weakened.

### 2026-10-04 UP224 — Stale unsupported-version oracle

Both-target build3385 succeeds. First native64172 passes21/22 in3.991s,
including all three new Cure cases and five real damage-specialty controls.
Only the old direct-damage parser test fails: it still expects version3 to
throw, while the existing production parser supports versions1 through3.
Retain UP224-cure-native.log/XML. Repair the fixture to accept the current
v3 record/absent field and reject version4; do not weaken production validation
or suppress the failing test. Repair build69675 succeeds; final native20053
passes22/22 in4.011s with zero skips, UP224-cure-accepted.log/XML. Independent
review confirms the repair matches the existing versions1–3 production contract.

### 2026-10-04 UP223 — Native binary must be relinked after contract expansion

Client-only build39945 succeeds, but the first adjacent native run uses the
previous vcmitest binary. Six Estates AI cases reject the new five-alias config
with the old one-to-four parser; the markerless baseline case passes. Evidence:
UP223-estates-adjacent.log/XML (1/7 pass). This is not an income/AI production
failure: vcmitest contains its own linked parser objects. Build that target
before acceptance execution, then rerun these exact controls. Do not drop
Estates from the new config or loosen validation to accommodate a stale binary.
After both-target16010 relinks vcmitest, native90493 passes both new Estates
cases, the baseline and all six AI controls. It fails only Archery's prior-list
fixture: removing Archery from today's five entries does not reconstruct the
historical three-entry snapshot (4 remains). Pin the exact historical list
Logistics/Armorer/Offense rather than deriving it from expanding current data.
UP223-estates-final.log/XML retains the11/12 result. Root repairs only fixture
input, preserving its size/legacy-alias assertions. Build47106/rerun pending.
Static fixture review also corrected floor-based handicap expectations to the
engine's divideAndCeil and typed the resource wrapper before compilation; no
income production fix or relaxed oracle was needed.
Repair build47106 succeeds. Final native95834 passes12/12 in4.501s, zero
skips, UP223-estates-accepted.log/XML. Both Estates cases, all six AI controls,
baseline, Archery and parser controls pass. Historical failed evidence remains.

### 2026-10-04 UP220 remainder / UP222 — Fixture API ownership types

Build95706 stops on two expanded spell-access fixture declarations treating
CSpell::battleMechanics' unique_ptr result as a raw pointer. Root had caught
the static type issue after the frozen build started and waited for its terminal
result rather than editing compiled inputs mid-run. Use const auto to retain
the owning mechanics object, as the existing creature-cast control already does.
First evidence: UP220-UP222-coverage-build.log. Repair/native acceptance remains
pending. No production access/rules change is needed. The Offense fixture's
unsupported-list control now uses Eagle Eye because Archery becomes supported;
keep the rejection assertion, not a stale invalid assumption about Archery.

Repair build7593 succeeds. Native55749 passes19/19 in2.067s, zero skips,
UP220-remainder-native.log/XML: all35 restricted identities, real guild/House
stock and13 starters, source/inscription denial, rejection without spend,
Ogre Mage Bloodlust/Master Genie Shield and Air Shield/Storm Elemental Protect
Air effects, and markerless saved-game restoration pass. No native failure was
hidden or game launched. Random Genie selection and broader source interactions
remain Phase2; Haste and authored replacements remain explicit implementation
gaps. UP222 is accepted separately: build46837 and native35240 pass12/12
in6.231s, zero skips. Its frozen fixture separates raw100 damage from the
unrelated50% bonus before computing rank oracles. Static review initially
suggested changing Focus Fire's helper value5 to10; source inspection rejected
that suggestion because Target Caller is a separate payload contribution.
Keep acceptance arithmetic tied to actual contribution seams, not reviewer
assumptions. No production change or relaxed oracle was needed.

### 2026-10-04 UP221 — Fixture setup corrections before execution

Root static review catches a mutable damage argument cast from const storage,
duplicate fixture member, initial-growth case using the ordinary control rather
than the specialist, and missing beginCombat. Tester corrects these before the
native build. Astra identifies a stale-forecast risk when advancing activation
by Defending intervening units; preserve the exact initial arithmetic assertions
and recompute the accepted attack forecast immediately before dispatch. No
production blocker found. Client80060 and twelve hero-data checks pass; native
execution remains pending. Record actual compile/runtime failures below rather
than weakening principal-path evidence. Phase2 retains future magical-melee
semantics and broader modifier compositions.

Native build93795 stops on one fixture compilation error: the local
newHorizonsOffense helper collides with the existing namespace of that name.
Root renames it nhOffenseSkill in the four call/declaration sites; production
code is unchanged. First log: UP221-offense-specialty-native-build.log.
Bounded repair build and native run must pass before accepting UP221.

Repair build22982 succeeds. First native11986 runs15 tests in7.681s:13 pass,
two specialist cases fail exact arithmetic by one damage point. Their oracle
multiplies an already floored baseline whereas the engine applies the Offense
factor before final flooring; the Pikeman Attack/Defense baseline is fractional
at the chosen count. Keep exact assertions and make the baseline integral or
derive the full pre-rounding oracle, not a tolerance or production change.
UP221-offense-specialty-native.log/XML retains the failure. Real accepted hits,
growth, source values, old rules and saved markers did not fail independently.

Tester confirms unrounded base292.5; use200 rather than100 attacking Pikemen
for integral base585, retaining every exact expected percentage assertion.
Bounded rebuild50115 succeeds; final native91027 passes15/15 in7.752s,
zero skips, UP221-offense-specialty-final.log/XML. Initial compile/static
repairs and first native oracle failure remain visible. No production damage
formula, tolerance or intended specialty/perk assertion was weakened.

### 2026-10-04 UP220 — Source-review fixture corrections before native acceptance

Production review approves separate heroAccess/world membership. Root catches
House-of-Wisdom fixture misuse (Castle is not eligible; use an actual Conflux
town), then Astra catches overlapping friendly/enemy hexes. Correct both
before runtime acceptance; assert the actual action controller is Player0 so
rejection cannot be proved by an unrelated ownership error. Client78840 and
native50252/25274 builds succeed. First native18192 passes18/18 in2.044s,
zero skips. Evidence includes authorized rejection without spend, actual Ogre
Mage Bloodlust, real acquisition/default-book producers and old-save round-trip.
No failed native run was concealed or assertion weakened. Remaining roster and
authored specialty gaps stay open; broad effect interactions remain Phase2.

### 2026-10-04 UP219 — Focused repair acceptance

Bounded rebuild2116 succeeds. Final native12882 passes13/13 in6.135s with
zero skips, UP219-armorer-specialty-final.log/XML. Exact Logistics marker
matching preserves Mephala's valid Armorer marker; Basic Pavise precedes
Advanced Formation Fighting. All intended reduction/growth assertions,
accepted melee/forecast, initial-XP sampler and legacy/save controls pass.
Module drift/twelve hero-data checks pass; source review has no blocker.
Seeded threshold growth and broad perk/AI interactions remain Phase2.

### 2026-10-04 UP219 — Expanded specialty support needs exact fixture identity

Both-target build21618 succeeds. Native30347 runs13 tests in5.948s, zero
skips:9 pass and4 fail. The earlier Logistics fixture counts every Skill
specialty marker on ordinary control Mephala; her newly supported Armorer
marker is legitimate and must not be removed. Count the Logistics identity
specifically. Three Armorer alias cases reach Advanced Formation Fighting
selection without a Basic perk, and correctly receive the prerequisite error.
Give the fixtures a valid Basic perk first; retain production progression and
the6/12/18 reduction plus12/24/36 growth assertions. Initial-XP sampling,
missing-rule legacy behavior and adjacent physical/growth controls pass.
First evidence: UP219-armorer-specialty-native.log/XML. Repair pending; no
native acceptance or playable delivery is claimed.

### 2026-10-04 UP218 — Focused repair acceptance

Bounded repair41124 succeeds. Native33370 passes15/15 in6.804s, zero skips:
the intended224/248/272 pools, Navigation274, unrelated sources, reused caches
through rank removal and both saved/legacy guards now pass, alongside eleven
adjacent controls. Exact legacy-producer retention replaces the false empty
list assumption. Module drift and twelve hero-data checks pass; final Astra
review finds no blocker. Source/native evidence is not playable acceptance.

### 2026-10-04 UP218 — Legacy alias and active Skill identity are distinct

Both-target95053 succeeds. Native46075 runs15 tests, zero skips, in6.852s;
eleven adjacent controls pass but all four new specialty tests fail. The local
saved specialty getter returns20, yet movement remains220/240/260 because the
matching predicate identifies the legacy core:logistics alias, not the active
new-horizons:logistics rank-bonus source. Match the exact active Skill source
as well, without scaling unrelated sources or Navigation. Keep224/248/272.
The legacy fixture also incorrectly assumes its alias producer list is empty:
core and New Horizons Skill definitions differ. The captured list is nonempty,
while the absent-rule220 movement assertions pass. Preserve that observed
legacy behavior and verify producer retention; do not invent a260 baseline.
First failure evidence: UP218-skill-specialty-native.log/XML. Retry pending.

### 2026-10-04 UP217 — Final focused acceptance

Both-target9817 and bounded fixture rebuild65610 succeed. Native23804 passes
11/11 in3.504s with zero skips, UP217-damage-specialty-final.log/XML. Controller
repair preserves the actual accepted Meteor Shower and both target-tier damage
assertions. Ciele save/estimate/cast, Luna stored/triggered damage, legacy and
rational-bound controls pass; Astra review finds no blocker. Earlier failed
build/native evidence remains below. Source/native acceptance is not graphical
or playable delivery.

### 2026-10-04 UP217 — Drift-check fixture must include translation inputs

The focused hero-data suite initially passed11/12; its isolated CMake drift
fixture omitted config/newHorizonsAdventureSpellTexts.json, which the production
guard already reads. Captured stderr identifies the missing file, not a changed
specialty rule or broken guard. Add that required input to the fixture's copy
list without relaxing CMake validation. The same12-test command now passes.
Lesson: isolated fixtures must mirror every required production input.

### 2026-10-04 UP217 — Preserve registered Lua method arity

Both-target build12061 fails at luascript/api/spells/Mechanics.cpp: the registered
member helper has three named Lua arguments, but its C++ signature was extended
to four. A C++ default argument does not preserve a registered member-pointer
arity. Keep the public three-argument member and Lua contract unchanged, and
introduce a distinctly named internal four-argument damage-specialty helper.
Do not relax the registrar assertion or implicitly change existing Lua scripts.
Compiler evidence is UP217-damage-specialty-build.log; acceptance is pending.

### 2026-10-04 UP217 — Target tier must not change the cast controller

Both-target retry9817 succeeds. Native56236 passes10/11 in3.546s, zero skips;
Deemer's accepted Meteor Shower request is rejected. Its high-tier Archangel
target also changes the acting side through higher Initiative. The fixture now
authors low target Initiative and explicitly asserts Player0 controls the action
before casting, preserving both target tiers and the actual cast assertion.
No production authority check is weakened. Retain the first log/XML and verify
the bounded fixture rebuild65610 before rerunning.

### 2026-10-04 UP216 — Translator ownership at tooltip boundary

Native76737 passed6/7 in2.954s with zero skips. The legacy fixture removed the
optional field from an input object, but GameSettings::addOverride merges over
installed defaults and reintroduced it. Mark the complete fixture rules object
as an authoritative replacement and assert the state actually omits the field
before testing the legacy branch. Production validation was not weakened.
The bounded fixture rebuild is77861; retain its terminal result before retrying.

Build32607 failed because the new MetaString tooltip passed the library's
unique_ptr<CGeneralTextHandler> where toString requires const ITranslator*.
Use generaltexth.get(), following its ownership contract. The compiler evidence
is retained privately in UP216-creature-specialty-build.log. No native or
playable acceptance from this failed build; retry the same candidate after repair.

### 2026-10-04 UP214 — Final focused acceptance

Both-target80140 and bounded fixture rebuild14491 succeed. Native39842 passes
all six focused cases in2.362s, zero skips, UP214-magic-resistance-final.log/XML.
Actual74/75 resistance-boundary casts, equipment/aura and detached projection,
positive spell, true immunity, legacy and penetration/Twist controls pass.
Earlier compile/setup failures remain below as lessons, not acceptance evidence.

### 2026-10-04 UP214 — Variant bonus identifiers require typed spell IDs

Build39401 and diagnostic retry32791 fail in the new native fixture, not the
production cap. BonusSubtypeID cannot construct its variant from the raw
SpellID::MAGIC_ARROW enum; explicitly wrap it in SpellID first. Diagnostic
UP214-build-retry.log retains the compiler evidence. Keep the immunity assertion
and correct the identifier rather than weakening targeting rules. Root also
tightens positive Haste evidence to require its spell-sourced bonus rather than
accepting intrinsic creature Speed. Root's first selector conjunction used
logical&& and produced bool rather than CSelector (build58491); use the existing
hasBonusFrom typed-source API instead. UP214-build-fixed.log retains this error.
Acceptance remains pending.

Focused native36148 passes the three new UP214 cases and two existing Twist
controls (5/6, zero skips), but the older penetration case throws before its
assertions: savedFormula relabels current data asv2 without stripping v3-only
structures/placement/variant fields. The cap/penetration case now explicitly
uses the current savedV3Formula. Phase2: sanitize the shared historical-v2 helper
and verify its other consumers; do not relax production saved-rules validation.

### 2026-10-04 UP210 — Final bounded acceptance after fixture repairs

Both-target build59223 succeeds. Native71275 passes all nine Orb controls and
the Fortress construction/binding case (10/10, zero skips,4.479s). The cap case
now authors low Behemoth Initiative to preserve the intended acting controller;
normal unit targeting and authoritative validation remain intact. Four equipped
artifact type/save/removal paths and actual cast/forecast comparisons pass.
Retain earlier failure logs as fixture/API lessons; do not erase them or claim
those earlier runs established acceptance. Data gates8/8 and module drift pass.

### 2026-10-04 UP210 — The cap fixture must preserve the acting controller

Retry68330 passes8/9; changing Fireball from unit to hex targeting does not fix
the cap-case rejection and was not its actual cause. Diagnostic build34997 and
its one-case run establish that spell, exact target and Hero Spell allowance
are valid, but the active stack belongs to PlayerColor1. Replacing a Pikeman
with the higher-Initiative Behemoth changed who acts first, so PlayerColor0's
request correctly fails authoritative controller authentication. Author low
Initiative explicitly for this controlled damage fixture, retaining Behemoth's
HP and every damage/cap/action assertion. Do not loosen production validation.

### 2026-10-04 UP210 — Artifact type and instance identities differ

Native-only resumed build65339 succeeds. Focused run31645 runs9 cases, zero
skips, but passes4 and fails5. Four equipment/save assertions compare
CArtifactInstance::getId (instance identity) to ArtifactID (type identity);
use getTypeId without removing actual equipment or saved-state assertions.
Their damage/forecast/removal assertions report no separate failure. The cap
case's damage/cap calculations pass, but the accepted Fireball action rejects;
diagnose that target/action fixture before claiming coverage. Preserve failed
UP210-elemental-orbs-focused.log/XML; no production acceptance from this run.

### 2026-10-04 UP210 — Forecast fixtures require concrete AI headers

Both-target build30051 stops at776/1041 on the new Orb fixture: DamageEnvironment
and HypotheticBattle are undeclared. Include their actual concrete headers; do
not assume the test precompiled header supplies AI types. The cap fixture also
needs a higher-health creature because getMaxHealth is per-creature, not the
stack's aggregate health. Retain the original log; fixture owner repairs these
without changing production damage or weakening assertions before one retry.

### 2026-10-04 UP210 — Use the actual numeric Mechanics API and JSONC schema

Root prebuild review catches optional-style has_value/dereference calls on
Mechanics::getEffectValue(), whose return is plain int64. Repair the fixture
before building; do not change the production API or weaken numeric assertions.
This is the same Value/value_or class of error already seen on Windows.
The first Python data gate runs8 checks with one error: spell.json contains
supported JSONC comments, so plain json.loads is inappropriate for that schema.
Strip only comment tokens while preserving quoted strings/URLs; no new parser
dependency or product schema relaxation is needed. Keep the failure lesson.
The retry also exposes supported trailing commas in the existing JSONC schema;
remove only out-of-string trailing comma tokens in the test adapter. Production
schema/parser behavior remains unchanged.

### 2026-10-04 UP211 — Explicitly author an empty construction fixture

Both-target build7085 succeeds. Native49630 runs two Conflux cases but fails
the initial-state assertions: TinyH3MBuilder's default town already includes a
Fort and first dwelling. Author an explicit empty building list before testing
the real prerequisite chain; preserve growth, recruitment and save assertions.
Keep UP211-conflux-focused.log/XML as the failed fixture evidence, not a
production-growth failure. No coverage credit until the corrected run passes.

Fixture-only retry28194 builds successfully. Native75381 then passes3/3 in
2.168s, zero skips, including adjacent Tower Library/Brimstone. The pre-init
map hook removes authored DEFAULT/FORT rather than changing production rules;
all tested construction and recruitment still use the authoritative handler.
Retain both failed and passing logs/XML. Python13/13 and module drift pass.

### 2026-10-04 UP211 — Candidate data requires regenerated metadata

An early category-data run passes six checks but its private-composition check
rejects the stale live manifest after captured growth lines were edited and
before the module was regenerated. The diagnostic intentionally requires exact
canonical composition. Complete the isolated content files, regenerate the
module, then rerun; do not weaken that drift guard. No production failure was
established by this premature candidate check.
After regeneration the same private guard still fails because it hardcodes the
obsolete0.14.0 live version while the authoritative product version is0.15.0.
Read config/newHorizonsVersion.json for the live-version guard and test; retain
exact category/text equality, private-path protections and its diagnostic version.
This is a stale diagnostic-tool baseline, not a reason to downgrade the product.

### 2026-10-04 UP-205/UP-206 — Focused acceptance after repairs

Both-target retry68082 succeeds. Native26470 passes6/6 in3.290s, zero skips,
UP205-UP206-focused.log/XML, including repaired legacy control, live/detached
Speed artifacts and adjacent Stables/primary artifact cases. The first combined
Python command also used a nonexistent remembered artifact-pool module name;
the actual guard is test_new_horizons_artifact_data. Corrected selected data/
inventory25/25 and module drift pass. Retain earlier failures as invocation/
fixture lessons, not claims of production defects. No playable promotion.

### 2026-10-04 UP-206 — Use the real BonusSelector header

Combined build45126 fails in the new Speed-artifact fixture because it includes
nonexistent lib/bonuses/Selector.h. The actual shared selector header is
BonusSelector.h. Root repairs only the include; no mechanic change. Keep
UP205-UP206-final-build.log and rebuild before native acceptance.

Retry59322 reaches a second fixture-only API mismatch: HypotheticBattle needs
a shared CBattleInfoCallback, not a raw BattleInfo pointer. The fixture owner
repairs its constructor from existing shared-callback test patterns; do not
change the production AI interface to accommodate an incorrect fixture.
Retain UP205-UP206-final-build-retry.log and rebuild before runtime acceptance.

### 2026-10-04 UP-205 — Legacy fixture must clear captured defaults

Both-target73017 compiles successfully. Native27217 passes the principal land/
boat/pathfinder and Stables/save cases plus the adjacent Stables test, but its
legacy control fails: an empty JSON object override merges with NH defaults and
does not disable the captured movement rules. The tester changes the legacy
branch to null overrides, matching existing legacy fixtures. Retain the failed
UP205-master-logistician-focused.log/XML; rerun after rebuilding the fixture.
No production carry defect was observed in this run.

### 2026-10-04 UP-205 — Activation expectation during candidate verification

The first selected perk/inventory Python run fails because Master Logistician's
candidate activation was added to the registry but not the explicit ACTIVE_PERKS
test expectation. Its follow-on309 count is a stopped subtest, not missing data;
the source registry still contains310 entries. Add only the newly implemented
perk to the expectation and retain the310-count/source-description checks.
Production client57328 succeeds; native acceptance remains pending. Do not
count candidate activation alone as coverage.

### 2026-10-04 UP-203/UP-204 — Focused acceptance after repairs

Client retry16088 and both-target64537 succeed. Native71900 passes6/6 in1.610s,
zero skips; UP203-UP204-focused.log/XML retain the evidence. Artifact removals
assert actual loaded IDs/legal slots and separate Sea Captain's Hat from Ocean
Guidance, retaining all four Movement records. Focused Python24/24 and module
drift pass. Failures below are historical repaired evidence, not active blockers.
No GUI, full assembled-artifact/save interaction or playable acceptance claim.

### 2026-10-04 UP-203 — Native RNG fixture API

Both-target18945 fails compiling the friendly-fire fixture: GameRandomizer
exposes its ordinary RNG through getDefault(), not nextInt directly. Root
corrects both assertions to getDefault().nextInt, retaining the no-RNG-draw
requirement. Evidence: UP203-UP204-focused-build.log. Client retry16088 already
passes; native execution still requires a successful fixture rebuild.

### 2026-10-04 UP-203 — Client target sentinel scope

Client19130 fails compilation at BattleActionsController.cpp because the new
preview adapter refers to undeclared INVALID_UNIT_ID. Use the target's declared
sentinel or predicate, not an invented magic number or transitive include.
Evidence: UP203-friendly-fire-client-build.log. Shared helper/native artifact
build42395 already succeeds; that is not client acceptance. Retry is required.

### 2026-10-04 UP-204 — Artifact fixture removal crash

Native build42395 succeeds, but run46950 exits139. Headless GDB88507 locates
null slotInfo in GameStatePackVisitor::visitBulkEraseArtifacts at the fixture's
MISC1 removal after HEAD/FEET removal. The authored equipment list is not proof
that the H3M importer placed an artifact in that slot. Verify loaded artifact
identities/actual legal slots before every authoritative removal; do not weaken
the exact scaled-value requirements or add a production workaround without
evidence of a reachable gameplay defect. Retain UP204-artifact-first-build.log,
UP204-artifact-first.log and UP204-artifact-crash-backtrace.log. Acceptance is
pending repair and retry; no coverage credit or playable promotion follows.

### 2026-10-04 UP-204 — Focused Python test import path

Root's package-style invocation of test_new_horizons_artifact_data fails before
test execution because that existing module imports test_new_horizons_content
as a sibling, not a package-qualified module. Correct invocation:
`env PYTHONPATH=tools/tests python3 -m unittest test_new_horizons_artifact_data`.
The corrected command passes3/3. This is an invocation failure, not artifact
runtime acceptance or a production defect; keep the sibling test directory on
the import path when running this existing test family.

### 2026-10-04 UP-202 — Final focused acceptance

All failures below are repaired. Client retry32362 and final both-target20101
exit0; native31232 passes4/4 in2.870s, zero skips, retained as
UP202-blood-obelisk-final.log/XML. Siege hero/creature distinction, live
HeroCommand, cancellation/restart/results scope, independent weekly rewards,
saved blessing/history, accepted-result consumption, same-week denial and
next-week reuse pass; adjacent Stables/Fountain remain green. No GUI/playable
promotion or complete physical-damage integration claim.

### 2026-10-04 UP-202 — Siege fixture baseline isolation

Native66208 passes adjacent Fountain/Stables but fails both Blood Obelisk cases.
The fixture attempts to rebuild an already present Fort, and compares creature
Attack with its base while the independent native-terrain bonus adds1. Guard
Fort construction with hasBuilt and isolate the battle from native-terrain
bonuses before applying BattleStart. Retain the exact heroAttack20 and unchanged
creatureAttack requirements; neither failure warrants a production workaround.
Evidence: UP202-blood-obelisk-focused.log/XML. Acceptance remains pending retry.

### 2026-10-04 UP-202 — Creature identifier fixture API

Blood Obelisk test build97506 rejects CreatureID::isValid(), which does not
exist. The fixture now checks the decoded numeric identifiers are nonnegative,
as existing creature fixtures do. Evidence: UP202-blood-obelisk-tests-build.log.
The independent review also caught and repaired a null-defender layout argument
before execution: no-hero siege layout must use its actual defending town army,
not a null hero. No production workaround or weakened mechanic requirement.

### 2026-10-04 UP-202 — Free helpers cannot use visitor friendship

First Blood Obelisk client build61032 exits1: the new free helpers accessed
CGTownInstance::builtBuildings, a private field. GameStatePackVisitor friendship
does not extend to its anonymous-namespace helpers. Use public getBuildings()
and retain the building set locally; do not widen visibility or add friendship.
Evidence: UP202-blood-obelisk-client-build.log. Runtime/native acceptance and
coverage credit remain pending the corrected build and focused fixtures.

### 2026-10-04 UP-201 — Focused Fountain acceptance

Both fixture-only assumptions below are repaired. Final both-target23674
builds and native12287 passes1/1 in1.136s, zero skips, retained as
UP201-fountain-final.log/XML. Actual construction and town visits, defensive/
visiting recipient isolation, saved bonuses/history, per-hero/per-building
weekly entitlement, accepted-result cleanup and next-week reuse pass.
Data/inventory20/20 and module drift pass. No production workaround, full
battle/retreat/replay claim, GUI run or playable promotion. Record those
broader interactions, strategic AI routing and rendered feedback for Phase2.

### 2026-10-04 UP-201 — Town bonus installation in fixture setup

Client38823 and both-target82906 compile/link successfully. First Fountain
native run fails before visits: the fixture calls addBuilding directly, which
only inserts the building ID and does not install local building bonuses.
Real NewStructures application calls recreateBuildingsBonuses. Preserve
UP201-fountain-focused.log/XML and correct setup through the authoritative
construction path, not a production workaround or a weakened Luck assertion.
Fountain native acceptance and coverage credit remain pending the retry.
Retry63580 builds successfully and verifies defending Luck3. The next assertion
incorrectly expects exactly one generated configuration entry; the visited
message also creates a record, so actual size is2. Preserve the interim
UP201-fountain-accepted.log/XML and select the actual first-visit reward in
the fixture. Do not remove the intended visited message from production data
to accommodate a fixture-only list-size assumption.

### 2026-10-04 UP-199 — Final focused acceptance

The fixture include failure below is repaired. Client77558, baseline19532 and
both-target retry82390 pass. Native49376 passes10/10 in3.561s, zero skips,
retained as UP199-academy-focused.log/XML. Actual Academy visits, exact preview/
grant, per-hero/per-building saved history and strict reward/parser/version
guards pass, alongside adjacent Spell Point rewards. Data/inventory21/21,
module drift and Astra reviews pass. This is source/native acceptance only;
graphical testing and playable promotion remain separate. Inherited extreme-XP
calculateXp multiplication overflow is recorded for Phase2, not silently fixed
through unrelated arithmetic changes.

### 2026-10-04 UP-199 — Concrete Bonus types in direct Reward serialization tests

Client77558 and baseline vcmitest19532 pass. Registered Academy fixture
build33367 exits1: directly serializing Reward instantiates its Bonus graph,
but the fixture has only forward declarations of BonusParameters, IPropagator,
ILimiter and IUpdater. Retain UP199-academy-fixture-build.log; the many template
errors have this one missing-include cause. Add the existing concrete Bonus
headers to the fixture, not a production serializer/PCH workaround. Focused
native acceptance remains pending; the stale binary was not executed.

### 2026-10-04 UP-198 — Final focused acceptance

The historical fixture failures below are repaired. Final both-target96096
exits0; native4614 passes19/19 in1.692s, zero skips, retained in
UP198-amplifier-accepted.log/XML. Real hero-local visits, exact saved timed
bonuses, nonstacking refresh, latest expiry and actual computer-winner raising
pass. Data/inventory20/20 and module drift pass; Astra review has no blocking
finding. Source/native acceptance is not graphical or playable delivery.

### 2026-10-04 UP-198 — Compare the named reward, not all raising bonuses

Rank-setup build10255 passes. Native65373 passes18/19: exact named Amplifier
benefit, stored duration refresh/save/expiry and computer raising assertions
pass, but three total UNDEAD_RAISE_PERCENTAGE assertions ignored the hero's
intrinsic Basic Necromancy bonus. Retain UP198-amplifier-final.log/XML. Record
each hero's pre-visit total and compare the10-point delta/nonvisitor unchanged
total; do not mistake ordinary skill bonuses for a kingdom-wide building leak.
Final fixture acceptance still awaits the corrected focused run.

### 2026-10-04 UP-198 — Controlled starting Skill rank in the visit fixture

Retry76110 builds both targets successfully. Focused native46234 passes18/19:
the real computer-winner raising case and adjacent resolver/legacy guards pass,
but the visit case tried to advance an authored starting Necromancy rank as if
it were rankless and hit the preceding-perk requirement. Preserve
UP198-amplifier-focused.log/XML. Control the fixture's starting rank before
using the ordinary Basic acquisition path; do not bypass production progression
or weaken its prerequisite validation. Visit/refresh/expiry acceptance is pending.

### 2026-10-04 UP-198 — Direct concrete-type includes in the Amplifier fixture

Registered fixture build45525 exits1 because its hero-specialty and Skill-handler
accesses had only forward declarations of CHero and CSkillHandler. Retain
build/new-horizons-linux/testing/UP198-amplifier-fixture-build.log. The tester
added the direct concrete headers and braced the GTest conditional; retry76110
is live. Do not weaken production encapsulation or rely on transitive/PCH
includes. Client68479 already passed; principal native acceptance is pending.

### 2026-10-04 UP-196 — Accepted focused retry

The two compilation failures below are repaired, not current blockers. Client
retry94429, baseline vcmitest13751 and fixture retry38283 exit0. Native64173
retains UP196-resource-broker-focused.log/XML:7/7 across two suites in1.373s,
zero failures/errors/skips. A normal Advanced offer, exact shared quotes and
authoritative resource deltas, locality/direction/custom-market guards and real
ResourceTrader -> callback -> serialized request -> server validation pass.
Astra reviews report no blocker. This is source/native acceptance only; new
Windows package, rendered UI and playable promotion remain separate.

### 2026-10-04 UP-196 — Current final callback in the AI fixture

Registered fixture build97281 exits1: MockCCallback derives from final CCallback.
The copied old ResourceTraderTest mocking seam is stale/unregistered and is not
evidence of a supported current test interface. Retained log:
build/new-horizons-linux/testing/UP196-resource-broker-fixture-build.log.
Use a real callback and supported test connection/request capture, not removal
of production final or preprocessor inheritance tricks. Client retry94429 and
baseline vcmitest13751 already pass; focused fixture/native gates remain pending.

### 2026-10-04 UP-196 — Residence-check public API

Client build70173 exits1: CGTownInstance's new Resource Broker check attempted
to read private CGHeroInstance::visitedTown. Retained log:
build/new-horizons-linux/testing/UP196-resource-broker-client-build.log.
Use the existing const getVisitedTown() getter to compare actual associated
towns; do not make saved residence state public or weaken the same-town gate.
The owner applied that narrow correction; rebuild/native acceptance remains
required. Completed objects are retained. Existing unrelated compiler warnings
are not this failure's cause.

### 2026-10-04 UP-194 — Final focused acceptance

The historical failures below are superseded by the final source/native
acceptance, not current blockers. Client retry5595 and test retry49144 builds
pass. The `build/new-horizons-linux/testing/UP194-forgetfulness-final-focused-retry2.log`
run and matching XML report 24/24 tests across seven suites in5.855s, with zero
failures, errors, disabled tests or skips. The focused run
covers the real Forgetfulness/Mindbreaker runtime, detached and BattleAI views,
bonus metadata/serialization, and isolated legacy damage behavior. Independent
Astra review finds no blocking issue. Data/inventory19/19, modulecheck and
diffcheck pass. This establishes source/native acceptance only: no GUI,
playable-delivery or Windows build acceptance is claimed. Full Windows
37177603721 succeeded on e4946162f, which excludes UP194.

Phase2 follow-ups remain: the creature-window status popup still displays the
legacy Forgetfulness text; broad spell/status/action interactions were not
exhaustively certified; and external retaliation-grace interactions remain
unverified. These do not block this focused Phase1 acceptance.

### Historical UP-194 pre-acceptance failures — Explicit suppression helper dependencies

Client build61077 fails because the new helper uses CSelector and BonusList
through forward declarations only. Add their explicit complete-type headers in
the helper implementation; do not rely on transitive/PCH includes. Initial
evidence is UP194-forgetfulness-client-build.log. Retry keeps compiled objects
and a separate log; no native acceptance is inferred from a retry starting.
Retry78695 also fails: root used a nonexistent Selector.h path rather than
the actual BonusSelector.h. Resolve header paths with rg before patching.
Independent review additionally catches an extra closing brace in the isolated
server fixture before compilation; remove it. Neither failure changes gameplay
semantics. Logs remain separate and acceptance still requires successful build
and actual native execution.
Source review also corrects the AI Hydra fixture: its effective live ability
must remain suppressed after an actual cast; only the raw baseline retains it.
The passive-offense fixture must use Medusa, not Mage: the current Tower content
deliberately removes Magi's No Melee Penalty. Do not restore removed abilities
in production merely to satisfy a stale fixture assumption.
Test build30689 fails on three isolated fixture API assumptions: CSpell must
be complete for its interface conversion, BonusList exposes operator[] rather
than at(), and HypotheticBattle branches take a shared callback subject rather
than a stack-allocated parent. Correct these to existing APIs without changing
production; preserve UP194-forgetfulness-test-build.log and retry evidence.
Retry79245 compiles the server fixture but fails the AI fixture constructor:
BattleEvaluator requires CBattleCallback, not CPlayerBattleCallback. Instantiate
the normal callback with the test battle for the read-only spell-choice check;
keep CPlayerBattleCallback for hypothetical attack projections. Preserve the
retry log and run a separate corrected retry, without loosening engine types.
Corrected test build49144 passes. First native69745 runs20 cases in5.368s,
zero skips, with9 passes/11 failures. All four AI cases pass; most server cases
fail because the Lua-created marker loses DEBUFF metadata. Rejected forged
commands also publish StartAction before the new dispatch gate, and the Ogre
fixture's later action advances beyond expiry. Two legacy damage fixtures reject
casts in this active NH profile. Investigate each cause rather than weakening
expectations or reporting partial passes as acceptance. Preserve initial
UP194-forgetfulness-active-focused.log/XML and use a distinct retry report.
Admission/metadata build4418 passes; retry24547 passes21/22 in5.330s, zero skips.
The remaining Ogre fixture trace proves cast round1, post-melee round3 and
post-Defend round4: the fixture's explicit endRound moves beyond the three-round
duration. Remove that redundant advance and assert the marker immediately before
the accepted Defend; a legal Defend may naturally end the final duration round.
Keep UP194-forgetfulness-round-trace.log. The two old damage tests inherited
active NH settings and never initialized its action round; isolate only those
tests in an explicit legacy magic/command snapshot fixture, preserving their
original damage expectations and leaving the shared damage fixture unchanged.
Final candidate65082 passes all22 NH/status/AI cases, but two isolated legacy
cases reject setup because active NH hero ratings require enabled Orders.
Clear the legacy fixture's dependent HEROES_NEW_HORIZONS snapshot too; preserve
the engine's invariant instead of loosening runtime validation. Retain the
24-case candidate log/XML separately from the next corrected final run.
Legacy setup then succeeds; candidate66468 passes23/24 in5.843s, zero skips.
The old fixed ranged damage expectation assumes an unpenalized baseline,
whereas this calculator/profile estimates a blocked shot. The legacy check now
measures the same fixed attacker/target before and after the real spell and
requires exactly half of that positive baseline, rather than changing a
prototype damage value to make the test green. Full-melee expectation remains
unchanged. This verifies the legacy spell multiplier without conflating other
shooting modifiers with Forgetfulness.

### 2026-10-04 UP-192 — Explicit AI path turn comparison type

Client30904 and base test94869 compile successfully. New isolated AI fixture
build36673 fails at two `std::min` calls combining `int` and `uint8_t` path
turns. Root specifies `std::min<int>` without altering route assertions; retry
28857 retains existing objects and a separate log. Preserve both
UP192-forced-march-ai-build.log and its retry log. This fixture compile failure
is not native acceptance or a production movement failure.

AI retry28857 exits0. Server fixture build35388 then fails on missing explicit
BattleLayout definition at a copied restart layout. Root adds BattleLayout.h
and braces around an assertion conditional to avoid a dangling-else warning.
Server retry uses retained objects and UP192-forced-march-server-build-retry.log;
the initial server-build log is retained. Prefer explicit complete-type includes
in new fixtures rather than assuming BattleTestFixture's forward declarations
are sufficient.

Server retry93649 exits0. Initial native79289 runs5 cases in1.750s, zero skips,
with3 pass/2 fail. Both failures are fixture expectations: ordinary stack Morale
is+1, not0, and MoveHero request coordinates use the hero anchor rather than the
visitable tile. Root compares first-round Morale to moraleValWithBonus(-1),
round2 to moraleVal(), and request positions to convertFromVisitablePos.
Actual burst, day markers, both gateway steps and fresh path turns passed.
Keep UP192-forced-march-focused.log/XML; rerun under separate retry reports.

### 2026-10-03 UP-056 — Deferred stale mastery fixture version

Supplemental content/mastery Python batch passes5/6; the existing
test_schema_registration_and_default_mastery_identity still hardcodes0.14.0
while canonical newHorizonsVersion.json and the generated module use0.15.0.
This predates the targeting hint and is a non-blocking Phase2 fixture issue.
Do not alter the live release version or mask it as a translation regression.
The focused live-module union and private-preview translation/overwrite checks
both pass (2/2). Update the stale fixture's version expectation at a later batch
checkpoint; no exhaustive suite is required for this bounded UI hint.

### 2026-10-03 UP-056 — Planned Dimension Door caster ownership

Client build53826 exits1: addDimensionDoorTeleportation has no local `hero`,
although the new caster-aware expenditure call used that name. The earlier plan
builder does own a local hero, so its call is unaffected. Return the bounded AI
file to its owner to use the source actor's hero; preserve planned remaining
Movement rather than substituting the live allowance. Original log:
UP056-dimension-door-policy-client-build.log. Retry must use a separate log and
existing objects; no build or native acceptance is claimed from this failure.
Closure of compile failure: owner binds the hero from srcNode->actor->hero;
client retry55650 builds successfully, retaining
UP056-dimension-door-policy-client-build-retry.log. Native evidence is pending.

Focused build83845 exits1 in the new AI fog fixture: direct access to
players.at(...).team requires the complete PlayerState definition, not its
callback forward declaration. Server fixture compiles; the tester owns adding
the explicit header. Preserve UP056-dimension-door-policy-focused-build.log;
retry separately with the compiled objects and unchanged assertions.
Closure: tester adds direct CPlayerState.h (PlayerState and TeamState); combined
retry22130 builds both targets successfully. Native43587 passes6/6 in2.086s,
zero skips (UP056-dimension-door-policy-focused.log/XML). No production behavior
or assertions were weakened; retained failures remain useful compile lessons.

### 2026-10-03 UP-056 — Explicit callback definition in shared destination helper

Combined build7395 exits1: SummonBoatEffect's new shared predicate invokes
IGameInfoCallback methods with only its forward declaration in scope. Root adds
the direct callback header, preserving behavior and assertions. Retain
UP056-summon-boat-targeting-build.log; retry uses a separate log and existing
compiled objects. No successful build/native evidence is claimed from this failure.
Closure: retry39815 builds vcmiclient and vcmitest successfully; native49715
passes8/8 in2.563s, zero skips. Retain targeting-focused.log/XML alongside both
build logs. No assertions or production validation were weakened.

### 2026-10-03 UP-123 — GoogleTest fixture cannot be final

The original UP123-status-tags-final-build.log exits1 compiling the new
BonusStatusTagsTest.cpp fixture. GoogleTest's TEST_F macro derives generated
test classes from the fixture, so declaring BonusStatusTagsRefreshTest `final`
is rejected by the compiler. Root removed only the `final` qualifier; no test
assertions or production behavior were changed. Retry24367 subsequently builds
both targets successfully under UP123-status-tags-final-build-retry.log.
UP123-status-tags-focused.log/XML passes14/14 from4 suites in0.917s, zero skips.
Retain the original failed log; no assertions were weakened to obtain the pass.

### 2026-10-03 UP-190 — Explicit selector include in new helper

Combined client/test build72676 exits1 at the new Puppet Master helper:
Selector was used without including BonusSelector.h. Root adds the explicit
header, retains UP190-puppet-build.log, and resumes compiled objects in a
separate retry log. This is not native acceptance or a successful build.
Retry17836 also exits1: iterating the returned BonusList requires its complete
BonusList.h definition, not just the selector declaration. Root adds that
explicit header; retain UP190-puppet-build-retry.log and retry2 separately.
Retry2 (session23198) builds both targets successfully. First native invocation
from the repository root exits1 before any test: CONFIG/FILESYSTEM is absent
because development resource lookup uses the current directory. Rerun from
build/new-horizons-linux/bin with absolute isolated-profile and evidence paths;
retain UP190-puppet-principal.log/XML rather than counting this as a mechanic
failure or weakening production loading.
Corrected native16624 executes6 cases in1.751s, zero skips:4 pass,2 fail.
Real hex cast/controller/allegiance, selected shot/Sanctuary, active creature
ability/provenance and detached AI valuation pass. Wait/lifecycle fixture fails
ordinary Slow eligibility and its afterOneRound duration assertion; JSON bonus
fixture sees unresolved named spell SID -1. Retain principal-retry log/XML.
Tester diagnoses actual eligibility, round advancement and identifier lifecycle
without weakening spell immunity/duration or claiming a6/6 pass. The newly
approved Berserk-removal correction is not included in this binary.
Incremental Berserk client86698 exits1: BonusSourceID's variant requires a
typed SpellID, not SpellIDBase::Type. Root wraps BERSERK in SpellID before
constructing BonusSourceID; preserve UP190-puppet-berserk-client-build.log
and retry separately. Apply the same typed-ID convention in new fixtures.
Final fixture build15580 exits1: CStack exposes creatureId(), not
getCreatureID(). Root corrects the two innate-source fixture calls, preserving
the source/SID assertions. Retain UP190-puppet-final-test-build.log and retry.

Closure: client retry90585/test retry72097 exit0. Fixture repairs use direct
receptivity checks rather than a spent Hero Action, one exact round transition,
and the named-spell JSON mod scope; production gates remain intact. Final
native97155 passes8/8 in2.290s, zero skips. Adjacent Berserk runtime/AI guard
71741 passes6/6 in1.986s, zero skips. Retain UP190-puppet-final.log/XML and
UP190-berserk-adjacent.log/XML alongside every earlier failure; no GUI or
playable acceptance is implied.

### 2026-10-03 UP-190 — Use the actual Python test import surface

The proposed tools.tests.test_new_horizons_magic_data module does not exist.
The perk/inventory cases in that combined command passed19/19, but the command
failed overall at import; it is not a20-case pass. Existing magic_v2_data and
map_magic_schema modules import their sibling test_new_horizons_content, so
module-style execution requires PYTHONPATH=tools/tests (or discovery rooted
there). Root preserves these failed invocation results and corrects the runner
instead of changing production to address test-import errors.

Corrected invocation runs23 cases,22 pass; the complete legacy-v1 schema
projection case fails. Read-only comparison of HEAD's committed magic profile
and the dirty profile through the same legacy_rules/validator reproduces that
failure in both, while the current v3 profile validates with zero errors.
Record the existing legacy fixture/schema integration mismatch for Phase2;
do not attribute it to Puppet Master or expand this source slice into legacy
fixture repair. The23-case command is not an all-pass gate.

The new Puppet Master data fixture initially used json.loads on the existing
JSON-with-comments spell file and failed during setup. Reuse the established
parse_jsonc helper, which preserves quoted comment-looking text. Corrected
focused registration/perk/inventory invocation passes22/22. This is data
validation only, not native spell/action execution or visual acceptance.


### 2026-10-03 UP-179 — Dependent template call in effect recorder

Hypnotize capture client39016 exits1: the generic packet lambda requires
`bonus.parameters->template toCustom<JsonNode>()`, not the non-dependent
spelling. Root corrects that call without changing capture behavior. Original
UP179-hypnotize-client-build.log is retained; retry26745 uses a separate log.
Do not claim native acceptance from the initial compile or stale test binary.

Test11554 exits1 because CUnitState::damage takes a mutable int64 reference:
the new fixture passed a const local and a temporary. Root uses mutable wound
locals, retaining the exact health/boundary assertions. Original
UP179-hypnotize-test-build.log is retained; retry uses a separate log.

Test retry7191 builds successfully. Native60997 executes22 cases:9 planner/
parser passes,13 real Astral cases fail at cast eligibility before effect
application, including the six pre-existing boundary cases. Preserve
UP179-hypnotize-principal.log/XML. Diagnose the real fixture/content failure;
do not relax production eligibility or claim acceptance from parser tests alone.

Diagnosis: the direct BattleStart fixture remains at round0 with allowance
currentRound=-1, so the ordinary Hero Action gate rejects every Hero cast.
The Astral fixture now enters its first playable round through BattleNextRound,
which initializes allowances normally. Limit this setup repair to the focused
fixture, not production rules or the entire unrelated legacy spell suite.

That setup repair is necessary but not sufficient: fixture build36554 exits0,
native1690 still reports9/22 passes with the same13 eligibility failures.
Retain UP179-hypnotize-principal-retry.log/XML. Instrument the actual Problem
messages and cast/target rejection boundaries before making another repair;
the previous allowance diagnosis did not establish the sole cause.

Diagnostic build47255 exits0. The single Pikeman probe proves general casting
is allowed (callback OK, round1 allowance present) but target admission fails:
the saved-v3 spell is Chaos, while the old fixture grants only vanilla Schools.
Its actual effectLevel is0, effectValue35 and specialty-adjusted ceiling40,
not the fixture's assumed Expert value345. Repair the explicit fixture School
and expected saved-v3 SP coefficient; preserve both exact-boundary probes.

School-fixture build5484 exits1: the new Chaos helper was placed on the
parameterized derived fixture but also called by a base-fixture refresh case.
Root moves that helper to the shared test base; only the focused Hypnotize
cases call it. Keep UP179-hypnotize-school-fixture-build.log and retry separately.

Retry30562 compiles, but native27718 still passes9/22. School/first-round
assertions now pass; target admission still rejects the old scale assumption.
The canonical expanded-attribute profile has powerDivisor10, and expected
values must use the loaded spell's authored base/level powers rather than old
Heroes III numeric comments. Compute the independent expected formula from
those data and saved145% coefficient, not from the capture API being tested.
Retain UP179-hypnotize-principal-final.log/XML despite its historical filename.

Data-fixture build39994 exits0; native10260 passes21/22 in5.990s. The sole
refresh failure is an invalid opponent recast: battleMatchOwner uses original
unitSide, so a defender cannot target its own originally defended unit with
Hypnotize. Preserve this production behavior. Test a same-caster refresh after
specialty removal and normal next-round action renewal; add an ordinary duration
bonus so the original effect actually survives that boundary. The existing
ownership-policy interaction belongs in Phase2, not this metadata change.

Final focused build39697 exits0. Native5489 passes all22 cases in5.958s,
zero skips, with a same-caster accepted recast after specialty removal and a
real round transition. The original ceiling is retained and the timer is
restored. UP179-hypnotize-refresh-final.log/XML are the accepted evidence;
older filenames containing "accepted" or "final" do not override their
recorded failing results. Independent final review has no blocking finding.
An explicit specialty-absence assertion and detached refresh parity are
deferred test strengthening, not a production eligibility change.

### 2026-10-03 UP-179 — Verify coordinator termination before resuming

Retry15193 exits143 after179/320 without a compiler error. Root inspects process
state: coordinator is absent, transient remaining compiler children finish, then
no cmake/ninja/compiler remains. Cause is unestablished. Resume retained objects
under a new retry log only after that check; never equate a polling timeout with
termination or start a second build while an orphaned coordinator is live.

### 2026-10-03 UP-179 — Complete Bonus serialization types

Client8019 passes. Test94692 fails in NewHorizonsRealityWarpPlannerTest.cpp:
instantiating Bonus serialization requires complete BonusParameters, limiter,
propagator and updater types, not only Bonus.h forward declarations. Add their
defining test headers; do not alter engine interfaces or remove wire assertions.
Retain UP179-prerequisite-test-build.log and reuse compiled objects in a distinct
retry. Earlier invented enum names/member lambda captures were corrected by
source review before the build. No native prerequisite acceptance is claimed.

### 2026-10-03 UP-129 Pact — Respect fixture skill progression

Test build retry passes, but fresh UP129-pact-principal.log/XML records23/27:
all four new encounter cases fail during setup with the preceding-perk-tier
advancement exception. Correct setup through normal rank/perk selection;
do not disable progression validation or weaken encounter assertions.
Retain the failing artifacts and run a distinct corrected principal retry.

### 2026-10-03 UP-129 Pact — Concrete fixture includes

Client66210 passes. Test build54732 fails in the new discounted-acceptance
fixture because StackLocation is only forward-declared by PacksForClient.h.
The fixture must include the type's defining header before constructing it;
do not change production interfaces or remove the actual transfer assertion.
Retain UP129-pact-test-build.log and use a distinct retry log. Native Pact
acceptance has not run, so registration status is not accepted coverage.

### 2026-10-03 UP-129 weekly Diplomacy — Review before build

Root review caught a potential removed-guardian dereference in movement after
objectVisited: Tribute can remove that object during the callback. Snapshot
guardian/destination IDs before calling it; do not dereference removed objects
afterward. Require a real movement fixture in addition to direct-visit tests.
Hero down-save validation also must precede base payload writes. The old-reader
packet fixture uses only a documented synthetic heroId prefix because the new
packet itself is correctly forbidden on old writers. Preserve that rejection.
Finally, hero-aware guard filtering must retain the cached no-guardian fast path;
do not enumerate nearby monsters on every ordinary pathfinding tile. These are
source-review corrections, not observed crash/performance reproduction claims.

### 2026-10-03 UP-129 Envoy — Actual sight authority in hidden-target fixtures

Retry principal2776 passes14/15; the remaining visibility assertion fails because
CGHeroInstance::getSightRadius reads the library engine settings, not the map
override used by the fixture. Replace that ineffective override with an
authoritative GiveBonus sight penalty followed by FoWChange HIDDEN; assert the
actual sight radius and visibility before testing the popup. Preserve the failed
retry log/XML. Production visibility/range guards are unchanged. A fresh focused
rebuild/rerun is required before accepting Envoy.
Resolution: test68817 exits0 and final principal retry2 passes15/15 in3.282s,
zero skips. The real hidden-target precondition and positive reveal both pass;
no production visibility guard or assertion was weakened.

### 2026-10-03 UP-129 Envoy — Hero visitable offset and perk argument

Client69085 and test68880 pass, but first principal61370 passes only the prior
10/15 cases. New fixture positions assumed the hero's artwork anchor was its
visitable tile: builder x5 produces visitable x4, so neutral x10 is distance6,
not5. Derive positions from the actual visitable coordinate and retain exact
squared-distance boundary assertions. The Basic Envoy helper also supplied
Envoy in the Advanced argument; select it in the Basic argument through the
ordinary offer path. Preserve UP129-envoy-principal.log/XML; do not loosen the
production five-tile gate or bypass perk selection to conceal fixture errors.

### 2026-10-03 UP-129 — Concrete neutral type and HeroPtr reference

Initial client95849 fails because AIGateway uses dynamic_cast to CGCreature
without including its definition. Add CGCreature.h explicitly; do not rely on
the MapObjects umbrella or precompiled-header accidents. Retry88847 exposes
that HeroPtr::operator* returns a pointer rather than a hero reference. Pass
*heroPtr.get() to the shared forecast. Keep UP129-client-build.log and
UP129-client-build-retry.log separate from retry2 evidence. Review also caught
legacy aggression gating of deterministic joins, rejection of initial FLEE
responses, and joining-enabled response recomputation after declining a join.
Fix the authoritative encounter path; do not soften threshold or query tests.
Native acceptance remains pending until the frozen fixtures build and run.
Test build15477 then exits1 on fixture API misuse: the concrete translator
definition is missing, JSON serializer constructor arguments are wrong, and
full-object binary serialization needs complete bonus updater types. Preserve
UP129-test-build.log. Repair fixture includes/API calls, not production guards
or required outcome assertions; focused execution still has not occurred.
Test retry66042 exits0. Source review then catches the JSON fixture loading into
an already populated neutral, while map option loading inserts slot0 and expects
an empty instance. Load saved JSON into a fresh CGCreature and preserve the
original for binary checks; do not alter the production loader for this fixture.
After the fresh-object correction, test retry2 handle77176 exits0. Principal
56667 runs10 cases in1.766s:5 pass,5 fail; retain UP129-principal.log/XML.
Normal rank advancement used the legacy core Diplomacy ID rather than the
module-decoded skill ID. The rank-zero free case also exposes an eligibility
owner-convention issue under investigation: ordinary unflaggable wandering
creatures must not be excluded merely for lacking the capturable NEUTRAL owner.
No coverage is accepted until focused repairs and native rerun succeed.
Final client67358/test39859 pass. Principal retry10/10 passes1.870s, zero skips;
the raw threshold/real query/payment/transfer/wire requirements now pass.
Adjacent7/8 passes2.440s; same-hero OrdinaryMerge final exact-fit response is
false and reproduces in isolation. Its player field is correctly populated;
do not assert an omitted-actor cause. Rejection cause is unresolved, without
observed crash or corruption. Retain UP129-adjacent and merge-diagnostic log/XML
for Phase2; do not weaken expectations to conceal it.

### 2026-10-03 UP-188 — Callback definition required by town-name lookup

Client62687 fails in UIHelper's new callback `getTown` call because CPlayerInterface
only forward-declares CCallback. Include the actual callback header at the call
site; do not remove authoritative destination identity or town-name feedback to
avoid the compiler error. Preserve UP188-client-build.log. UI owner repairs only
the include, then root runs an incremental retry before native acceptance.

### 2026-10-03 UP-187 — Old-format fixtures and canonical source identity

Review caught a fixture attempting to write populated new special-casualty maps
in an old format; the production prewrite rejection was correct. Clear fields
on the old writer source, and seed only the read target when checking resets.
Actual magical-casualty fixtures must inflict at least one death after the target's
magical reductions; use sufficient Spell Power rather than relax provenance checks.
After the user clarified Ossuary in the canonical document, the data identity
test exposed its stale registry source hash. Update sourceSha256 and regenerate
the module with each canonical amendment; repaired data/inventory gates pass19/19.
No production guard was weakened for these setup/identity corrections.
Fresh native principal passes32/32, but adjacent11/12 has the Vampire capture
case rejected at hero spell submission. Vampire initiative differs from the
slower original Pikeman/Golem fixtures; establish the legal active-side window
before casting rather than relax server action ownership. Preserve the original
UP187-adjacent log/XML and rerun after focused diagnosis/repair.
The fixture-only incremental rebuild17335 passes. Final retry principal32/32
and adjacent12/12 pass, zero skips; the Vampire case uses the real attacker's
round-1 window. Original failing evidence remains separate from retry logs.

### 2026-10-03 UP-185 — Creature service interfaces are not concrete entities

Client57879 fails in the new upgrade-availability helper: CreatureService
`getById` returns the public Creature interface, not CCreature with its public
`upgrades` set. Use the supported upgrade accessor or appropriate concrete
lookup; do not weaken configured-upgrade validation to make compilation pass.
Preserve UP185-client-build.log. Runtime owner repairs the API mismatch before
root resumes the same incremental build; source/native acceptance stays pending.
Resolution: concrete `LIBRARY->creh` lookup preserves the configured-upgrade set.
Client retry43912 and test65198 pass. Principal26/26 and adjacent9/9 pass on the
freshly linked binary, zero skips; the repaired source is independently reviewed.

### 2026-10-03 UP-184 — Google Test fixtures cannot be final

Client95124 passes. Test53850 fails compiling the new Soul Harvester fixture:
Google Test generates derived case classes, so its fixture cannot be `final`.
Remove that qualifier from the fixture; do not alter production inheritance.
Retain UP184-test-build.log. Resume the same incremental build only after the
test owner freezes the correction; native acceptance remains a separate gate.
Resolution: retry15042 exits0. Focused principal20/20 and adjacent9/9 pass,
zero skips, on the freshly linked binary. No production inheritance change.

### 2026-10-03 UP-181/182 — Standalone state headers need their own constants

Core51743 and client32699 pass, but test15320 fails compiling the new
BattleDeploymentOrderTest: BFIELD_WIDTH is unavailable when including the state
header directly. The constant belongs to BattleHex.h, not GameConstants.h or
NumericConstants.h. Add the direct BattleHex.h include to BattleDeploymentState,
instead of depending on consumer include order or adding a fixture-only workaround.
Preserve testing/UP181-UP182-test-build.log. Repaired build18902 and final frozen
incremental/native checks remain separate acceptance gates.
Repaired18902 later exposes a fixture-only type error: MasteryLevel is a namespace
of rank constants, not a parameter type. Use the existing integer rank contract
in the legal-acquisition helper; do not change production Skill types. Resume
incrementally only after the fixture owner freezes the correction.
Resolution: resumed19165 and frozen98551 pass; principal12/12, adjacent6/6 and
production-active12/12 pass, zero skips. Final fixture31761 builds successfully.
The fixes preserve production rank semantics and make the header self-contained.


### 2026-10-03 UP-177 — Never wait for packet realization under the GUI lock

Source review finds that the Portal source picker enables waitTillRealize while
the GUI owns the interface mutex. CClient::sendRequest releases only the game-
state mutex; incoming state/ack processing also needs the interface mutex.
This can deadlock despite a correct authoritative request. Use asynchronous
state acknowledgement and revalidate the current town/window lifetime before
opening recruitment. Do not manually drop the GUI mutex or mutate the link
locally. This was caught before execution, not a reproduced user crash.
The same review catches an undefined Portal-army local and missing New Horizons
gates in AI source discovery and legacy-row exclusion. The sole AI owner repairs
the variable and restores inactive-profile behavior before any build.
Client79950 then fails on three auto-pointer declarations for a shared_ptr
callback in CCastleInterface.cpp. Keep the existing callback ownership idiom:
reference or shared_ptr copy, not deduced raw pointer. The UI owner repairs all
three sites; retain UP177-portal-client-build.log. Client/test retry and native
acceptance remain separate gates.
The repaired combined build34651 is then terminal exit143 at272/374, without
a compiler error in its retained log. Process inspection confirms cmake/ninja
has stopped; the termination cause is unknown. Only after that check, resume
incrementally as75438 with a new log, preserving completed objects and the
original evidence. Do not mistake an interrupted build for a successful link.


### 2026-10-03 UP-175 — Repeat detached mutation receiver and TU scope

Client build17500 exits1. Four new AttackPossibility consumption sites call
removeUnitBonus on CUnitState, which has no such method. The UP171 lesson below
already records this receiver constraint: use the branch-local StackWithBonuses
returned by HypotheticBattle::getForUpdate, never widen CUnitState's API merely
to compile a perk. Independent review also finds BEx references an AP anonymous-
namespace helper absent from its own translation unit; add a local helper or
inline selector. That separate scope issue is source review, not a reported
compiler error from this build. Retain testing/UP175-night-prowler-client-build.log;
repair through the sole AI owner, freeze, rebuild, then run native acceptance.
Pre-build fixture review additionally repairs non-constexpr BattleHex constants,
a flying Angel used for a non-flying Ghost Walk path, and a mismatched log
assertion. A persistent Basic Backstab instead of one-shot Ambusher isolates the
Night Prowler follow-up baseline. Do not weaken faction/movement/lifetime rules.
Resolution: guarded branch-wrapper removals and a BEx-local helper pass repaired
both-target build90975. Principal3/3 and production-active3/3 pass, zero skips,
each total1.110s. The failed build and original logs are retained; no shared
unit-state API, production duration or acquisition validation was weakened.

### 2026-10-03 UP-173 — Fixture pre-build review guards

Readiness review catches two fixture-only mistakes before a test build: fatal
ASSERT macros in a value-returning geometry helper, and a Castle defender used
for a faction-locked Dungeon perk. A failed fatal assertion returns void and
cannot compile in that helper; use an explicit ADD_FAILURE plus a typed return.
Create two distinct allowed Dungeon heroes and retain legal acquisition checks.
Do not loosen runtime faction gates or misreport this source review as a failed
native run. The owning tester repairs the unregistered fixture before freeze.

### 2026-10-03 UP-171 — Detached bonus mutation receiver

Combined Evasive Shroud build92831 exits1: BattleExchangeVariant's helper
casts to battle::CUnitState, which has no addUnitBonus/removeUnitBonus methods.
These mutations belong to the detached StackWithBonuses wrapper returned by
HypotheticBattle::getForUpdate. Repair the bounded AI receiver, not the shared
unit-state API or authoritative bonus graph. Retain
testing/UP171-evasive-shroud-baseline-build.log; no stale native run or
activation is acceptance. Require a frozen-source both-target retry.
Retry76200 exits0. The first three-case native gate passes2/3, zero skips:
the1,000-peasant target dies after the preceding negative-control attacks plus
the flanking hit, so it cannot demonstrate ordinary retaliation. Preserve
UP171-evasive-shroud-native.log/XML. Repair only fixture survival controls,
using current damage-range bounds; refresh/real-expiry and Bonus roundtrip
already pass. Do not reduce production damage or claim the failed case passed.
Resolution: receiver repair plus fixture survival bounds (3,000 peasants and
20 Angels, with pre-hit damage/retaliation assertions) compile in both targets.
Repaired principal3/3 and final production-active3/3 pass with zero skips;
the latter takes5.730s and uses no planned-only registry override. The original
failure logs remain retained. No production damage or duration rule was changed.

### 2026-10-03 UP-170 — Observe activation-timed effects before expiry

The first No Escape native gate passes2/4 with zero skips. Its combat log
confirms the effect triggered, but the fast victim's automatically advanced
Creature Activation expires it before the fixture reads the live bonus.
Separately, blockRetaliation(victim) adds BLOCKS_RETALIATION to the wrong unit;
it does not prevent the victim's response. Retain UP170-no-escape-native.log/XML.
Repair fixture controls and the observation interval, not the production timer:
prevent retaliation on the attacking stacks, spend the victim's real activation
first, and retain an unacted reserve before checking refresh. Then explicitly
verify expiry on its next genuine activation. Acceptance remains pending.
Fixture retry builds18646/29029 catch const CStack* passed to the mutable
retaliation helper and battle::Unit* passed to a CStack-only action helper.
Use mutable flankers for bonus setup and the common Unit interface for actions;
do not cast away constness or run the previous binary as repaired evidence.
Resolution: both-target retry3 build33849 exits0; principal4/4 passes in8.112s
and the final production-active gate passes4/4 in8.145s, both zero skips.
Original failed logs remain retained; no production timer change was required.

### 2026-10-03 UP-161 — Unsigned 64-bit battle snapshot encoding

Core-only build29456 exits1: BinaryDeserializer deliberately rejects direct
uint64_t serialization. Preserve raw starting Army Value as optional uint64_t
in memory, but encode a present value as two supported uint32_t low/high words.
Do not loosen the serializer, truncate strength or replace unknown with zero.
Retain testing/UP161-field-study-core-build.log; require a frozen-source retry
and current roundtrips including present zero and UINT64_MAX before activation.
Core retry6495 exits0 and baseline client/test57072 exits0. Registered fixture
build16347 exits1 on fixture-only errors: queryAs returns a raw query pointer,
an unused neutralization helper needs an incomplete CHero definition, and
stackAt belongs to the reference fixture rather than its shared base. Repair
with dynamic_pointer_cast of the owning query, remove the unused helper and
reuse the small stack lookup locally. No production contract change is needed.
Retain UP161-field-study-fixture-build.log; no stale native run is acceptance.
Fixture retry48428 exits0. Principal11-case native gate exits139 after two
hero-victory cases pass, while the wandering-army case runs. No XML is emitted;
retain UP161-field-study-native.log. Activation is held. Diagnose only that
case with a batch backtrace and repair the actual failing path before retrying;
do not misreport the crash as a skipped or completed remaining matrix.
Isolated debugger identifies a test lifetime error: neutral battle completion
removes the battle synchronously before expectWinnerXp reads it. Hero-vs-hero
dialogs happened to retain it. Capture independent expected inputs before the
terminal action and keep actual awarded-XP checks; do not change production
cleanup to accommodate the fixture. Backtrace retained as
UP161-field-study-monster-backtrace.log; repaired native acceptance is pending.
Resolution: fixture-only precompletion context capture removes the dangling
BattleInfo access. Both-target rebuild31335 exits0; isolated retry1/1 and full
principal11/11 pass with zero skips. Keep original failure/backtrace as evidence,
not a production crash claim. Actual published-to-awarded XP check still passes.

### 2026-10-03 UP-160 — Pointer metadata versus object serialization guards

Pre-native review catches an incorrect fixture expectation: writing a hero
pointer emits pointer nullness/ID/type metadata before invoking the hero's
payload guard. A zero-byte payload test must serialize the hero by value/reference
(*firstHero), not its pointer. Preserve the separate pointer roundtrip; do not
weaken the guard or claim whole-object-graph atomic writes. Build81506 is live
when this is found; repair the fixture after it terminates and rebuild before
native acceptance. No failed native run or production serialization failure is
claimed from this source review finding.
Combined81506 terminates with exit1 in the new Investor fixture: redundant
namespace close at458 and incomplete ObjectTemplate/bonus updater/propagator
types while instantiating direct hero serialization. Repair fixture structure
and direct defining includes using existing hero-roundtrip patterns; do not
remove persistence assertions or alter production serializers to make it compile.
No native acceptance was executed. Retain UP160-investor-build.log and require
one serialized retry after the bounded fixture repair.
Retry45920 terminates with exit1 in the new Estates AI fixture: mutable
PlayerState::getTowns returns vector<CGTownInstance*>, while BuildAnalyzer's
read-only quote requires vector<const CGTownInstance*>. Use a const PlayerState
view for forecast calls; do not loosen the production quote API. The repaired
server fixture compiles. Preserve the retry log; native execution remains held.
Retry2 40823 exits0. The focused14-case native gate then passes13 and fails
one copied-hero income assertion,zero skips. A standalone CMemorySerializer
copy does not rebuild the complete bonus graph; compare its income against its
own zero-Investor baseline, with explicit active-perk and saved snapshot checks.
Do not claim full-game income/save acceptance from isolated object copying.
Preserve UP160-investor-native.log/XML and rebuild the repaired fixture before
rerunning its principal gate.
Resolution: retry3 both-target build exits0; repaired serialization case passes
1/1,zero skips. After production activation, all5 Investor cases pass,zero
skips. Other13 cases from the earlier principal gate passed unchanged. Final
independent review finds no blocking runtime or fixture issue.

### 2026-10-02 UP-158 — Redeployment direct interface include

Combined12-job build95627 terminates with exit1: TacticsHandler.cpp calls
getDeploymentState on a forward-declared IBattleInfo. Add its direct defining
header; no mechanic/assertion change is justified. Preserve UP158-redeployment-
build.log and require the serialized retry before executing native acceptance.
Data/inventory19/19, generated-module drift and package preflight91/91 pass,
but none of those prove the C++ build or Redeployment's principal path.
Retry56408 builds both targets successfully. Native principal15/15 and activated
4/4 pass with zero skips; final incremental build and data/package gates pass.
Activation required updating the explicit test allowlist and the inventory's
Implementation/Art columns correctly; their failed intermediate checks were
not C++ mechanic failures and were repaired without weakening assertions.

### 2026-10-02 UP-154 — Windows deployment source-guard drift

Full Windows run37086471771 on b954d071 terminates in failure before compilation.
Job111097728868, Package audit regression tests, runs91 tests with one failure:
the spell-routing source guard still requires `on || tacticsMode || !canCastSpells`
after production correctly changed to global `deploymentPhase` blocking.
Repair the guard and mutation checks without weakening global deployment or
spent-action feedback assertions. This is source-guard drift, not an observed
MSVC failure or infrastructure outage. Compilation/package stages were skipped;
retain this run and require a repaired preflight before another full dispatch.

### 2026-10-02 UP-156 — Defend lifetime and turn eligibility

The base rank audit finds round rollover clearing Defend's action flag and exact
stance provenance while its STACK_GETS_TURN bonuses remain. A first narrow repair
retains those fields and baseline25126 compiles, but review catches willMove's
!defending guard: the retained flag would suppress the next activation. Do not
execute/promote that baseline as accepted behavior. Reuse the existing
UNIT_DEFENDING duration tag for effective stance, retain provenance until next
activation, and keep the round action flag reset. Projected AI Defend must tag
its clone too. Native acceptance must include actual turn-queue selection, not
only a direct afterGetsTurn call. Mastery allocation remains separately planned.
The revised effective-stance predicate also exposes Second Wind's use of
`moved() || defended()` as action-completion eligibility. A prior-round stance
must not qualify before the current normal activation. Correct that consumer to
current-round action state and include focused eligibility evidence; do not
defer a newly introduced incorrect core mechanic as mere integration polish.
Client retry84116 succeeds. Combined fixture build98647 fails because the new
helper treats the MasteryLevel constant namespace as a type; use the existing
integer rank API. Independent fixture review also catches the last striker's
attack potentially advancing the round before unconditional endRound. Retain a
slower unspent reserve and assert round1 before checking spent retaliation, then
advance once to round2. Keep original stance and real-queue assertions; neither
failure justifies relaxing production rules or claiming native acceptance.
Repaired combined27682 exits0. Native37434 passes16/16 in4.453s, zero skips,
including actual next-round queue selection and Second Wind's prior-round/current-
round distinction. Original failed logs remain; no immutable snapshot is promoted.

### 2026-10-02 UP-154 — focused data-gate command correction

Root review found an additional blocking construction-order defect while
baseline build42019 was running: BattleWindow's constructor invokes
`tacticPhaseStarted(false)` for AI-only deployment, whose new `blockUI(true)`
call dereferences stacksController before BattleInterface creates it. Astra
confirmed the missed path. After42019 is terminal, move that blocking call to
the inactive-local branch of deploymentPhaseChanged; initial blocking already
runs after controller construction. Preserve the live build; do not execute its
binary as accepted native evidence before the repair is recompiled.
Baseline42019 then ends with exit143 at386/542, no reported compiler error;
process inspection confirms no surviving build children. Its termination cause
is not established. Repair moves the call after controller initialization, and
the frozen fixture is registered for incremental retry10000, logUP154-fixture-build.
Do not reuse an incomplete or stale binary as native acceptance.
Repaired build10000 passes both targets. Principal35799 runs8 cases:
7pass/1fail, zero skips,23.519s. All state/acquisition/phase/compatibility cases
pass. The movement case passes its rejection assertions, then finds no reachable
legal endpoint for its acceptance assertion atline462. Diagnose the fixture's
first-free placement and added blockers before changing production movement;
do not remove the required accepted-move assertion. Preserve UP154-principal.log
and XML. Fixture owner is assigned the bounded geometry repair; counts unchanged.
The fixture now proves an accepted move before adding blockers and retains all
rejected-move assertions plus the real END request. Adjacent11237 passes10/10
in17.054s, zero skips. Fixture-only retry build39337 passes both targets; the
principal retry must pass before accepted coverage increases.
Principal retry24146 passes8/8 in23.514s, zero skips. No production rule was
weakened to pass the fixture. Tactics source/native acceptance now advances
coverage to184/310; broader UI/AI rendering/execution remains Phase2.

The first data-gate invocation named the nonexistent Python module
`tools.tests.test_nh_ui_asset_inventory`; the perk tests passed but unittest
reported one import error. Discover the actual module with `rg --files`:
`tools.tests.test_new_horizons_ui_perk_inventory`. The corrected paired gate
passes19/19, and generated-module drift validation passes. No production code
or assertions were weakened. Native deployment acceptance remains pending.
Interim Astra review found no shared-state/server blocker; defensive rejection
of pending deployment attached to a post-opening round is Phase2 hardening.

### 2026-10-02 UP-153 — detached views and activation-token assertions

Both-target build9087 succeeds. First principal native32568 runs6 tests:
4pass/2fail, zero skips. Preserve `UP153-principal.log` and XML. Gameplay
transit, occupied endpoint rejection, current-controller footprints, occupied
Fire Wall damage and hidden Quicksand stopping pass their assertions. The
remaining failures compare an uncopied hypothetical unit against a frozen
snapshot and compare the Fire Wall trigger token against the next activation.
`HypotheticBattle` lazily exposes the subject until `getForUpdate` creates a
branch-owned unit. Pin the detached copy before changing the live battle; do
not change production copying policy to satisfy this fixture. Capture the
activation token before the authoritative movement action, because completing
movement can advance the battle queue. Retry acceptance remains pending.
Fixture-only retry38200 builds both targets. Principal13004 passes6/6 in4.330s;
adjacent87077 passes10/10 in2.280s, zero skips. No production rule was weakened
to satisfy the failed assertions.

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

### Cabir portrait resource-fixture prefix mismatch (2026-10-06)

The extended native resource fixture builds successfully, but its first run
fails before checking Cabir portrait pixels: a raw `ImagePath` resource-existence
check ignores the module's `SPRITES/` mount. Production RenderHandler resolves
`SPRITES/`, `DATA/`, then the unprefixed image. Correct the fixture's precheck
to match those actual routes, retaining missing-resource assertions; do not
change production mounts or replace a missing resource with a placeholder.
The same precheck correction is required for standalone animation-frame PNGs.
Corrected fixture rebuild succeeds; the default four-scale portrait/native
renderer test passes1/1 in2.17s. Animation validation remains separately opt-in
and pending the complete exported resources.
The bounded run reports missing music because its isolated profile mounts only
original Data, and also reports the custom Repair schema rejecting its `type`
field. These warnings are separate from the proven resource-prefix failure;
Repair's actual native casting cases already pass. Track schema-warning cleanup
and profile completeness without claiming that either caused this failure.

The first animation-enabled run loads both custom descriptors but fails a
projectile provenance assertion built from an extension-sensitive substring.
Use canonical prefixed AnimationPath identity instead, preserving the exact
original-resource assertion and useful actual/expected diagnostics. Rebuild
and activated retry pass1/1. Independent integration review also catches an
outdated base descriptor copied before directional sequence completion; copying
the final descriptor restores exact export/runtime parity. The final activated
fixture checks actual creature bindings and Master climax3 and passes1/1 in2.28s.
Neither failure proves a gameplay defect or establishes battlefield motion.

No credentials, workstation paths, purchaser content or raw research dumps in
these notes. Keep historical failures even after repair, but label their scope.
Do not claim the pipeline is future-proof: tests reduce recurrence and catch more
failures earlier; new dependencies and environments can still reveal defects.
