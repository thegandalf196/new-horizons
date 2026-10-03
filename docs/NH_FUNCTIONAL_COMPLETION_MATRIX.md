# New Horizons functional completion matrix

Updated: 2026-10-03
Canonical source SHA-256: `ccaa84fcd322b1e011ab5cac8094195f198b499026f5f78005b1a1a4eab93d21`

This is the durable evidence register for UP-023. It tracks functional gameplay
completion separately from catalogue presence and artwork. An `active` data row,
a description, an icon, or a generic engine primitive is not by itself a
completed mechanic. The full evidence chain remains relevant for stabilization;
Phase 1 moves on after production implementation, registration, a working
principal path, the necessary interaction hook, a build, and focused
verification without an obvious crash or state-integrity defect. Unverified
cross-system interactions are deferred explicitly to Phase 2.

## Phase 1 specification-coverage snapshot

UP175 Night Prowler is source/native verified and active. Actually committed,
non-flying Ghost Walk through a currently hostile footprint grants10% melee/
ranged damage on the next attack in that activation. Existing UNTIL_ATTACK and
STACK_ACTIVATION Bonus lifetimes consume the pair after the full first hit set
or expire it unused. Named quantitative feedback, gated exact-route AI prediction,
branch-local consumption/replay and ID-only cache history are included. Core53687
and repaired both-target90975 build pass; initial client17500 failure and repairs
are retained. Principal3/3 passes (suite0.825s,total1.110s); production-active3/3
passes (suite0.833s,total1.110s), both zero skips. Data/inventory19/19, module drift
and activated incremental build pass. Final independent review finds no blocker.
Binary SHA-256:537d5121058c8e16be5a5c1eab738ca0a4d9177a63984128650e3e3e9ac1c785.
Coverage193/310 active perks,117 planned; faction55/90,35 planned; Shroud6/10;
ranks84/93 unchanged. Generic UI Provisional and purpose-made art Not done.
Broader hazard/control/form, full-save and rendered-delivery checks remain Phase2;
the flying negative is a shared-predicate check, not an executed flying move.
No immutable playable promotion is claimed. Deep Flank UP174 and Vanish UP176
await their narrow recorded design rulings, without implementation/count claims.

UP173 Shadow Assault is source/native verified and active. A target-carried,
attacking-side-specific battle-long Bonus gates the first qualifying flank's
25-percentage-point Creature-Defense ignore. Accepted hits consume it even on
zero damage or a dead target; prediction reads do not consume it. Live and AI
candidate/replay paths share classification, and ID-only cache history bypass
survives side changes. Client10661, combined99818 and activated both-target
build pass. Principal1/1 (0.358s) and production-active1/1 (0.353s) pass with zero
skips; the latter bypasses the planned-only fixture override. Data/inventory19/19
and module drift pass; independent final review finds no blocker. Binary SHA-256:
eb13b6ef7f9c69e416031ce74750cb7e7960eea303c1a551ade54c19dd84add1.
Coverage192/310 active perks,118 planned; faction54/90,36 planned; Shroud5/10;
ranks84/93 unchanged. Broader control/form/clone, whole-combat saves and rendered
delivery remain Phase2. Generic UI Provisional, purpose-made art Not done;
no immutable playable promotion is claimed.
Shadow Assault source is committed/pushed asbc979fdcd. Matching notice37105388534
is dispatched; the older full Windows37102336709 still excludes this source.
The next Deep Flank map flags a positional-contact versus accepted-hit-history
ambiguity, recorded asUP174 without an implementation or coverage claim.

UP172 Ambusher is source/native verified and active. A per-stack battle-long
spent Bonus marker drives the first qualifying flanking strike's20-percentage-
point damage premium. Shared live/detached damage reads do not consume it;
accepted primary-hit application does, before retaliation, even on zero damage
or a dead attacker. Current-controller/candidate/replay transitions and cache
history are included. Client40160 and combined9218 build successfully; principal
1/1 passes, zero skips (0.354s test), and production-active1/1 passes, zero skips
(0.346s test), without a planned-only override. Data/inventory19/19, module
drift and activated incremental build pass. Test binary SHA-256:
5c9706054ccbc4f59f040e7ed901498fa3878aa0d9a3e471fdd063b83aba36e9.
Coverage191/310 active perks,119 planned; faction53/90,37 planned; Shroud4/10;
ranks84/93 unchanged. Full combat-save restoration, broader control/ordering
and form/clone identity remain Phase2. Generic UI Provisional, purpose-made art
Not done; no rendered acceptance or immutable playable promotion is claimed.
Final independent activation review finds no blocker and confirms the recount.
Ambusher source is pushed asdb96e718b; no immutable playable promotion.

UP171 Evasive Shroud is source/native verified and active. Accepted direct
rear melee grants the surviving attacker nonstacking15% physical reduction
before retaliation, until its next real Creature Activation. Live, candidate
and selected AI replay use the same classification; direct qualifying Cleave
is eligible, secondary collateral is not. The cache retains affected-target
history so expiry cannot revive stale reduced damage. Both-target retry and
fixture rebuild pass. Repaired principal3/3 passes, zero skips; production-active
3/3 passes in5.730s, zero skips. Data/inventory19/19, module drift and activated
both-target build pass. Final independent activation review finds no blocking
issue. Binary SHA-256:
3d302ce731c2f278dfc30093d4a3e6767b95c3ebe7da0efde8810a7f00a3ab09.
Coverage190/310 active perks, faction52/90, Shroud3/10; ranks84/93 unchanged.
Broader Cleave/preemptive ordering, control transitions and full combat-save
restoration remain Phase2. Generic UI is Provisional; purpose-made art Not done.
No local immutable playable promotion or rendered acceptance is claimed.
Evasive source is pushed as04a6ecdd6 and matching notice37103081203 passes.
Full Windows37102336709 is live on preceding025ea810a; no Evasive package claim.

UP169 Rage Through Pain is source/native verified and active. A saved per-unit
increment is earned once at a strict below-half HP crossing; shared damage and
threshold consumers apply the normal cap. Accepted-hit feedback and detached
AI projection are included. Both-target build52291 exits0; principal4/4 passes
in1.186s with zero skips. Activated data/inventory19/19, module drift and
incremental build pass; final production-active4/4 passes in1.183s, zero skips.
Final review accepts with no blocker; test binary SHA-256 is
fc1b4c90470cb164641e06ffc4be91220bedfb03776d788b212fba43a956e4d9.
Coverage is189/310 active perks, faction51/90 and Bloodrage7/10. Broader historic
CUnitState binary restoration and rare control/form/revival interactions remain
Phase2. Purpose-made art is Not done; generic UI is Provisional.
Rage Through Pain source is pushed as025ea810a; matching notice37101230223
passes. Full Windows37098764803 succeeds on preceding No Escape9f59f66eb.
Full Windows37102336709 is confirmed running on025ea810a, excluding dirty
UP171. No local immutable playable snapshot was promoted.

UP170 No Escape runtime and detached AI are source/native verified and active.
Accepted rear melee applies a nonstacking -2 Speed bonus to its surviving direct
target until that target's next Creature Activation; qualifying retaliation uses
the same predicate. Visible current-controller heroes govern AI projection.
Both-target baseline build42759 exits0; independent runtime/AI review finds no
blocker. Initial principal2/4 result exposes fixture retaliation controls and
post-activation observation errors, not a production duration failure. Repair
retry3 build33849 exits0; principal4/4 passes in8.112s with zero skips. Production
activation, generated module, data/inventory19/19 and activated both-target build
pass. Final activated native gate passes4/4 in8.145s, zero skips; independent
activation review finds no blocker. Coverage188/310 perks,122 planned; faction
50/90, Shroud2/10; ranks84/93 unchanged. Full-game resume, resurrection, hidden-
enemy comparative valuation and guaranteed separate status labeling remain
Phase2. Purpose-made art is Not done; generic UI is Provisional. No playable
promotion is claimed. Native binary SHA-256:
a9cb4afb3fd1184af1e2825e6f55791a3d9b8a57a4353bcc5a0f002fa0fcb02e.
Windows37094808848 succeeds on the older Investor21dbb224b source, not this work.

Field Study source is committed/pushed as4c41c8aa0; notice37095711525 passes.
Full Windows37094808848 remains running on older Investor21dbb224b, not this
later source. Next-item maps do not increase coverage: Master Teacher awaits
whether selected Mentor is required; Archivist shares UP-054's Adventure-scroll
policy; Unyielding shares Deep Bulwark's absent nonmagical displacement producer.
Prospector awaits Gold-mine eligibility; Magnate awaits ownership/stacking rules.
Their event/history, packet, income and AI/UI seams are mapped, but no new
mechanic is implemented or counted. Reuse the queue evidence after answers
instead of repeating exploration or activating a narrowed subset.

UP161 Field Study is source/native verified and activated: generic battle-start raw Army
Value snapshots and wandering-army classification feed a winner-only conditional
25-percentage-point XP contribution. Existing Learning rank bonuses compose
once before the normal rounding; equal/weaker and unrelated guards do not
qualify. Unknown legacy snapshots never reconstruct strength from survivors.
Core29456 fails on unsupported uint64_t direct decoding; the bounded wire repair
uses optional low/high uint32_t words, retaining the complete runtime domain.
Core retry6495 and both client/test builds pass. The principal gate crashes after
two passing hero-victory cases: the neutral battle fixture reads battle state
after synchronous cleanup. Isolated debugger establishes the fixture lifetime
error; the fixture-only repair is independently reviewed. Both-target
rebuild31335 passes, isolated wandering retry1/1 and full principal11/11 pass
in4.059s with zero skips, including actual awarded XP and legal AI acquisition.
Registration/module/inventory activation follows acceptance; data19/19 and
module drift pass, activated both-target build exits0. Final activated native
gate passes11/11 in3.697s with zero skips and independent review finds no blocker.
Binary SHA-256:84b396ec5c21d9abc9ecb1a8209a4494aad37f92af1f2a6cb9f735500e996605.
Coverage187/310 perks,123 planned; Learning3/10. No playable
promotion or purpose-made icon is claimed.
Full-game resume, unusual result/ownership transitions and comparative AI XP
valuation will be tracked for Phase2 after principal acceptance.

UP160 Investor's server-authored weekly snapshot feeds normal daily receipts
and shared AI income:50 Gold/full5,000 pre-income treasury, capped250 daily
Gold. Midweek treasury changes do not recalculate it. First-week seeding,
new-week prospective receipts and pooled expiry are explicit. Combined client
and native-test retry2/retry3 and activated builds exit0. The principal14-case
gate passes13 with one standalone-copy fixture failure; its bounded correction
passes1/1 on retry,zero skips. The copied hero uses its own bonus-graph baseline,
not a claim of full-game restoration. Registry/module activation,19/19 data
and inventory checks, module drift and91/91 package checks pass. Independent
final review finds no blocker. Activated five-case gate passes5/5,zero skips
in68.870s. Native binary SHA-256:
`7a5a3e4681a9f0ab231f94a2f8a55e5d559a86aabee048b78fe52b64d7532d9f`.
Source is committed/pushed as21dbb224b; no playable promotion. Notice37093465685
passes and full Windows37094808848 remains running on that exact Investor source,
excluding dirty Field Study.
Coverage is186/310 active perks,124 planned,84/93 ranks; Estates5/10.
Full-game resume, unusual ownership/rehire transitions, comparative AI valuation
and rendered/playable delivery remain Phase2. The generic icon remains Not done.

