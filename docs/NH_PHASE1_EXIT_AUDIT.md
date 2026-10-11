# Phase 1 implementation-coverage exit audit

Audit date: 2026-10-10. Reviewed implementation:
`6e21523dfef8c71151605f8d722f11962d4c3311`.

## Authority and decision

The canonical [New Horizons specification](design-sources/New%20Horizons.md),
[Version 1.0 scope](NH_VERSION_1_0_SCOPE.md), and integrated amendments govern
this audit. The [functional completion matrix](NH_FUNCTIONAL_COMPLETION_MATRIX.md)
records item-level consumers, focused evidence and historical failed attempts.
Registration counts alone do not establish functional completion.

Root decision after independent final reconciliation: Phase 1 implementation
coverage is substantially complete. Transition to Phase 2 — Stabilization and
Integration. Resolve the recorded interaction backlog rather than adding new
mechanics. This is not a Version 1.0 release-candidate declaration.
Version 0.85 is a development identifier, not a completion percentage.

## Implementation accounting

| Area | Accounted scope |
|---|---|
| Skills and ranks | 31 Skills; 93 active Basic/Advanced/Expert rank effects |
| Perks | 310 active: 220 generic and 90 faction; zero planned |
| Faction foundations | 9 Skills and 27 rank effects, included above |
| Combat spells | 67 detailed identities and 5 explicit perk-granted Mass entries |
| Adventure spells | 5 acquisition, payment/day-state, control and minimum-AI paths |
| Orders | 8 typed action/result, targeting and shared-action/AI paths |
| Leadership | 18 class profiles and 66 base-line requirements; per-slot admission |
| Creature categories | 128 forms: 52 Core, 58 Elite and 18 Champion |
| Siege outputs | Ballista, Catapult, First Aid Tent and defensive towers |
| Hero initialization | 144 development profiles and Leadership-safe original-composition armies |

The matrix maps defining clauses to production consumers and retained focused
execution evidence. The category count is not 128 creature-ability certifications.
The hero inventory includes 52 rewritten biographies and 92 intentional inherited
biographies, rather than claiming all biographies were rewritten.

All 37 grouped unique-building rows, ten artifact-framework rows and six
specialty-conversion families have explicit dispositions. These are audit
denominators, not accepted-mechanic fractions. Retained growth, markets, boats,
income, Luck/Morale and qualitative effects are not individually runtime-certified.
The conditional flat-primary-specialty conversion has no applicable built-in
flat hero-stat producer; no conversion or whole-family completion credit is
assigned and no unauthored producer is invented.

Principal production paths are mapped for action/activation, separate Speed and
Initiative, Luck/Morale, creature mechanics, recruitment/Diplomacy, dwellings,
buildings, artifacts and specialties. Later focused repairs close demonstrated
minimum-AI omissions, including spell evaluation, active-creature Second Wind,
town visits, joining and detached status/activation state. AI hooks do not certify
strategic quality or completed autonomous games.

Required combat, hero-development and adventure-magic controls exist sufficiently
to exercise the mechanics. Bounded ordinary evidence covers starting-profile
popup/encyclopedia, empty books, Guild acquisition, Hold footprints, Storm
targeting and Frozen feedback. Versioned meaningful save/packet state has typed
admission, older absent defaults and focused round-trip/rebind controls. Broad
campaign saves and compound temporary-state resumes remain unverified.

## Blocking Frozen defect repaired

An earlier automatic-forfeit thaw interpretation was withdrawn: the same stack
forfeited again without status removal. The repair completes the accepted normal
automatic skip, runs Plague processing once, removes Frozen while retaining its
once-per-round receipt, and guards synchronous computer-battle finalization.

Focused native validation passes 46/46 with zero failures/errors/skips:
Server 9, Foundation 17, Snapshot 7 and Plaguebearer 13. XML SHA-256:
`27eba58a533e076c03dbac13f69aae3d677000b9055c9def164de8587938b307`.
The native engine is statically linked; a copied facade was pinned, not falsely
claimed as mapped by the test process.

The final isolated, silent ordinary Linux witness observes natural own-primary
Freeze, normal activation forfeiture, authoritative removal, visible thaw and
the same surviving stack's subsequent normal attack. Retained receipt SHA-256:
`0c37e5054ca3cd9254fa3a53c4ba0b92bb6cc00218522d8f711b1d35c96735e3`.
Earlier scoped evidence establishes the tooltip, prism/initiative marker,
Shatter preview, no retaliation, actual damage and surviving-recipient clearing.

The final witness has one no-town fixture warning and nine known attack-preview
diagnostics; it is not a warning-free run. No new full immediate pre-run scan was
performed: the preceding verified protected stage and adopted binary identity
form the baseline. Full post-run verification confirms all 1,026 files unchanged.
Separately isolated shard-animation capture and latest Save-button rendering
remain unverified presentation breadth.

## Historical preview delivery (withdrawn)

