# New Horizons — release failures and regression lessons

## Purpose

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

```text
Failure ID / CI run or local command / frozen source identity:
Observed error and affected stage:
Confirmed cause (or explicitly unconfirmed hypothesis):
Minimal fix / regression test names:
Local result / actual target-platform result:
Remaining gate and next action:
```

No credentials, workstation paths, purchaser content or raw research dumps in
these notes. Keep historical failures even after repair, but label their scope.
Do not claim the pipeline is future-proof: tests reduce recurrence and catch more
failures earlier; new dependencies and environments can still reveal defects.