Full Windows37087488369 succeeds on older0721ee12b (Tactics source). It does
not verify Defend's later repair or Redeployment. Notice37091288150 is queued
on213b4a35e, the Redeployment source plus delivery notes, before next full build.
Notice37091288150 succeeds; full Windows37091363403 is now queued on that
exact committed source. Dirty Investor implementation is excluded.

UP158 Redeployment's combined retry56408 builds both client and test targets.
Principal native gate passes15/15 in25.735s with zero skips: four Redeployment,
eight Tactics deployment and three Defend lifetime cases. Production registration
is activated after this gate. Coverage184->185/310 perks,126->125 planned;
Battlecraft4->5/10. Ranks84/93 and faction49/90 are unchanged. Final module/data
checks pass: data/inventory19/19, module drift and package91/91. Activated native
gate passes4/4 in1.564s, zero skips; the incremental both-target build exits0.
Independent activation review finds no blocker. Source is committed/pushed as
`27ae52f378984c8be70bf0dbf0b4ac483f92a0db`. Notice37090616619 is queued on
that source; full Windows37087488369 remains live on older0721ee12b. Rendered/actual
AI handoff, full midbattle resume
and rare trap/death interactions remain Phase2; no playable promotion.
Native binary SHA-256:
`97a5f613342455317aacbe319d6c912d37ce52581d61cc42c1c9531bb470fd86`.

UP158 Redeployment is source-staged, not active coverage. A separately versioned
generic final-relocation phase follows all initial deployment, reusing existing
legal movement and human/AI controls. One accepted changed-position move or END
pass completes it; rejected/no-op requests preserve the opportunity. Same-side
UI handoff and AI no-op/stale-END handling are repaired. Combined95627 stops on
a missing direct IBattleInfo include; its bounded repair is frozen for retry.
Four focused native cases are registered but not yet accepted. Data/inventory
19/19, module drift and package91/91 pass. Coverage remains184/310. Rendered and
actual AI handoff plus rare trap/death interactions remain Phase2; no promotion.

UP156's base Defend prerequisite is source/native verified: the current-round
action flag resets for queue eligibility, while the existing UNIT_DEFENDING
duration tag and captured stance persist until next activation. Shared damage,
AI clone projection and Second Wind's current-round eligibility are aligned.
Both-target retry27682 passes; focused native37434 passes16/16 in4.453s, zero
skips, including actual next-round queue selection. No new perk activation:
coverage remains184/310 perks and84/93 ranks, directly recounted from registry;
Command's stale summary row is corrected to7/10. Battlefield Mastery and
Pre-emptive Strike await recorded narrow design rulings. Broader expiry/save,
vanilla and visual interactions remain Phase2; no playable promotion.

Windows guard drift in full37086471771 is repaired and pushed as0721ee12b.
Local package gate91/91 and notice37087398608 pass; replacement full37087488369
is confirmed in_progress on0721ee12b, excluding uncommitted Defend changes.

UP-154 Tactics is now active and source/native verified. Both armies receive
independent base-plus-two-row deployment, sequential phase packets and strict
pre-action side/whole-footprint/occupancy validation. Opening effects and round1
begin only after the last entitled side. Existing human controls and AI handler
receive phase switches, with global ordinary-action blocking and branch-local
deployment projections. Core37002 and repaired both-target10000/39337 pass;
principal retry24146 passes8/8 in23.514s, adjacent11237 passes10/10 in17.054s,
zero skips. Data/inventory19/19, module drift and source review pass. Coverage
183->184/310 perks,127->126 planned; Battlecraft3->4/10. Ranks84/93 and
faction49/90 are unchanged. Preserve the interrupted build and7/8 fixture logs.
Descriptor/packet persistence is not full midbattle resume. Rendered/actualAI
handoff, broader siege layouts and malformed post-opening phases remain Phase2.
No playable snapshot is promoted. Binary SHA-256:
`ab5cb64908b58d203ed9233a88749960d01789655a68d34297c27ca7a560a6d1`.
Source is pushed as`a85f2e44e303effe166badb5765422f4eeb6585a`.
Notice37086103464 is queued on that source. Full Windows37082097577 is still
live on older7aaa48c1; Tactics Windows compile/package acceptance is pending.
CI update: older37082097577 succeeds on7aaa48c1; notice37086103464 succeeds
on Tacticsa85f2e44e. New full37086471771 is queued onb954d071f (delivery notes
only beyond the same Tactics source). Tactics Windows acceptance remains pending.

UP-154 Tactics is source-staged with independent range3 deployment for each
entitled army, saved phase progression, authoritative side/whole-footprint guards
and existing human/AI phase handoff. Core build37002 passes; baseline42019 was
interrupted with143 before completion. Root/reviewer repaired a constructor-order
UI crash before native use. Repaired combined build10000 is live with the eight
registered principal cases. Registry activation is staged for real acquisition
tests, not yet accepted coverage; verified counts remain183/310. Data/inventory
19/19 and module drift pass. Rendered/actualAI handoff, broader siege layouts and
post-opening malformed-phase hardening remain Phase2. UP-155 Overwatch has a
read-only map and awaits the Teleport/Blink scope clarification.

UP-153 Passing Lines is active and source/native verified. Shared movement permits
friendly occupied transit but rejects occupied endpoints, including controlled
double-wide footprints. The authoritative path preserves occupied Fire Wall
damage and stops before hidden Quicksand; detached AI consumes the same query.
Both Linux targets build; principal retry13004 passes6/6 in4.330s and adjacent
87077 passes10/10 in2.280s, zero skips. Data/inventory19/19, module drift and
independent review pass. Coverage182->183/310 perks,128->127 planned;
Battlecraft3/10, ranks84/93 and faction49/90 unchanged. Phase2 retains compound
hazards, consecutive friendly footprints, siege gate/moat interactions and full
tactical AI certification. No playable snapshot promotion or rendered acceptance.
Iron Will, Crisis Command and Seize Initiative retain their narrow unanswered
design/conflict questions. Windows37077420212 succeeds on older37dd359b8,
including Battle Plan but not Passing Lines.
Passing Lines source7aaa48c1be92db6056973dfd4a3e6ad9164302c5 is pushed.
Notice37081984549 succeeds; full Windows37082097577 is queued on that source.
No compile/package success is claimed before that job reaches terminal success.

UP-148 Battle Plan is implemented and active after both-target builds and
principal native5/5 in13.180s, zero skips. Its saved pre-combat opportunity and
dedicated BATTLE_PLAN receipt run after tactics but before any Creature Activation,
preserving HERO, allowing both sides to choose and never triggering Double Command.
The existing painted Orders chooser is reused; actual bookless AI submission
and authoritative acceptance pass. Production-registry activated acceptance
passes30/30 in45.567s, zero skips; fixtures no longer force activation.
Data/client34/34, module drift
and independent source review pass. Coverage181->182/310 perks,129->128 planned;
Command6->7/10, ranks84/93 and faction perks49/90 unchanged. Rendered chooser QA,
broader opening interactions and exhausted-ID preflight hardening remain Phase2.
No full midbattle save/resume or immutable playable promotion is claimed.
Source is committed/pushed as52c4f89a636923260ea0ac8d4e1a2c79aab76bda.
Windows notice37076319236 is running on that commit; earlier full Windows
37071436091 remains running on51340a3d4, not this feature.
That older full run subsequently succeeds. Battle Plan notice37076319236 also
succeeds; full Windows37077420212 is now in_progress on37dd359b8, containing
the same Battle Plan source. This is not yet compile/package acceptance.

UP-147 Double Command is implemented and active. The first accepted HERO-paid
Order once per combat grants an immediate different Order, with a dedicated
typed receipt, saved continuation metadata, authoritative no-choice exhaustion,
and deferred first-Order Second Wind activation. The existing Orders chooser
opens automatically; target cancellation returns to it without declining or
spending the opportunity. Actual AI submission and detached projections use
the same mandatory choice and do not enter ordinary-action forecasts.
Both Linux targets build. Principal retry 30525 passes 6/6 in 99.078s; activated
focused 99320 passes 47/47 in 134.777s, zero skips, including Commanding Presence,
simultaneous Orders, packet persistence and action/spell allowances. Data/client
30/30, generated-module drift and independent source review pass. Counts advance
180 -> 181/310 active perks and 130 -> 129 planned; Command is 6/10. Ranks remain
84/93 and faction perks 49/90. Descriptor roundtrip evidence does not certify
full midbattle save/resume. Rendered chooser QA, broad interaction matrices and
generic hypothetical packet replay remain Phase 2. UI is Provisional, bespoke
art Not done; no immutable playable promotion. Next missing item: Battle Plan.
Source is pushed as 51340a3d48607a096acd1dcf2975bafdcfc03ef7. Notice
37071300435 succeeded; full Windows build 37071436091 is in progress on that
source. This is source/native delivery, not a Windows graphical acceptance or
an updated local launcher snapshot.

UP-146 simultaneous different Orders foundation is implemented/native-verified.
One authoritative per-side collection preserves independent targets, consumption
and shared live/AI effects; typed consumers replace latest-only gameplay reads.
Required existing UI hooks enumerate the collection and observe the action ledger.
Append-only saves retain it, migrate older singleton records, and reject lossy
downgrades before bytes. Packet mutations preserve issuance and forward progress.
Both Linux targets build; principal retry75974 passes20/20 in31.867s, zero skips.
The bounded adjacent Order/Iron Discipline/persistence suites pass37/37; eight
Vengeance cases fail at an unchanged earlier-perk setup and remain explicit Phase2
fixture work. Review reports no remaining blocking issue. No action allowance is
granted by storage and Double Command remains planned pending its actual trigger.
Perk/rank counts remain180/310 and84/93; no playable delivery is inferred.
Source commitf57f58a84c8d82d49c9b5f555b12eb08f62e4585 is pushed to
origin/definitive-mvp. Notice preflight37060224004 is live; no new Windows
package or launcher snapshot is claimed yet. The notice preflight subsequently
succeeds; full Windows37060422101 is live on doc-only checkpoint7a90085cb,
which includes the same gameplay implementation. Poll that exact run to terminal.
Full run 37060422101 subsequently succeeded at 2026-10-02T21:29:11Z, including
compilation and packaging on 7a90085cb0d96a9b379bf4afc83a2fe66354bf41.
It does not include UP-147, which remains uncommitted and unactivated here.
Phase2 deferrals for this slice: uncapped compound-reduction numeric fixtures,
broader Order/perk/control/save matrices, and pre-existing hidden-enemy-hero
coefficient behavior. Multiplication is source-reviewed; the bounded capped
native fixture alone cannot distinguish additive from multiplicative reductions
once either reaches the cap. Existing UI source guard passes7/7; rendered
multi-indicator layout remains unverified and must not be described as final art.