`v0.85.0` was published as an experimental prerelease, pinned to the reviewed source above.
That release and tag have subsequently been withdrawn; this section records the
original delivery evidence, not an available download or acceptance of newer builds.
The Windows build run
[38078142472](https://github.com/thegandalf196/new-horizons/actions/runs/38078142472)
completed successfully. Both actual public player downloads were streamed and
independently hashed after publication:

| Player archive | SHA-256 |
|---|---|
| Linux x64 | `1335e4f5f659b22b7140e16cdee2b82024f81fc12842d33fc6e40209253c2464` |
| Windows x64 | `cb3eb79d41ef8fec34301f44f9613b56308c628f5bb84a08ca62768a6e4bc382` |

The release also contains corresponding engine, dependency and packaging source
companions and aggregate checksums. Required licenses, attribution and provenance
remain distributed. Windows source matches the reviewed content after CRLF
normalization, with host-mode differences qualified; Linux source preserves Git
bytes/modes. These checks are not an independent full rebuild or legal certification.

Both packages retain the unchanged selected NHART: 3,137 selected resources,
both mounts, no loose-art fallback. Package SHA-256:
`1a91c08b1efa146af5d8f7df0d8a99a5739c94e280fedb13e8fd4d11ae555fc8`.
Original Heroes III Complete data remains external. At that delivery checkpoint,
the usual local Linux launcher selected the accepted runtime bytes; its 807 curated
runtime files matched the final stage, without treating omitted release notices
as runtime resources.

Linux targets Ubuntu 26.04 system libraries; other distributions are unverified.
Windows compilation, setup smoke, ZIP CRC, 1,070 payload checksums, 26 AMD64 PE
images and 272 dependency identities pass scoped independent audit. Native
Windows gameplay, installation, GUI and media behavior remain untested; Linux
rendering does not certify them.

## Phase 2 backlog and limits

The authoritative Plague/Spell Lock activation-end countdown adapter now passes
eleven focused native Plague tests. Hostile seals preserve Plague and pause its
countdown while damage and spreading continue; friendly seals cleanse it. A
neutral-spell targeting predicate had incorrectly classified hostile casts as
friendly; the runtime now uses the established spell-mechanics alignment policy.
Three consumed-slot serialization tests and fourteen Time Stop tests also pass.
These native tests embed the engine; they do not certify the shipped shared-library
boundary or rendered presentation. The eight existing Spell Lock cases now pass
after repairing stale fixtures: seals use an ordinary paid cast and its earned
Metamagic allowance, an accepted Order coexists with both seals before a real
round transition, and the AI mock supplies the matching spell identity. The
combined serialization, Time Stop, Plague and Spell Lock gate passes all thirty-six
cases without skips. Earlier failing results remain recorded privately; this
fixture repair does not bypass production action validation.
The subsequent shared completion adapter passes 39 focused native cases on the
current candidate: automatic Confusion and Bad Morale completion, detached
attack/Frozen completion, captured mitigation, lethal propagation and isolated
Spell Lock clocks. No Quarter/Veteran snapshot and adjacent Frozen/Time Stop/
timed-effect gates pass 29 and 44 cases respectively, without skips. Local
serialized-marker validation works before bonus-graph rebinding; earlier failing
results are retained. These are static-engine tests, not shipped-library or
whole-game save/resume certification. Defend/creature-cast and explicit movement
forecast completion remain separate work. Controlled-target cleansing transitions
still require execution; ordinary friendly/hostile casts do not prove them.

Other recorded interaction risks include composite percentage rounding,
Counterfire/Crossfire ownership provenance, Inspirational aura expiry, Deep Flank
splash/control contacts, automatic Seize/Rapid/Order hooks, compound control and
extra-activation save/resume, casualty/restoration chains, cohort transfers,
dynamic adventure-route invalidation, AI valuation and UI prediction/clipping.
No fresh all-repository suite, full interaction matrix, completed-game campaign
or whole-family graphical certification is claimed. Historical broad/fixture
failures remain recorded rather than erased.

For example, the historical adjacent 38/41 run retains three Counterfire
failures reproduced on its preceding binary/configuration pair. Later focused
Counterfire repairs passed their bounded gates; neither result is a fresh broad
suite certification. The matrix retains the failed setup attempts and subsequent
corrected receipts separately.

Costs, coefficients, growth/economy, Leadership, original random army ranges,
equal-Elite Conflux and timing/geometry/stacking judgments remain provisional in
[the review ledger](NH_PROVISIONAL_RULE_REVIEW.md) and
[Pending Changes](NEW_HORIZONS_PENDING_CHANGES.md). Numerical tuning is not
missing source functionality and does not excuse a crash or inert mechanic.

Governors, regional administration, caravans, anti-chaining and the experimental
single-expeditionary-hero restriction are intentionally outside Version 1.0.
No new troop line, undefined unique rider or unauthored Tome replacement is
invented to fill those boundaries.