UP-145 Blood Scent is implemented and active after principal retry91133 passes
5/5 in1.713s, zero skips. The shared physical attack payload treats Bloodrage as
one rank-sized increment higher against an enemy strictly below half maximum
HP, bounded by the saved cap. Base counters and threshold benefits remain
unchanged. Current control and resolved per-side snapshots preserve enemy-hero
privacy in detached AI branches. Current saves retain the value; older lossy
writes reject before bytes and older loads default0. Both Linux targets build;
data36/36 and module drift pass. Coverage179->180/310 active,131->130 planned;
faction48->49/90, Bloodrage5->6/10, ranks84/93 unchanged. Activated focused62923
passes36/36 in8.312s, zero skips. Broader control/status/save interactions are Phase2; no rendered
acceptance or immutable playable promotion. Artwork Not done, generic UI Provisional.

UP-143 Unrelenting and Berserker are active and native-verified. Speed and
retaliation bonuses follow half the current saved cap and current controller;
explicit Initiative remains independent, and retaliation usage is not refunded.
Authoritative per-side resolved bonuses make enemy-side AI prediction independent
of hidden hero inventories. Current binary saves preserve them; older writes
reject their loss and older loads default them to0. Both Linux targets build.
Principal retry7/7 and activated focused31/31 pass with zero skips; data36/36,
module drift and the existing UI source guard pass. Coverage177->179/310 active,
planned133->131; faction46->48/90; Bloodrage3->5/10; ranks84/93 unchanged.
First Blood, Blood Scent, Rage Through Pain, Slayer and Avatar of Rage remain
missing. Broader control/status/save interactions and pre-existing hidden-enemy
hero gaps are Phase2 findings. Purpose-made art remains Not done, generic UI
Provisional. No rendered or immutable playable promotion is inferred.

UP-143/144 Fury Unbound and Endless Bloodshed are implemented and active after
principal66256 passes4/4, zero skips in1.559s. Legal War Drums->Fury->Endless
offers open ordinary Advanced/Expert progression. Fury floors negative Morale
while the current controller's side has an active Bloodrage increment, preserving
positive Morale and NO_MORALE. Endless changes the Expert cap60->80 without
changing12-point increments. One saved cap drives live kills, detached AI and
the existing resource panel; legacy saves restore rank-only caps, while older
writes reject nonbase caps before bytes are written. Live and isolated AI deaths
cross72->80. Both Linux targets build, data36/36/module drift and the UI source
guard pass; activated adjacent retry passes41/41 with zero skips. Coverage175->177/310 active,
planned135->133; faction perks44->46/90; Bloodrage1->3/10; ranks84/93 unchanged.
Unrelenting/Berserker, First Blood, Blood Scent, Rage Through Pain, Slayer and
Avatar of Rage remain missing. Phase2 retains broader save/control/death and
Morale-floor interactions; purpose-made art Not done, generic UI Provisional.
No rendered or immutable playable promotion is inferred.

UP-142 Commanding Presence: the accepted spent/broken recipient-lifetime rule
is integrated into the canonical Command perk row and registry help text.
Shared live/detached recipient predicate and Morale floor are implemented.
Both Linux targets build; principal retry18653 passes9/9, zero skips in21.568s,
covering all eight Orders, spent/broken benefits, current control and detached
branch isolation. Registration is active:175/310 perks,135 planned; Command5/10.
Ranks remain84/93. Data36/36/module drift pass; activated adjacent gate39487
passes23/23 in25.382s, zero skips, with no blocking final review finding.
No new state, polling or Hero Action; existing Order state supplies lifetime.
The missing multiple-Order foundation remains independent Phase1 work, not
silently counted as implemented by this perk. Phase2 retains broader Protect
control changes and Flank/Formation Fighting interactions. Generic UI is
Provisional and purpose-made art Not done; no immutable playable promotion.

UP-140 Steadfast principal native verification passes14/14, zero skips in3.155s.
Its prerequisite exposes cached limiter-applied bonuses before same-key
stacking, so reducing hostile penalties does not discard stronger friendly
penalties. Application-time enemy provenance is binary/JSON versioned;
propagated creature auras retain dynamic ownership. Runtime, AI and focused
fixture lanes are independent. Both Linux targets rebuild76129 successfully;
friendly/enemy added native auras now have correct stack-owner provenance.
Activation advances173->174/310,137->136 planned; Discipline6->7/10. Ranks
remain84/93. Activated data36/36, module drift, both-target build and final
native23/23 in5.834s pass; zero skips and no blocking review finding. Adjacent9/10
pass; the full-battle No Quarter roundtrip rejects existing Veteran damage
history because CStack's binary payload omits CUnitState. Retain that Phase2
finding, not a claim of broad save acceptance. No graphical/playable delivery.

UP-139 structural foundation verified: both-target4772 builds; principal35313
passes10/10 and adjacent27391 passes6/6, zero skips. Focused data36/36 and
module drift pass; independent review has no blocker. Ordinary-scenery and
fortification execution clauses now have source/native evidence for Meteor
Shower and Armageddon. This adds behavior coverage, not new spell identities
or active perks:173/310 remains unchanged. Absolute-landmark classification
and structural-perk stacking remain Phase1 design questions. Phase2 retains
normal-profile AI tactical choices, obstacle-removal utility, localization and
broader interaction coverage. No graphical/playable delivery is claimed.

UP-139 structural foundation is in progress: data-driven saved-v3 opt-in adds
Meteor Shower's impacted ordinary scenery/fortification path and Armageddon's
battlefield-wide equivalent. Prototype fort output is50%/100% of raw
coefficient-aware spell damage; old snapshots omit the opt-in and keep prior
behavior. Runtime, bounded geometry-aware AI and focused fixtures have separate
owners. No activation or coverage increase from intermediate source/data.
Fixed absolute landmarks and combined structural-perk stacking await the user;
do not treat an engine placement category as proof of destructibility.

UP-135 Bastion verified checkpoint: client and test targets build; final
fixture build92361 exits0 and native45892 passes8/8 in2.618s, zero skips.
Legal perk offers, actual Defend/Hold the Line reduction, first-hit expenditure,
real next-round renewal, spell-like exclusion, detached branch/live isolation
and compatible JSON/copy defaults pass. Registration changes172->173/310,
138->137 planned; Armorer6->7/10. Activated gates remain next. Generic UI
Provisional, purpose-made artwork Not done; no GUI/playable promotion.
Phase2 retains absorbed/lethal/current-control/Immovable combinations, populated
AI cache assertions and Hold the Line Defend-heuristic baseline valuation.
This checkpoint supersedes earlier in-progress counts below.
Activated both-target build28218 exits0. Combined native67346 passes18/18
in5.197s, zero skips (Bastion8 plus Mine Layer10). Data/schema/inventory35/35
and generated-module drift pass; independent frozen reviews have no blocking
findings. Frozen pre-commit test SHA-256:
`794af3ccffdbbde1d3b4a4829561589600f81811c5cc955197bfacb66a5860c4`.

UP-139 structural audit: canonical Meteor Shower/Armageddon currently damage
units but lack their scenery/fortification clauses. Demolitionist has no eligible
Havoc structural producer yet; Earthquake is Nature and must not be substituted.
These partial spell effects are foundational Phase1 work, not balance-only
polish. Perk registration remains planned until actual structural paths exist.

UP-137 Mine Layer principal51245 passes10/10 in2.899s, zero skips after
client27766/test22177 successful builds. The shared coefficient-aware count
adds one after the base cap (3/4/5); UI, obstacle creation, AI and both server
checks consume it. Legal Advanced acquisition, detached AI and accepted/rejected
placement paths are verified. Registration changes171->172/310 active,
139->138 planned; Havoc6->7/10. Activated data/native gates remain pending.
Activated retry70019 passes10/10 in2.919s, zero skips. Client activation build
exits0; data/schema/inventory35/35, module drift and independent activation
review pass. This checkpoint supersedes the older counts below.
No new state/polling, artwork approval or playable promotion is claimed.
Phase2 retains broader GUI/placement and explicit unselected legacy guards.
Windows37008135705 completed successfully on8c5f5ec87, including Broad Muster
and Unbreakable, but not the newer Precise Casting/Bastion/Mine Layer source.

UP-135 Bastion is being implemented with a distinct per-stack round marker,
shared current-controller Defend/Hold the Line eligibility, final physical
damage factor and detached AI spending. It remains planned until build and
principal verification; counts remain171/310 active and139 planned. No polling,
playable delivery or new artwork approval is claimed from implementation work.
UP-135 architecture review found an inherited Phase2 integration concern:
Immovable's accepted-hit consumption uses !bat.spellLike while its damage gate
also excludes ranged SPELL_LIKE_ATTACK. Bastion must use matching actual physical
provenance in reduction, consumption, logging and AI instead of copying that
mismatch. This finding is recorded, not used to broaden the Bastion slice.
Frozen runtime/AI reviews find no blocker. Client92791 exits1 on a new
Counterfire state-interface read; corrected acquireState access is reviewed.
Both-target retry79589 exits0. Principal64640 runs8 cases in2.607s, zero skips:
four pass and four fixture setups fail. Repeated/spent action lifecycle and a
player0 AI view hiding the defender hero are being repaired without weakening
production validation or information visibility. Activation remains planned.
Phase2 retains absorbed/lethal/current-control/Immovable assertions and the
Defend heuristic's repeated baseline discount when Hold the Line already grants
Bastion. This heuristic limitation does not alter authoritative combat.

UP-133 Precise Casting's bounded three-damage-area path is in source and verified:
client48085/test18427 and new fixture90773 build; principal96284 passes7/7
in2.238s, zero skips. Legal Basic/Advanced offers, accepted live/detached Fireball,
unselected/planned gates, current-control double-wide Meteor Shower, Armageddon
and BattleAI viable central targets pass. Shared Controlled Blast regression89309
also passes6/6. This does not prove the broader effect scope: Time Stop/Earthquake
await the user. Keep registration planned,171/310 active and139 planned.
Independent fixture review has no blocker. Phase2 retains Precise Casting-specific
Inferno/hover assertions and actual AI utility ranking; shared regression and
candidate generation are not claims of full chooser or visual acceptance.
Windows full37000555568 passes compile/package/upload on7331e1056, an older
source containing External Recruiter and Spellward. Newer Broad Muster and
Unbreakable Windows compile/package evidence remains pending.

UP-131 verified checkpoint: Unbreakable is active. Client52523 and test10838
build; fixture repair52313 exits0. Principal retry10314 passes10/10 in3.330s;
activated both-target98579 and native69637 pass10/10 in3.303s, zero skips.
Legal Expert acquisition, actual per-round renewal, separate Rally expenditure,
current-controller ownership, positive/immunity and unselected gates, old/current
wire, packet and detached branch/reset cases pass. Data/schema/inventory35/35,
module drift and independent frozen review pass. Coverage170->171/310 active,
140->139 planned; Discipline5->6/10. Ranks84/93 and combat identities60/67 plus
five Mass variants remain unchanged. Phase2 retains multi-round AI valuation
and broader stochastic reroll interactions; purpose-made art Not done, generic
UI Provisional. No GUI or playable promotion. Frozen pre-commit test SHA:
`b4928cbfefc56a763e20109c35ed89ea1bca84d043065e31355d0e6111695a3c`.

UP-131 in-source checkpoint, 2026-10-02: Unbreakable extends the shared Morale
suppression state with an independent round allowance, captured from the saved
Expert perk. Live and detached round events renew that allowance without
resetting Rally. The authoritative first negative draw spends one allowance,
round-first, before the existing Twist path; named feedback distinguishes the
perks. Packet validation accepts one exact legal expenditure or an idempotent
spent snapshot, not an enable/reset/double spend. Added state is append-only,
old-format loads default it off and lossy downgrade writes are rejected.
Source review has no provisional blocker; build and principal native evidence
remain pending. Registration stays planned. Counts remain170/310 active and
140 planned; no GUI, artwork approval or playable promotion is inferred.

UP-128 Broad Muster is active and native verified. One optional split request
allocates the generated Core total between two distinct town rows, validating
positive amounts, eligibility and both overflow bounds before one shared weekly
marker and combined stock update. Solo/external paths remain unchanged. The
native scrollable picker shows exact destinations/counts; AI sends one split
when Leadership-admitted value beats every solo option. Enumeration is bounded
to current town rows, not a world scan. An append-only request wire feature
preserves old solo defaults and rejects lossy old-format split writes; no new
saved gameplay counter or polling is introduced. Both Linux targets build40839;
principal54272 passes18/18 in3.701s and activated82143 passes18/18 in3.704s,
zero skips. Legal Basic/Advanced/Expert splits, malformed/same-row/category/
context rejection, both overflow guards, shared Master Recruiter uses, saved
markers, old wire and AI helpers pass. Data/schema/inventory35/35, module drift
and static UI guards pass. Independent runtime/wire/fixture and AI/UI reviews
have no blocker. Coverage169->170/310 active,141->140 planned; Recruitment6/10.
Ranks84/93 and combat identities60/67 plus five Mass variants are unchanged.
Phase2 retains full UI/AI query execution, long-label native fit, existing-stock/
Gold/free-slot AI valuation and an isolated external-split guard. Bespoke icon
Not done; generic UI Provisional; no GUI or playable promotion. Frozen test SHA:
`e46a98b5b49da243fdabf34a2ae20f639ee433bab48fedcc751919470d94c600`.

UP-124 External Recruiter is active and native verified. Owned external saved-Core
dwelling Muster adds exactly two recruits at every Recruitment rank, shares the
hero's weekly town allowance and target lock, and requires the exact active visit.
Native recruitment controls expose the action; AI issues it before purchase and
waits for authoritative realization. Shared cost handling preserves free original
tier-one external recruits without bypassing Leadership. Existing markers/packets
suffice; no saved fields or polling are added. Both Linux targets build; repaired
principal15376 passes11/11 in2.877s and activated81048 passes11/11 in2.899s,
zero skips. Legal progression, invalid contexts/perks/categories/requesters,
shared Master Recruiter allowance, zero-resource free purchase, Leadership,
overflow and saved markers are covered. Data/schema/inventory35/35, module drift
and Muster UI wiring pass; independent Astra source/fixture reviews have no
blocking finding. Coverage168->169/310 active,142->141 planned; Recruitment5/10.
Ranks84/93 and combat identities60/67 plus five Mass variants are unchanged.
Phase2 retains full natural visit/window/AI execution, full-army merge behavior,
mixed-row dwellings and restored in-progress queries. Bespoke icon Not done,
generic UI Provisional; no GUI or playable promotion. Frozen candidate test SHA:
`8862d77812f157de4fe9c8b243d658521d6c64724b5f086999055bdee24a0223`.

UP-120 Spellward is active and native verified. Current-controller protection
supplies an independent10% magical reduction before the shared95% cap and
penetration, without adding resistance or reducing physical/nondamaging effects.
Paid damage,50% independent combination, cap, rank loss/reacquisition, inactive
offers and detached forecast/castEval pass. A computed-defense proxy override
preserves hidden hero information and projected-only Hypnotize ownership.
Repaired build80192 exits0 and principal24023 passes21/21 in5.858s; activated
build12348 exits0 and native60120 passes22/22 in6.000s, zero skips.
Data/schema/inventory35/35 and module drift check pass; independent Astra review
has no blocker. Coverage167->168/310 active,143->142 planned; Warcasting5/10.
Ranks remain84/93 and combat identities60/67 plus five Mass variants. No new
saved state or polling. Wider magical-ability/save interactions remain Phase2.
Neutral fallback art is Not done, generic UI Provisional; no GUI/promotion.
Native binary SHA-256:
`ef7f9e187c5221e80b99a73a6cdd20e713b5434ab64910f9c3c5c4c4a24e7c32`.
Combat Casting, Mire Shaper and Enchanted Command are separately design-blocked.

UP-118 consumer checkpoint, 2026-10-02: Earthquake's selected-area siege and
radius2 field modes are native verified; Advanced Geomancer is active. Field
damage uses30+0.8×scaled raw Spell Power, both grounded sides, immunity-aware
targets and three-round Fractured Ground. Geomancer adds one terrain round and
25% structural damage. Structural HP is an explicit Phase1 tunable100/125 per
section because the canonical text supplies no absolute amount. Weighted
movement budgets are distinct from physical Charge travel in server and AI;
return movement checks affordability. Legacy profiles retain Catapult behavior.
Both targets build; final80978 exits0. Native91000 passes63/63 in8.411s, zero
skips, including actual AI-selected paid siege, detached forecasts, immune
field targets, saved profiles and terrain guards. Data/schema/inventory35/35
and module check pass; independent Astra review has no blocker. Coverage
166->167/310 active,144->143 planned; Nature7/10. Combat identities remain60/67
plus five Mass variants, not an identity-count increase. Phase2 retains wider
save/movement interactions, special Metamagic-event projection modifiers and
optional scenery destruction. Borrowed Quicksand terrain art is Not done;
generic UI Provisional. No GUI or playable promotion. Next unblocked candidate:
Advanced Nature Mire Shaper's additional Quicksand patch.
Native binary SHA-256:
`fd3227d571340042be8ee857c94891c7f97942ea775f783f2dea3ad25a0e9191`.

UP-118 foundation checkpoint, 2026-10-02: spell-created movement-cost terrain
has native evidence for exact weighted costs, cheaper detours, new double-wide
footprint cells, flying exclusion, overlap maximum, expiry, packet/JSON state
and append-only binary compatibility. Client54585, test92767 and focused82811
exit0; native72881 passes12/12 in3.287s, zero skips, including five existing
detached-obstacle guards. Service rejected an additional reviewer; root reviewed
the bounded diff directly. This prerequisite is not a completed Earthquake or
Geomancer. Counts remain166/310 perks and60/67 canonical spell identities plus
five Mass variants. Before activating consumers, implement field creation and
feedback, selected-area structural damage and separate movement cost from actual
travel for Charge/Pursuit. No GUI or playable promotion. Broader terrain/order/
save interactions remain unverified; no Phase1 completion credit is inferred.
Native binary SHA-256:
`044cc3be01db86403e48f6968201d8886cc19a7f70ee44a41e2310e6dfe11fd0`.

UP-115/116 Sanctuary Keeper and Venomancer are active and native verified.
Keeper adds exact-source marker-limited+2 Morale, refreshes without stacking,
cleans non-perk recasts and removes siblings when Sanctuary breaks. Poison
stores Venomancer's20% whole-Base boost once, with exact92/138/184 ticks at
Basic SP100. Toxic Spines remains independent. Legal Basic acquisition,
inactive/legacy guards, materialized detached/live parity and both actual
AI-selected paid server casts pass. Client90317 and final both-target10531
exit0; principal20512 passes25/25 in6.559s, activated76413 passes30/30 in7.833s,
zero skips. Data/schema/inventory34/34 and module check pass. Independent
review has no remaining blocker. Coverage164->166/310, planned146->144;
Light8->9/10, Nature5->6/10. Spell identity coverage remains60/67 plus five
distinct Mass variants. No new saved state or polling. Phase2 retains broader
Morale/specialty/save/modifier interactions and dedicated negative-Morale-only
AI selection. Generic UI Provisional, bespoke art Not done; no GUI/promotion.
Binary SHA-256: `5af0f580afc5743f828334ddf00d50e8cd25c924ed0bbf71c1b9e63b990a5449`.
Nature's Wrath map is retained in UP-117 awaiting range/conduction/resistance
answers. Next unblocked missing-content audit: Earthquake/Fractured Ground
and Advanced Nature Geomancer.

UP-114's final distinct entry, Mass Slow, is active and native verified.
Temporal Field grants the virtual entry without durable learning or a legacy
once-per-combat limit. Saved-v3 Slow-family resolution covers60% post-cap/
specialty magnitude, unchanged duration, triple listed cost before Wisdom and
single-status refresh in both directions. Book/rank revocation, acquisition
exclusion, immunity/Spell Lock, repeated casts and detached/live state pass.
Client33547 and final both-target53442 exit0. Principal7002 passes22/22 in5.821s;
activated47843 passes36/36 in9.512s, zero skips, including actual AI selection/
accepted cast, legacy budget guards, Communion and Heavenly Gale. Data/schema/
inventory34/34 and module check pass. Independent review has no blocker.
Distinct variants4->5/5; active perks remain164/310 (Temporal Field already
active); canonical combat identities remain60/67. No new persisted state or
polling. Phase2 retains broader save/modifier and rendered interactions.
No GUI or playable promotion. Next: Basic Light Sanctuary Keeper.
Verified native binary SHA-256:
`f1560bbd876638dbcaa94853ccdde671bfa558caf9d97955ceef90620a1eb765`.

UP-114 Mass Regeneration/Verdant Communion is active and native verified.
Virtual physical-book sources, living ally exclusions, triple costs, School
rate snapshots, future-wound survivor healing, family refresh and already-mutated
detached state pass. The real AI selects the variant and its submitted action
is accepted after no-target candidates receive the protocol sentinel. Final
both-target55569 exits0; principal79005 passes26/26 in7.453s and activated81701
passes30/30 in8.697s, zero skips. Data/schema/inventory33/33 and module check
pass; independent review has no blocker. Binary SHA-256:
`34be6e07935a7f4a1d617004d906aba0a5dcbc07e358cea2b14fc662f86c2ba5`.
Coverage163->164/310, planned147->146, Nature5/5, distinct variants3->4/5.
Phase2 retains broad save/modifier interactions and the synthetic eligibility
roster's round-advance diagnostic; bounded minimal refresh passes. No GUI or
playable promotion. Next: distinct Mass Slow/Temporal Field.

UP-114 Mass Bless/Litany is active and native verified. Advanced Litany grants
a distinct perk-only virtual spell while physical-book removal and rank loss
revoke it. Saved-v3 family handling preserves Bless's capped School-scaled
duration and Benediction; ordinary Expert Bless stays single-target. Explicit
family refresh fixes the materialized detached duplicate found by the principal
fixture. Uncapped duration, natural maximum-damage endpoint, ally eligibility,
both spell immunities, Curse removal, prior Bless replacement, real paid Mana,
three-times listed costs before Wisdom and ordinary-acquisition exclusion pass.
Client23367 and both-target20799/28088 exit0. Repaired31806 passes55/55 in10.977s;
activated5126 passes55/55 in11.867s, zero skips. Data/schema/inventory32/32 and
module check pass. Binary SHA-256:
`8f92162d77e59a5e67d9971419e8e04a9d8858a4c0a633111be0ce08ffde6088`.
Coverage162->163/310, planned148->147, Light8/2, distinct variants2->3/5.
Independent final review has no material blocker. The earlier stale Bless text
assertion is repaired and included in the passing gate. Phase2 retains broad
save/load, AI selection and modifier interactions; no GUI/playable promotion.
Mass Regeneration/Verdant Communion is next, then distinct Mass Slow.

UP-114 first slice is active and native verified: two distinct Mass Curse/Sorrow
entries plus Expert Grand Malediction, with saved-profile perk-only virtual
grants and base-family status refresh. Physical-book removal and rank loss revoke
grants; ordinary/manual/real-scroll sources cannot supply them. Both base and
variant immunity apply. Curse/Sorrow effect application uses battle::Unit so
previously-mutated detached targets are not skipped. Source review restrictions
and principal fixtures cover100%-strength metadata,3x listed costs before
Wisdom, eligible mass targeting, Expert base single-target scope, Malediction,
stored family replacement, old roster absence and detached/live parity.
Final both-target37060 exits0; principal99435 passes48/48 in7.091s; activated
21991 passes48/48 in7.326s, zero skips. Data/inventory/schema32/32 and module
drift check pass. Binary SHA-256:
`8484a7dff572753cbc95c4091dd3a812bfb1e0629f1da3aaaddc4ab300f5ca5d`.
Coverage161->162/310; planned149->148; Shadow9/1; distinct variants0->2/5,
additional to the67 school-roster identities. Independent final review has no
material blocker. Phase2 retains wider save/map-ban/counter/dispel interactions
and one stale Bless tooltip-text assertion, excluded explicitly from the final
filter after its gameplay assertions passed. No new persisted counters, polling,
GUI execution or playable promotion. Generic perk UI and reused base-spell art
are provisional; bespoke Grand Malediction art is Not done. Mass Bless/Litany
is next, then Mass Regeneration and the distinct60%-strength Mass Slow.

UP-112 Blood Drinker and Painweaver are active and focused native verified.
Blood Drinker heals75% of actual clipped Life Drain damage, rather than60%,
using ordinary survivor-only healing. Painweaver adds20% to Hex's cast-time
Spell Power term only; fixed15 and the10% actual-attack share are unchanged.
The existing bonus snapshot serves authority, replication and detached AI;
there is no new state or polling. Final both-target68123 exits0; final
principal6616 passes23/23 in6.246s and activated33834 passes29/29 in7.838s,
zero skips. Legal Basic offers, exact damage/healing, overkill, reduction,
no resurrection, zero-SP and v2 guards, stored-once injury and detached/live
parity pass. Data/inventory19/19 and module drift check pass. Binary SHA-256:
`43ee01b00ff7225a21c263560f215cb457a835fc11dd765a1e315f0b4fb22db2`.
Coverage159->161/310; planned151->149; Shadow8active/2planned. Independent
review has no remaining blocker. Phase2 retains strategic AI nonselection
observed in the first fixtures, reflected Life Drain paired-target semantics,
recipient-healing preview and wider modifier/save interactions. Generic UI
provisional, authored art Not done; no GUI or playable promotion. UP-113's
normal propagation limit awaits clarification. UP-114 maps the missing distinct
Mass-entry foundation, including family-aware effects and saved-profile grants.

UP-109 Controlled Blast and UP-110 Pyromancer/Cryomancer are active and focused
native verified. The shared Lua range-target filter excludes only the friendly
original center stack for Fireball, Inferno and Meteor Shower, using current
control and either double-wide footprint. Actual casts, detached forecasts and
hover predictions agree; the empty-target fallback remains Cure-only. Shared
damage coefficients add15% for Pyromancer and20% for Cryomancer only to the
Spell Power term, preserving flat bases and other spell effects. Fire Wall
stores the boosted cast-time damage and does not apply it again at contact.
Client38230, repaired both-target48589 and v3-fixture10278 exit0. Principal5598
passes20/20 in5.977s; activated19887 passes28/28 in8.153s, zero skips. Legal
offers, Advanced query/save-load, exact damage, inactive guards, current control,
double-wide protection, adjacent collateral, hover and accepted AI parity pass.
Data/inventory19/19 and module drift check pass. Binary SHA-256:
`ae8f67495a2784184afbeea8091117051dceda435a0cfd16bc1203c777e198b4`.
Coverage156->159/310; planned154->151; Havoc6active/4planned. Independent source
and fixture reviews have no remaining blocker. Phase2 retains broad defense/
coefficient interactions, proxy/reflection cases and the older hybrid v2 fixture
audit. Art Not done and generic UI provisional; no GUI/playable promotion.
UP-111 Cataclysm is mapped but not implemented: baseline Armageddon also lacks
specified physical-obstacle cleanup, and the ordinary magical-obstacle filter
awaits a recorded item-level clarification. Fortification damage is separate.

UP-107 Land Surveyor is active and focused native verified. First successful
mine capture per hero per absolute week grants three times the mine's normal
dailyIncome after ownership changes; ordinary bonuses and handicap follow that
shared path. A versioned hero marker replicates through existing object-property
packets before resource grants and never polls or invalidates Mana capacity.
Ordinary and abandoned captures share the hook, including guarded victories.
Client11819, test baseline13332 and both-target30265 exit0. Principal87442
passes3/3 in1.709s; activated61224 passes18/18 in7.206s, zero skips, including
legal Basic offers, accepted movement/capture, actual resource/callback receipt,
same/next week, save-load and serialized property replay. Data/inventory19/19
and module drift check pass. Binary SHA-256:
`f2e7c7848d1d161cc7d841049d6c2834cf2997be2cb7e5756967e904b9167c0f`.
Coverage155→156/310; planned155→154; Estates4active/6planned. Independent
production/fixture reviews have no blocker. Phase2 retains independent holders,
guarded/abandoned capture execution, old-format save/wire gates and wider client
fan-out/AI strategy. English capture feedback needs later localization; authored
art Not done and generic UI provisional. No GUI or playable promotion. UP-108
Divine Mandate maps are complete, pending explicit expiry/use-accounting choices.

UP-106 Financier is active and focused native verified. The existing weekly
income event grants floor(max(0, pre-turn Gold)/100), capped1000 per selected
active Expert holder, from the same treasury snapshot for every holder. Day0→1
is a week start; ordinary days have no interest. No new saved counter or polling
is introduced. Both-target21766 exits0 after the recorded fixture type repair;
principal39424 passes3/3 in58.108s and activated95915 passes15/15 in61.975s,
zero skips (`UP106-principal` and `UP106-activated` log/XML pairs). Legal offers,
accepted selection, exact packet/treasury receipts, flooring/cap/noncompounding,
ordinary daily income and actual callback resource receipt are covered.
Data/inventory19/19 and module drift check pass. Coverage154→155/310;
planned156→155; Estates3active/7planned. Binary SHA-256:
`072c9e6ca11a5f0b5c956a0b00fe240039a77c8e44aeba92dc014c51399b47b9`.
Independent production/fixture reviews have no blocker. Phase2 retains selected
holder rank-loss/planned-snapshot runtime suppression, broad calendar/save/client
fan-out interactions and strategic AI weekly forecasting. Generic offer UI is
provisional; authored art Not done; no GUI or playable promotion.

Current verified checkpoint: UP-102 Roadmaster and Wayfarer are active. The
shared rational movement formula applies the terrain cap after Pathfinding and
the extra road reduction before one final ceiling. TurnInfo caches saved active
perk flags once; authority, player forecasts and Nullkiller routes use the same
helper. Both-target20420 exits0; activated28363 passes25/25, zero skips in4.827s
(`UP102-activated.log`/`.xml`), including legal perk offers, accepted movement,
planned-snapshot/blocked guards and actual AI routes/cache refresh. Data/inventory
19/19 and module drift check pass. Coverage152→154/310; planned158→156;
Logistics5active/5planned. Binary SHA-256:
`72dcd031ee08bc4342b2a472372608965f97c6f18cc46e7e82a2d7d8d898cfc2`.
Independent production review reports no blocker. Phase2 retains end-to-end
client visitor fan-out, broader multi-day/movement-mode/perk interactions and
full old-save loading evidence. Art Not done; no GUI or playable promotion.

Current verified checkpoint: UP-101 Quartermaster is active. Ballista/Tent/
Catapult accepted actions prove the genuine50% extra activation, once/cart guards,
versioned state and shared detached forecasts; a production Tent AI action is
submitted authoritatively. Final both-target21374 exits0; principal47670 passes
6/6 in2.277s; activated88418 passes22/22 in8.107s, zero skips. Data/inventory19/19
pass. Coverage151→152/310, planned159→158; War Machines5active/5planned. Binary:
`c0cad3f2435ba0886c286ce5bb311170cbec7a58b06af60fa2d57e96e3ade5d8`.
Independent review has no blocker. Phase2 retains future activation consumption,
low-output Catapult fallback, broader Wait/Order/controller interactions and
full binary battle-save evidence. Art Not done; no GUI or playable promotion.

UP-101 Quartermaster is now in production source with required output, authority,
persistence, AI and hover/log feedback. The isolated six-case fixture is frozen;
independent review has no remaining blocker after its bounded repairs. Initial
build88648 failed; the repaired build completes both targets. Principal73624
passes five of six cases with zero skips; Catapult activation setup fails before
structural-output assertions and is being diagnosed by a bounded fixture owner.
No active-count increase is claimed yet.
Registration remains planned. Phase2 retains future AI activation-state
consumption and the low-output Catapult legacy-fallback edge; no GUI/promoted
playable evidence exists for this candidate.

Current next-item audit: UP-101 Quartermaster is being mapped as an independent
unblocked candidate. Required coverage is an immediate genuine extra activation
with half output, not a same-activation second attack. Runtime and consumer maps
must cover Ballista, Tent and Catapult paths, activation lifecycle and once-per-
combat Ammo Cart survival. No new active count or execution evidence yet.
Maps completed: Tent output must be halved before missing-HP cap; Catapult uses
the authoritative Lua callback's structural conversion; Ballista uses shared
damage actual/forecast. Side-owned versioned extra-activation state avoids
depending on omitted CUnitState binary fields. Root found that current
afterGetsTurn does not generally reset moved/wait flags: implementation must
refresh those explicitly for the new genuine reason, preserve continuations and
exclude Morale recursion. The next step is implementation, not repeated mapping.

Next-item audit: UP-100 Field Workshop remains planned. Independent maps identify
shared Tent target/output admission, a new structural repair update and cached
wall-sprite refresh as required production seams. Destroyed machine/fortification
scope is asked before implementation; tower shooter removal means HP-only revival
would be incomplete. No new implementation or coverage is claimed for this map.

Current verified checkpoint: UP-099 Master Gunner. Saved same-activation
follow-up state permits an independently selected second Ballista shot at60%
shared forecast/actual damage, with raw pending action restrictions, explicit
decline and fresh legal AI target selection. Both-target76128 exits0 after the
recorded compile repairs. Principal71745 passes4/4, zero skips in1.450s;
activated74243 passes16/16 Master Gunner/Engineer/Piercing/Surgeon cases, zero
skips in5.022s. Data/inventory19/19 pass. Coverage150→151/310, planned160→159;
War Machines4active/6planned. Test binary:
`11b4dc190f413736f8868e88e654fb27276a81f87b8b652ab6b2ac49fbdebecb`.
No GUI, artwork approval or playable snapshot promotion. Initial-shot AI
multi-target lookahead remains Phase2. Breachmaker topology awaits user choice.
Phase2 review finding: issuing Second Wind between the two shots currently
retains Ballista continuation before the Order's immediate-target activation
branch. That interleaving requires explicit activation handoff/resumption work;
ordinary Hero Action permission is not proof that every Order interaction is
correct. Also retain external extra-attack stacking and full binary battle-save
coverage behind the pre-existing Veteran history fail-closed boundary.

Previous verified checkpoint: UP-098 Fortification Engineer. Shared saved-perk/
defended-town eligibility grants deterministic manual tower control and evaluates
tower damage from floor(125% Siege), not125% total output. Client43691 exits0.
Production review has no blocker; the fixture's nested-registry lookup was
corrected before execution. Both-target19393 exits0; principal2955 passes4/4,
zero skips in1.624s. Activated75235 passes12/12 Engineer/Piercing Bolts/Surgeon
cases, zero skips in3.960s. Data/inventory19/19 pass. The perk is active;
coverage149→150/310, planned161→160; War Machines3/7. Binary:
`87d29702e613cd333db104a2266b8dd2231dc2a7416e15e1a03e7e94dbead788`.
Phase2 retains controller-transfer and ordinary automatic-activation breadth.
No new save state, GUI, art approval or playable promotion.

Previous verified checkpoint: UP-096 Piercing Bolts is active and Phase1 complete.
Physical Ballista shots ignore50% target Creature Defense through the shared
damage callback/script payload, using the current controlling hero's saved perk.
Actual/UI/detached AI use the same calculation; hero PDR, Frenzy's own-Defense
trade, melee, nonphysical and non-Ballista attacks are unaffected. Client70423
and combined42241 exit0. Principal18354 passes4/4 in1.560s, zero skips. Activated
21661 passes all4 new and4 Surgeon cases plus spell-like damage classification;
the11-case batch has9 passes and2 existing Archery fixture prerequisite failures,
recorded for Phase2, not described as a green batch. Data/inventory19/19 pass.
Independent production/fixture reviews have no blocker. Binary:
`bcd78c12f52089a2b50a24b8628c58eb82373dd4b9da4095d9446504b7cae427`.
Coverage148→149/310, planned162→161; War Machines2/8. Phase2 retains actual
controller-transfer interactions and legal prerequisite setup for the two named
Archery tests. No new persistent state; existing perk saves apply. Art Not done,
no GUI or playable promotion. Battlefield Medic is mapped; persistence after
combat awaits clarification before implementation.

Previous verified checkpoint: UP-095 War Machines Surgeon is active and Phase1
complete. Actual friendly Tent HP gain removes exactly one shared physical
affliction: Poison → Disease → Bleeding → other eligible groups by application
order. Stored physical Poison uses authoritative unit-state updates; marked groups
use exact source/sid removal. Named combat feedback and current-controller AI
target selection exist. Generic zero-value metadata is parsed, guarded for save
downgrade, stamped in effect packets, and preserved in live/detached branches.
Both-target67115 and final64786 exit0. Principal29476 passes17/17; activated59354
passes34/34 in6.552s, zero skips (`UP095-activated.log`/`.xml`). Data/inventory19/19
pass. Binary: `beb89e7e361cdd3f42e9b664a8b3de5d9b816b6d1662410896ca39a76d273e1d`.
Independent runtime/consumer/fixture review has no blocker. Coverage147→148/310,
planned163→162; War Machines1/9. Generic Bleeding removal is verified, not a
production Bleeding damage producer. Phase2: packet-wide rollback of preprocessing
on a later invalid unit, broader controller-change interactions, status-producer
duration consistency and localization. Art Not done; no GUI or playable promotion.

Previous verified checkpoint: Discipline Fearless is active and Phase1 complete.
The current controller's perk filters positive non-magical CREATURE_ABILITY
FEARFUL contributions, preserving spell/unclassified sources and nonpositive
caps. Authoritative full prevention bypasses the RNG/Twist resolver and emits a
hero/stack-named blocked-check log; detached AI uses the shared nominal chance.
Both-target36048 and repaired24535 exit0; principal13527 passes5/5; activated
native30226 passes27/27, zero skips in9.008s (`UP093-activated.log`/`.xml`). Binary:
`275f9ef5ddf86b424924369988ccb25fbd0dff0b9bf6e6ecc0b444df8438ead5`.
Data/inventory19/19 pass; independent source and fixture reviews have no blocker.
Coverage146→147/310, planned164→163; Discipline5/5. No extra stored state; existing
perk persistence applies. Art Not done; no playable promotion. Phase2: future
same-stacking-key source interaction, direct unchanged-RNG evidence, nominal AI
probability/history correlation. Next mapped items: Heroic Spirit activation
timing and Veteran Cohesion HP reference need the requested design clarification.

Previous verified checkpoint: Discipline Hold Fast is active and Phase1 complete.
Authoritative Defend and captured Hold the Line recipients receive an isolated
MINIMUM_MORALE zero floor until the next genuine Creature Activation. The generic
duration has parser/schema/docs and an append-only save feature with downgrade
rejection; live and detached expiry preserve continuations and stopped queues.
Second Wind now recognizes canonical Defend completion and uses the current
controller throughout activation/start/end bookkeeping. Existing queue flags and
legacy target eligibility remain unchanged. Combat logs and AI forecasts consume
the shared grant. Both-target2758 exits0; principal85726 passes7/7; activated
native69879 passes28/28, zero skips in8.612s (`UP092-verified.log`/`.xml`). Binary:
`a20a31dee29b913dec0a59c64554e318093c9555858513839ec70aa6700d9bab`.
Data/inventory19/19 pass; independent duration, production, fixture and controller
reviews have no remaining blocker. Coverage145→146/310 active, planned165→164;
Discipline4active/6planned. Purpose-made art Not done; no playable promotion.
Phase2: future harmful consumers must extend Purify's temporary-duration mask;
control changes during an activation, cross-perk Morale forecast correlations,
and full battle-snapshot interactions remain separate integration evidence.
Next: Fearless source-aware immunity to non-magical fear, mapped read-only.

Previous verified checkpoint: Discipline Standard Bearer is active and Phase1
complete. Shared contextual Morale adds one+1 before caps/MIN, preserving immune
and MAX handling. It queries live or candidate occupied footprints/current control
without cached sibling-dependent bonuses. Both real Morale gates, four AI status
valuation families and the existing compact-panel display/refresh use the query.
Both-target42607 exits0; principal82873 passes4/4; activated native74455 passes
39/39, zero skips in10.603s (`UP091-standard-bearer-activated.log`/`.xml`). Binary:
`f12c0d80bf4aa73ec033bfc80c6b68aa0bb36fed2fd7e92665e2a646ce10d261`.
Data/inventory19/19 pass. Independent production and fixture-repair reviews have
no blocker. Coverage144→145/310 active, planned166→165; Discipline3/7.
Phase2: explicit double-wide rear-hex and supporter death/resurrection evidence;
Sorrow/Doom retain their existing detached-context helper signatures (no topology
change). No new stored state/counter; existing perk persistence remains the
contract. Art Not done; source UI hook is not playable visual acceptance.
Next: Hold Fast's generic genuine-activation expiry, mapped in UP-092.

Previous verified checkpoint: Battlecraft Reserve is active and Phase1 complete.
Only a delayed waited TURN_QUEUE activation grants +2 movement; Initiative is
unchanged, continuations preserve it and terminal activation cleanup clears it.
Current-controller selection and detached Wait candidates share the helper.
An append-only version-gated BattleInfo sidecar and guarded UnitChanges preserve
the field, while old loads default zero. Both-target34777 exits0; activated
native69809 passes16/16, zero skips in4.778s (`UP091-activated-verified.log`/`.xml`),
five Reserve cases plus seven Battlecraft and four Rally regressions. Binary SHA:
`9fe6ca9152d6e666c651efbea2617ee173a5770b6c8100b571befa07030e6cce`.
Data/inventory19/19 pass; coverage143→144/310 active, planned167→166;
Battlecraft2/8. Independent reviewer spawning failed; root production review is
recorded without claiming independent approval. Phase2: regenerate newly reachable
attacks in the Wait-choice catalogue, and audit the pre-existing full unit-state
binary snapshot contract. Purpose-made art Not done; playable delivery unpromoted.
Next missing item: Standard Bearer dynamic adjacency Morale.

Previous verified checkpoint: Discipline Rally is active and Phase1 complete.
Independent side-long suppression, save/packet288, current-controller ownership,
cached first RNG draw, cancellation before Twist, ordinary activation and
hero-named log feedback are implemented. Detached AI copies/spends only its local
allowance; four Morale-status valuation paths protect one prospective event,
not every stack or every round. Build93251 exits0 for both targets; principal3559
passes4/4 and activated native7731 passes47/47, zero skips in13.611s
(`UP090-activated-verified.log`/`.xml`); binary SHA-256
`f012740b9bf1a26fc50ea0a16a8fa4ae9b759401e3c972d812ff8d47ed5b1d2b`.
Data/inventory19/19 pass. Independent production review has no blocker; its
stochastic-precedence and numerical-AI fixture findings were strengthened before
the passing gate. Coverage142→143/310 active, planned168→167; Discipline2/8.
Ranks84/93 and combat60/67 unchanged. Deferred: exact cross-stack first-trigger
ordering, future eligibility and multi-round probability correlation in AI;
future Unbreakable coexistence requires a suppression-precedence decision.
Purpose-made art Not done; playable delivery unpromoted.

Previous verified checkpoint: Twist of Fate is active and Phase1 complete.
Independent side allowance, save/packet transition, current-controller adverse
Luck/Morale/procs/resistance, one final redraw, scripted binomial result and
minimum branch-local AI hooks are implemented. Late Hand of Fate collateral
uses its actual chosen recipient's final MR decision without replacement.
Both-target31690 exits0; native95364 passes43/43, zero skips in12.421s
(`UP089-activated-verified.log`/`.xml`); binary SHA-256
`67ba01cfdc050aade2741bee5187d9e2d870ba93b06ffe13c3aaa4bf7b6d3d19`.
Data/inventory19/19 pass; independent Astra activation review has no blocker.
Coverage141→142/310 active, planned169→168; Luck6active/4planned. Ranks84/93,
combat60/67 unchanged. Broader reflection/control-change/cross-category ordering,
full attack-script dispatch and conditional AI valuation remain Phase2. Art
Not done; playable delivery unpromoted. The earlier checkpoints below are history.

UP-089 Twist of Fate infrastructure is source/native verified, not a completed
perk. Independent side allowance, append-only save/packet287, monotonic state
application, authoritative one-final-redraw resolver, spell callback bridge and
detached nested-branch isolation are implemented. Both-target1364 exits0
(`UP089-infrastructure-build.log`); native73236 passes32/32, zero skips in8.536s
(`UP089-infrastructure.log`/`.xml`), six new infrastructure cases plus26 Luck
regressions. Binary SHA-256
`adefcb23c6001b687c0f425d29dafbc69ff9fbe749edbb31063a5c86275b08b8`.
Data/inventory19/19 pass; independent review has no blocker. Registration remains
planned until actual Luck/Morale/hostile-proc/resistance interception and minimum
AI forecasts are wired and verified. Coverage stays141/310,169 planned; Luck5/5.
Deferred direct assertions: invalid-side resolver fallback and malformed current
state decoding. No playable promotion or purpose-art approval is implied.

UP-089 runtime source is now frozen for validation: attack/Morale and hostile
chance abilities use the current harmed controller; suppression precedes the
reroll, and fractional favorable-proc chance is prepared once. Spell MR uses
cached first draws and one lazy final decision per actual recipient, covering
chain preparation and late collateral without untargeted expenditure. Callback
lifetime is scope-bound; prediction cannot spend authoritative state. Minimum
AI source has local callback expenditure and first-event Luck/MR forecasts.
Production compile77876 is running; a bounded actual-runtime fixture is being
written separately. These are source claims only. Registration/counts remain
unchanged, and longer conditional forecast sequences await Phase2 evidence.

Production compile77876 exits0; existing focused native59072 passes32/32,
zero skips in8.445s (`UP089-runtime-regressions.log`/`.xml`). Binary SHA-256
`e1196ff56aa5e28adc2240fc7598099266a0d654b5a781abac1cec2bd0b2d7c2`.
This is a regression pass, not evidence for the new fixture or full scope; actual
runtime cases and independent review are pending. No count change is justified.

Bounded runtime checkpoint is verified: both-target4456 exits0; final native34416
passes35/35, zero skips in9.458s (`UP089-runtime-verified.log`/`.xml`), including
actual lazy primary chain MR with unchanged secondary HP, first-event Luck
forecasts and local AI callback isolation. Binary SHA-256
`e6bd7c8ccd8ee258a953e1e4103005def3be6210b51ca3d7e56d5a7efba5b852`.
Independent source/repair reviews have no blocker; earlier fixture failures are
retained. Scripted Destruction/Transmutation/Death Stare and focused principal
attack/Morale/proc/collateral callsite evidence remain required Phase1 work.
Registration stays planned; coverage stays141/310 active,169 planned, Luck5/5.
No playable delivery or art approval is implied by this dependency checkpoint.

Scripted hostile-proc source is implemented through an explicit recipient bridge
and a capped binomial-count bridge. Root repaired callback/Unit API compile
mismatches after build18304 failed. Both-target23124 exits0; native26641 passes
39/39, zero skips in10.901s (`UP089-scripted-regressions.log`/`.xml`), adding
actual negative Luck, negative Morale, suppression precedence and hostile Death
Blow ownership evidence. Binary SHA-256
`a19116a05068cdb05e067c624335c5f3deec38f8e232358af3cd82ca076ee9d5`.
Data/inventory19/19 pass. Principal real Lua scripted-ability fixtures are still
being written, and late-collateral evidence remains outstanding. Registration
and completed coverage therefore stay unchanged; no playable promotion.

Scripted checkpoint verified: both-target50122 exits0; final combined native6674
passes42/42, zero skips in12.110s (`UP089-scripted-verified.log`/`.xml`). Three
real Lua cases establish Destruction final cancellation, an exact capped Death
Stare redraw and immune-target non-consumption. Binary SHA-256
`a18f4d8ed6cc97c933fa9ae37f1b5ab41b0b1be0559f28ac3e07d5416809cdd5`.
Independent scripted review was service-rejected repeatedly; root reviewed the
diff without claiming that approval. Full attack-event dispatch/controller
matrices and conditional AI forecasts remain Phase2. Late collateral remains
the next principal gate; registration and counts stay unchanged.

UP-087 Chain of Fortune is source/native verified. A positive friendly strike
arms one+1 Luck benefit for the next different friendly stack's attack. Same
source follow-ups retain it; unused benefits carry across rounds. Consumption
precedes rearming and respects caps/No Luck. Current-controller reactions,
append-only save/strike-packet state and detached certain/UNKNOWN AI candidate
and selected replay are verified. Both-target3912 exits0
(`UP087-repaired-build.log`); native73716 passes26/26, zero skips in6.779s
(`UP087-verified.log`/`.xml`), including six Chain cases and20 Luck regressions.
Binary SHA-256 `9a0a233a0abb92f6eb1ca9ad570568301bc8e1d30fa784e4ecde484a599a180e`.
Data/inventory19/19 pass; independent review has no blocker. Coverage140→141
active perks, planned170→169; Luck5/5. Ranks84/93 and combat60/67 unchanged.
Phase2 retains broader reaction/perk matrices and playable combat-log acceptance.
Purpose-made art Not done; no playable promotion. Twist of Fate's approved
adverse-roll scope is canonical and is the next implementation slice.

UP-086 Gambler is source/native verified. The first friendly attack each round
receives+3 Luck through the shared capped/immunity-aware callback and spends
the side's window regardless of outcome. A failed positive trigger adds a
uniquely identified per-unit-2 Luck bonus before subsequent attacks, retained
through ordinary Hero Spell/Order continuations and removed on genuine
activation, including Second Wind. Current-controller ownership, side/round
isolation and detached branch histories are verified. Captured pre-consumption
outcomes preserve ordered AI replay without inventing stochastic rolls; state
and strike packets use append-only versioning. Final both-target76677 exits0;
native39678 passes20/20, zero skips in5.324s (`UP086-verified.log`/`.xml`).
Binary SHA-256 `3ef29f6941beefed86678fe8ce02a0d383d3330f45d38eeca490b7f6568ab6da`.
Data/inventory19/19 pass and independent source review has no remaining blocker.
Coverage139→140 active perks, planned171→170; Luck4/6. Ranks84/93 and combat60/67
unchanged. Phase2 retains stochastic conditional penalty correlation, broader
reaction/controller matrices, committed-replay recovery and detached-expiry
fixture breadth. Purpose-made art Not done; no playable promotion. Chain of
Fortune's different-stack and carry-until-used decisions are now canonical;
Opportunist's reaction/own-activation question remains pending.

UP-084 Second Chance is source/native verified. Shared army Luck state tracks
its independent once-per-combat allowance; Nature's Providence remains
once-per-round. Authoritative physical creature rolls suppress only the first
negative outcome, replicate it and emit combat feedback. Detached forecasts
consume only selected branch state, including certain multihits/retaliation;
forced-positive outcomes cannot also spend the negative allowance. Append-only
serialization rejects lossy downgrade and defaults old state inert. Final
both-target23927 exits0; native6011 passes14/14, zero skips in3.859s, reports
`UP084-verified.log`/`.xml`. Binary SHA-256
`8a34b62c978fa1842b571c9d1e0c5344583d18aa4678e6c5f73f5d61cd553f5f`.
Data/inventory19/19 pass; independent material findings repaired. Coverage
138→139 active perks, planned172→171; Luck3/7. Ranks84/93 and combat60/67
unchanged. Phase2 retains probabilistic multihit distribution, explicit
stochastic-result replay and fully absorbed reaction/controller-change matrices.
Art and playable delivery remain pending. Serendipity awaits its round1 policy;
Gambler is being mapped read-only, without bypassing that design question.

UP-082 Lucky Aim is source/native verified. Shared damage payload adds25%
target Creature Defense ignore only on positive Lucky physical shots by ordinary
creature shooters, using selected/active/rank gating and current controller's
hero. Elven Precision remains25% and combines in the same capped target-side
calculation. No polling, new state, save format or duplicate AI formula.
Final both-target62197 exits0; native69961 passes8/8, zero skips in2.487s
(Lucky Aim3, Fortune's Favor3, existing Elven Precision2), reports
`UP082-final.log`/`.xml`. Binary SHA-256
`32995a012bf94c0d134449a402d984ea974d4a500902cb7520787e3a6aec042c`.
Data/inventory19/19 pass and repaired independent review finds no blocker.
Coverage137→138 active perks, planned173→172; Luck2/8 active/planned.
Phase2 retains explicit executed shots, dual-perk/Defense-ignore combinations,
special reactions and general fractional-damage rounding (one-HP truncation).
Purpose-made art and playable promotion remain pending. Perfect Fortune and
Lucky Recovery have narrow design questions pending; other missing coverage
must continue rather than treating the whole goal as blocked.

UP-080 Fortune's Favor is source/native verified. Accepted Luck selection,
rank changes and reconstruction derive exactly one permanent +25
LUCKY_STRIKE_DAMAGE_PERCENTAGE bonus from existing saved perk state; no polling,
additional combat state or new damage formula. Positive Lucky Strikes gain
+0.25x; ordinary and negative damage remain unchanged. Shared expected-damage
calculation agrees on detached AI units. Both targets build56878 exit0;
native69200 passes4/4 (three new cases plus ordinary Luck-rank regression)
with zero skips in1.398s, reports `UP080-final.log`/`.xml`.
Binary SHA-256 `08679d27b7f2e6055824309b8aca67e7133233b7739e2c4c6428b3703b714e04`.
Data/inventory19/19 pass. Coverage136→137 active perks, planned174→173; Luck1/9
active/planned, opening normal Advanced progression. Save roundtrips, explicit
ranged/retaliation/reaction combinations and wider Sylvan coexistence remain
Phase2; no art approval or playable promotion is implied.

UP-078 Veteran is source/native verified. At genuine activation start the shared
helper consumes actual physical creature HP-loss history and restores floor15%,
capped to surviving wounds. Temporary/Guardian absorption, magical damage,
clones and Phantom Integrity do not qualify; no resurrection or polling.
JSON unit updates preserve the interval and reject older writers that would
drop it. Unsupported binary battle snapshots reject pending history; ordinary
combat saving is already blocked. Owner-scoped AI uses the same helper and
explicit physical provenance. Guardian preview/replay now retains incoming
damage separately from resolved HP damage, avoiding double absorption and
pre-buffer HP caps. Both-target8101 passes; native19447 passes14/14, zero skips,
in4.124s. Reports `UP078-focused-buffer.log`/`.xml`; binary SHA-256
`ef35142a822610a400a5f9dad60f358fb21645a2cb15736ce325a84f0428dca8`.
Data/inventory19/19 pass; independent review blockers repaired. Coverage135→136
active perks, planned175→174; Armorer6/4 active/planned. Ranks84/93 and combat
identities60/67 unchanged. Phase2 retains fully absorbed multistrikes and
Guardian reaction/Rain combinations. No purpose-made art or playable promotion.
Last Stand awaits two design answers; Fortune's Favor is the next unblocked
Basic foundation to open Luck progression, using the existing bonus multiplier.

UP-077 Formation Fighting is source/native verified. Shared current-controller
and projected-footprint adjacency prevents Shroud damage/retaliation and Flank
melee/history, retaining Combined Arms ranged benefits. Independent10% physical
reduction composes within the existing cap. Owner-scoped detached predictions
and authoritative melee hits agree; rank loss, ally control/movement/death and
two-hex self aliases have focused evidence. Final both-target74477 passes;
native61868 passes18/18, zero skips, in4.465s, reports
`UP077-focused-final.log`/`.xml`. Data/inventory19/19 pass. Coverage134→135/310
active perks, planned176→175; Armorer4/6→5/5, ranks84/93 and spells60/67
unchanged. Phase2 retains other allies' multi-blow projected state, hidden
opponent perk uncertainty and the recorded Encirclement repeated-hit assertion.
No art, rendered acceptance or playable promotion is claimed.
UP-076 deterministic Diplomacy still awaits the authored free-join exception
decision already recorded in UP-048. Existing raw Army Value and full-stack
pricing paths are mapped; the nine missing ranks are not silently activated.

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
| Skill perks active | 193/310 | 117 planned; Night Prowler is the newest source/native-verified activation. Learning is 3/10; Estates is 5/10; Battlecraft is 5/10; Command is 7/10. Active status alone does not certify every mechanic. |
| Faction Skill ranks active | 21/27 | Six planned ranks. |
| Faction perks active | 55/90 | 35 planned perks; Night Prowler has committed hostile transit, first-strike consumption, unused expiry and isolated AI route/replay evidence. |
| Canonical combat-spell identities registered | 60/67 | 7 missing/inactive; Shield of Chaos is the newest identity. Chaos is 6/11, Light 11/11 and Nature is 9/11 by identity, not blanket mechanic certification. Rendered/playable delivery remains separate. |
| Distinct perk-granted Mass spell entries | 5/5 | Mass Curse/Sorrow, Bless, Regeneration and Slow have focused native evidence. Slow uses its separate permanent virtual grant, 60% family magnitude and ordinary action transport. These five variants are additional to the 67 school-roster identities; rendered/playable acceptance remains separate. |
| Adventure spells with ordinary acquisition | 5/5 | All five have a validated town unlock/purchase path, saved town state, visitor learning, client purchase UI, and AI purchasing. The five-spell effect audit finds missing canonical clauses in every spell (UP-056); no blanket effect-complete claim. Rendered/playable purchase remains unverified. |
| Orders registered | 8/8 | Config and `HeroCommand::isActive` agree. UP-146 adds native-verified independent simultaneous state/effects and action/AI/UI hooks; broader per-Order interaction coverage remains Phase2. |
| Hero-class Leadership profiles | 18/18 | Capability data exists; transfer paths remain a user-reported correctness gap. |
| Creature base-line Leadership requirements | 64/64 | Data coverage only; individual creature mechanics remain unaudited. |
| Creature category forms | 126/126 | 50 Core, 58 Elite, 18 Champion are registered; this is not creature-ability coverage. |
| Siege output formula families | 4/4 | Ballista, Catapult, Tent and defensive tower outputs have data; universal Blacksmith access and Ballista Yard's weekly Siege effect are implemented with focused native tests. Rendered/playable acceptance remains open. |
| Recruitment perks active | 6/10 | Four planned; external, solo town and split town Muster have server and AI paths. |
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
requirements in total. The historical 2026-10-02 registry had 84 active rank effects and 180
active perks, leaving nine ranks and 130 perks planned. These counts were
rechecked directly from `config/newHorizonsPerks.json` on 2026-10-02; they are
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
| Armorer | 3/0 | 7/3 | Bastion, Formation Fighting and Veteran have focused live/detached damage evidence. Three perks missing; Last Stand and Defiant await design choices. |
| Archery | 3/0 | 10/0 | All ten perks are active; focused evidence pending |
| Battlecraft | 3/0 | 5/5 | Entrench, Reserve, Passing Lines, Tactics and Redeployment active. Focused native evidence covers delayed movement, friendly transit and initial/final deployment; rendered/actualAI deployment execution remains Phase2. Five perks remain planned. |
| War Machines | 3/0 | 5/5 | Surgeon and Piercing Bolts have focused live/detached evidence; Fortification Engineer has real fortified-town manual-shot evidence; Master Gunner has accepted independently targeted second-shot evidence; Quartermaster has accepted half-output Ballista/Tent/Catapult extra-activation evidence. Five perks remain planned. Battlefield Medic persistence awaits clarification. |
| Discipline | 3/0 | 7/3 | Steadfast joins Unbreakable, Inspirational Leader, Rally, Standard Bearer, Hold Fast and Fearless with focused authoritative and detached evidence. Three perks remain planned; Esprit de Corps composition scope, Heroic Spirit activation timing and Veteran Cohesion HP reference await clarification. |
| Recruitment | 3/0 | 6/4 | Four perks missing |
| Command | 3/0 | 7/3 | Aggressive/Defensive, Veteran, Combined Arms, Commanding Presence, Battle Plan and Double Command have focused runtime/AI evidence. Iron Will, Crisis Command and Seize Initiative remain planned pending their recorded narrow design rulings. |
| Light Magic | 3/0 | 9/1 | Sanctuary Keeper's linked Morale lifetime and actual AI cast are native verified; Litany grants distinct Mass Bless, alongside Benediction, Healer, Guardian, Aegis, Purifier, Retributionist and Crusader. Miracle Worker remains planned. |
| Shadow Magic | 3/0 | 9/1 | Grand Malediction now grants distinct Mass Curse/Sorrow with native cast/forecast evidence, alongside the eight earlier perks. Plaguebearer awaits its normal-limit definition. |
| Nature Magic | 3/0 | 7/3 | Geomancer's structural/terrain effects and actual AI cast are native verified alongside Venomancer's whole-Base snapshot and independent Toxic Spines. Herbalist, Rootcaller, Beastcaller, Verdant Warden and Verdant Communion active. Mire Shaper, Worldroot and Elemental Conjurer remain planned. |
| Havoc Magic | 3/0 | 7/3 | Three perks missing; Mine Layer has focused live/detached placement evidence |
| Sorcery Magic | 3/0 | 10/0 | Evidence audit required |
| Chaos Magic | 3/0 | 3/7 | Blinkmaster, Weaver and Paradox Shield are active with focused evidence recorded above; seven perks remain planned. |
| Spellcraft | 3/0 | 4/6 | Grand Formula scales the first accepted Level 4-or-5 hero spell's SP term by 150%; Arcane Focus, Spell Penetration and Empower Spell remain active. Empower Spell uses the final Wisdom-adjusted 12-Mana threshold. Basic/Advanced/Expert efficiency is 110/120/130% under saved v3 rules. |
| Wisdom | 3/0 | 8/2 | Mana Conservation post-combat Normal recovery, Archmage first accepted Level 4/5 discount, Arcane Reservoir flat capacity, Deep Knowledge shared bonus-growth chance, Meditation completed-day Movement recovery, Prepared Caster's first accepted combat-cast discount, Mysticism daily Normal recovery and Intelligence capacity are implemented. Arcane Memory source passes focused scroll/provenance/completion cases but remains planned pending neutral Adventure acquisition policy. One other perk remains missing. Broader interactions and playable acceptance remain open. |
| Warcasting | 3/0 | 5/5 | Spellward has focused live/detached/current-controller damage evidence. Five perks missing; Combat Casting and Enchanted Command await shared rule decisions. |
| Logistics | 3/0 | 5/5 | Five perks missing; Roadmaster/Wayfarer native verified |
| Diplomacy | 0/3 | 0/10 | Ranks and progression missing |
| Estates | 3/0 | 5/5 | Land Surveyor, Tax Collector, Investor, Estate Network and Financier supply working Basic/Advanced/Expert perks. Accepted mine-capture grants, daily income, weekly Wood/Ore, weekly treasury interest, Investor's pre-income treasury snapshot and AI resource receipt/selection are native verified. |
| Learning | 3/0 | 3/7 | Mentor, Quick Study and Field Study supply working Basic/Advanced perks and open ordinary Expert-rank progression. Meetings, weekly persistence, initial offer reroll, stronger-opponent awarded XP and query/RNG parity are native verified. Academic Study awaits first-visit timing. |
| Luck | 3/0 | 6/4 | Fortune's Favor, Lucky Aim, Second Chance, Gambler, Chain of Fortune and Twist of Fate source/native verified; four perks remain planned. |
| Divine Mandate | 0/3 | 0/10 | Ranks and progression missing |
| Sylvan Luck | 3/0 | 10/0 | Evidence audit required |
| Metamagic | 3/0 | 10/0 | Evidence audit required |
| Shroud of Malassa | 3/0 | 6/4 | Basic Backstab/Ambusher/Shadow Assault and Advanced No Escape/Evasive Shroud/Night Prowler are active; four perks remain planned. Night Prowler has live/AI crossing, first-strike and unused-expiry evidence; its flying negative is predicate-only. |
| Demonic Gating | 3/0 | 10/0 | Evidence audit required |
| Necromancy | 3/0 | 3/7 | Seven perks missing; one inert hook |
| Bloodrage | 3/0 | 7/3 | War Drums, Fury Unbound, Endless Bloodshed, Unrelenting, Berserker, Blood Scent and Rage Through Pain have focused live/AI evidence and legal progression; First Blood, Slayer and Avatar of Rage remain planned. |
| Bulwark of the Mire | 3/0 | 9/1 | Nine perks are active in committed source; 53/53 native regressions pass; persistent Poison status builds and passes focused tests; Deep Bulwark and rendered/playable evidence remain open |
| Elemental Rebirth | 0/3 | 0/10 | Ranks and progression missing |

Strict progression requires a perk at the preceding rank before the next Skill
rank. Three Skills therefore cannot normally advance beyond Basic because they
have no active Basic perk: Diplomacy,
Divine Mandate, and Elemental Rebirth. Fortune's Favor opens Luck; Tax Collector opens Estates and
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
