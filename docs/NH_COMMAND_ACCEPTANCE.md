# First Orders/Doctrines — independent acceptance plan

Status: **PLANNED, not executed or accepted.** Tester owns this document and
normal-input journeys; no product code or art implementation. Authority:
`NEW_HORIZONS_DESIGN.md` and updated `NEW_HORIZONS_MVP.md`, read with the user's
external philosophy text. Fun first; numerical balance is not a gate. Correctness,
AI participation, feedback, crash prevention and save integrity are gates.
Repaired Windows archive publication continues independently; feature workers
need not wait for these tests. Existing architecture investigation stays closed.

## Current Tester handoff — post-reboot local-first resumption

Read current AGENTS, design/MVP, worker plan and Runtime/Frontend/Build handoffs.
Shared dirty work is preserved; no product edits, build, commit or GUI launch.
Build owns the compile/test lane and fresh candidate freeze. Invalid GitHub API
credentials do not block these local checks; no cloud workflow was requested.

- Added `tools/tests/nh-new-art-audit.py`. Actual command
  `python3 tools/tests/nh-new-art-audit.py --reproduce --report build/new-horizons-linux/testing/commands-static/art-audit.json`
  exited0:88 geometry SVGs,88 RGBA PNGs,20 animation JSONs; semantic state order,
  dimensions, six exact transparent header paddings and isolated byte-identical
  regeneration passed. No purchaser input, product-output overwrite or GUI.
- Synthetic invalid-boot guard check exited1 as expected before X11 loading/display
  open; local `testing/commands-static/guard-rejection.json` records the refusal.
  No pre-reboot guard can authorize a new graphical run.
- Independently read `commands-native-resume.xml`:19 tests,2 failures; all nine
  budget combinations passed. `commands-baseline-resume.xml`:67 tests,3 failures
  in town-garrison binary persistence. Both actual exit files contain1. Command
  failures were full-game round-trip identifier resolution and settings lookup.
  These are failed integrated gates, not a feature-acceptance checkpoint.
- Corrected combined `commands-version-fixed.xml` subsequently reports88 tests,
  one failure and one skip:86 passes. All19 command/settings/full-game-state
  cases and the baseline regression suite pass; bookless AI command passes. The
  strong competing-spell AI case instead selected an Order, spent no mana and
  cast no spell. Its original Magic Arrow oracle assumed1000 Spell Power despite
  the effective99 cap; this failure does not establish a product AI defect.
  Runtime's revised Implosion fixture asserts effective power, cast legality and
  substantial nonlethal damage while retaining real evaluator/server assertions.
  Its subsequent pass is recorded below; the earlier failed files are retained,
  not relabeled.
- Latest independently inspected `commands-integration-resume.xml` and `.exit`:
  **92 tests,91 passes,1 expected export skip,0 failures, exit0**. Both real AI
  choice/server cases and all four persistence/recipient cases pass, including
  full BattleStart reconstruction/continued play on an independent replica.
  All19 command/settings cases and66 baseline regressions pass. This is native
  evidence only; final UI rebuild, authored export and immutable GUI freeze are
  still pending. No GUI GO has been given.
- Critical version regression: HERO_COMMANDS placed after MINIMAL's enum alias
  made CURRENT894 and disabled later feature gates. Runtime moved it before
  aliases and added a monotonic static assertion. Corrected persistence/baseline
  reruns and the revised AI spell gate are now green in the latest combined XML
  above; no final GUI candidate is frozen. Never reuse
  the previous feature build's save bytes or treat that candidate as valid.
- Native AI coverage now proves a legal bookless command and a competing spell
  through the real evaluator/server, plus spent-budget exclusion. It does not
  substitute for observing AI hero actions through the actual GUI journey.
- Reported two concrete static gaps: managed launcher omitted the new module;
  Aggressive UI omitted its ranged benefit. Current source now mounts the module
  and describes melee plus ranged. Launcher test exit0 is recorded; real installed
  activation/rendering must still pass on the newly frozen candidate.
- Independently ran `python3 tools/update-new-horizons-module.py --check`, exit0.
  Generated inline settings now match canonical rules; this removes the earlier
  named-file module-scope mistake without cross-scope lookup fallback. Actual
  installed activation still needs GUI/native evidence from the corrected build.
- New numeric-label fit risk reported to Frontend using offline installed font
  metrics: SMALFONT line height16, `Physical damage taken +10%` width166, exceeding
  Aggressive's163×47 label. Three unwrapped lines already need48px; wrapping makes
  four/64px. Local `testing/commands-static/effect-label-font-metrics.json` records
  the measurement. Frontend corrected source to `Physical taken` and163×64
  ending at y458 before the footer at463; actual font selection/rendering still
  needs the frozen GUI check.
- Pre-reboot temporary ordinary-run profile is absent; do not claim its saved-game
  compatibility route can be rerun unchanged. Persisted gap1 pre/post saves remain
  under ignored testing storage, preserved. Use fresh authored fixtures and clones.

**Next executable task:** review corrected activation/tooltips and Runtime's native
results/hero-versus-hero fixture; then run only Build's fresh frozen integrated
candidate under a new private guard and explicit quiet lease. Ordinary adventure
save/reload and new-battle NONE are distinct from active-battle packet persistence.
No stale original-mode preview is evidence of new-command gameplay.

## Full owned redesign acceptance (not limited to this first slice)

No row below is accepted merely because the command increment passes. Preserve
this scope across candidate freezes; numerical balance is never the gate.

| Increment | Required independent evidence | Current acceptance |
|---|---|---|
| Commands/Doctrines | Authoritative legality/shared action budget, real effects, situational AI, human UI, expiry/replacement, versioned save/state continuation; extend coverage to each later implemented command/Doctrine. | First-slice static checks and narrow native passes only; integrated persistence/GUI gate pending. |
| Six schools and spells | Exactly the intended registry, real school filtering/casts/costs/target legality/effects, AI valuation/targeting and expiry; school/page/bookmark UI, legacy-versus-new content identity, actual spell save continuation. New spell prerequisites (forced movement, sharing, revival timing, etc.) require executable tests, not descriptions alone. Unresolved faction/Blind classification stays explicit. | Original-art inventory and new-art static checks only; no six-school gameplay acceptance. |
| Growth and secondary attributes | Declared class profiles sum to ten, support both authorized profile patterns, independent skill bonus rolls (including zero/multiple outcomes), coherent starting scale and coefficient formulas, Knowledge/mana, movement/morale/luck/leadership/siege derivations. Native deterministic oracles plus human/AI level-up/save continuation; no creature deletion at capacity changes. | Pending implemented candidate/data; no inferred pass from original rules. |
| Masteries and hero UI | Every implemented Expert-skill mastery has a real legal choice and effect, distinct alternatives, persistence, AI selection/use where relevant, readable growth/derived-attribute/class/military/skill/mastery display and normal input. No inert icon accepted as a mastery. | Pending implementation/candidate; concept images are direction, not rights-cleared outputs. |
| Creature tiers | Core/Elite/Champion classification matches the declared roster; proposed Pixie/Sprite independence, five elementals and Phoenix remain functional recruitable armies where implemented. Check upgrade/production/transfer/AI/save behavior, UI and original-mode compatibility. | Pending implemented data/candidate. |
| Distribution and assets | Fresh Linux installed-content journey, exact candidate/source/content hashes; Windows actual archive/import/resource/source/license/provenance audit. Genuine Windows runtime gate only on authorized native Windows, never Wine/compile inference. Editable original art, truthful placeholders and no purchaser/private assets in distributed payload. | Linux command candidate pending. Prior Windows archive failed dav1d notice gate; no corrected archive accepted. |

Use this existing handoff for material results and the next executable task; do
not create competing product implementations or declare the full goal complete
while any required row lacks matching integrated evidence.

## Freeze and evidence contract

Build supplies source/content identity, immutable client/library/resource hashes,
feature activation, declared provisional rules, native-test evidence and a quiet
GUI lease. Only Tester runs private guarded Xvfb/XTest, bounded and using fresh
profiles/clones; no host display/input, original executable or asset writes.
Preserve original-mode saves, previews and packaging payloads. No feature runtime
claim before the new candidate is frozen. An existing frozen spellbook may be
inspected separately only after Build confirms identity and quiet lease.

Record actions, visible before/after state, round/side, hero mana/action availability,
relevant stack state and bounded screenshots locally. Independently match actual
state changes with requests where evidence permits; ACK alone proves neither
acceptance nor visible effects. Native deterministic tests complement GUI results.
Failures go directly to the owner for a corrected freeze and focused retest.

## Prepared authored-fixture route

Requested Runtime-owned opt-in TinyH3M exporter for two ordinary SOD36×36 maps,
`NHCommandsBooklessAI` and `NHCommandsSpellAI`: human red hero versus blue AI
hero, with explicit armies, primary attributes, equipment and learned spells.
Towns stay away from the immediate field engagement. Existing gap1 neutral-town
maps cannot establish AI hero actions and are not substitutes.

Runtime's exporter source now declares: towns at(8,10)/(30,30); red Orrin0 at
anchor(17,10), A2/D2/P3/K10, book with Haste/Bloodlust/Magic Arrow; blue Edric2 at
(20,10), A2/D2/P3 or99/K10, bookless or book with Magic Arrow/Implosion. Both have
600 Dendroid Guards,80 Grand Elves and a Ballista; mana100, basic Pathfinding,
zero XP. Choose **red human, blue AI, Gold starting bonus for both** in ordinary
setup. Native export will report visitable coordinates (not interchangeable with
anchors). The current rules intentionally retain original Knowledge×10 mana for
this first increment; the later new-scale mana/growth gate remains pending.

For unchanged A2/D2, independent command display oracles are Charge melee+21%,
Hold physical taken−16%, Advance base speed+26%, Aggressive melee/ranged+16% and
physical taken+10%, Defensive physical taken−16% and base speed−10%. Confirm the
frozen settings/hero values before using these oracles; do not equate percent
bonuses with integer movement range or randomized final damage. Exporter's native
map override proves command-fixture semantics, not actual module activation in
ordinary GUI setup. The latter remains a separate installed-candidate gate.

Native export must prove parser/real-init hero ownership, army/equipment/spells
and disk gzip integrity. Tester owns independent file audit and GUI route, not
Runtime's C++ fixture/source. `tools/tests/nh-command-fixture-audit.py --self-test`
exited0 on a header-only control and rejected truncated, concatenated and invalid
gzip. This is tool validation, **not an actual exported map pass**. After export,
run it on both actual map files with a local manifest destination; it bounds
input/decompression, checks SOD/name/dimensions and hashes both compressed/raw data.
Native semantic and graphical gates remain additional requirements.

Bounded normal-input route after Build freeze/quiet GO:

1. Fresh private profile and narrow fixture asset mount; New Game selects authored
   map, red human/blue AI. Confirm both actual heroes and declared armies. Save
   before engagement, quit/reload and compare state/rules identity.
2. Order journey: enter the hero-versus-hero battle. Open/close chooser with X/Esc;
   open spellbook and cancel a target selection where supported, then confirm
   action still available. Across fresh rounds issue Charge, Hold and Advance;
   reopen for actual Order/spent state, mana unchanged, and effect readback.
   Finish each unit turn normally; observe AI hero command and continued army
   actions. New round clears the Order and restores the hero budget.
3. Doctrine/spell journey from a normal reload of the prebattle save: choose
   Aggressive, inspect three declared effects and penalty, then next round confirm
   it persists. Cast a real legal spell; confirm mana cost and all command choices
   unavailable that round. Later switch Defensive and verify replacement, no
   stacking, same-Doctrine disabled and continued AI play. Capture compact label
   fit and active-state feedback. Native tests cover malformed requests UI cannot
   legitimately submit.
4. Use the spell-capable AI fixture for an actual competing AI spell context; do
   not substitute a neutral battle or infer AI choice from source. Survive/finish
   or legitimately retreat, save/quit/reload on adventure, then start another
   battle and verify Doctrine NONE. No unsupported mid-battle save or state injection.

Fixture survivability is an execution precondition, not a balance target. Split
journeys/reload legitimate prebattle saves rather than forcing debug state when
one fight cannot last long enough. Record every unexercised subcase honestly.

## First-slice acceptance matrix

| Area | Independent checks |
|---|---|
| Reachability | Human accesses Charge, Hold the Line, Advance, Aggressive and Defensive through ordinary combat UI. Both Might and Magic heroes have core access; no faction-exclusive list or mana-like Order currency. Unsupported states explain rejection. |
| Shared action | In separate fresh rounds, successfully cast a spell, issue an Order, and change Doctrine. After each, all three hero-action routes reject further actions for that same hero/round. Cover all nine first/second-kind combinations natively; exercise all three first-action kinds graphically. Opposing hero retains its own action. |
| No false spending | Open/close menu, Escape/cancel targeting, invalid target and denied request preserve hero action, mana and existing effects. Follow cancellation/rejection with a valid action in the same round. Duplicate/stale, wrong-side, no-hero, tactics-phase and already-used requests reject authoritatively without partial mutation. |
| Round boundary | Budget resets exactly once at the declared new-round boundary, not on a creature's next turn, Wait, retaliation or morale extra action. Creature abilities/automatic casts do not accidentally spend or reset the hero budget. |
| Charge | Legal initiating melee actually uses declared tunable coefficients. Check excluded attack kinds, friendly/enemy scope and retaliation separately; inspect state plus resulting combat, not an icon alone. No unintended raw expanded hero rating is added to unchanged creature damage math. |
| Hold the Line | First slice has no stationary-history prerequisite: moving before issue does not disqualify an otherwise eligible stack. Verify protection for eligible recipients, including movement after issue, and removal at nextRound. Do not enforce the earlier stationary-only proposal. |
| Advance | Legal movement range changes, reachable movement executes, and range returns at expiry. Distinguish movement from initiative, flying, obstacles and occupied hex legality; do not silently permit invalid destinations. |
| Doctrine | One active Doctrine persists across rounds of this battle without automatic re-spending. Switching Aggressive/Defensive replaces rather than stacks effects and consumes the shared action. Same-Doctrine reselection and NONE/removal reject without cost or mutation. Bonuses and declared penalties actually operate; document Wait/retaliation/movement behavior. |
| Duration/interactions | Order expiry, Doctrine persistence and simultaneous active Doctrine plus a later-round Order match declared rules. No duplicate application on round transition, stack death or serialization. Spell effects remain functional; no unrelated combat behavior is silently removed. |
| AI mandatory | Deterministic contexts demonstrate legal spell-versus-Order/Doctrine evaluation and execution, not an unconditional fixed Order. Cover useful spell, useful command, no legal target and exhausted action. GUI observe an AI-controlled hero using the system and continuing its army turn without stalls/double spending. No requirement to prove numerical optimality or faction balance. |
| Save/reload | Native round-trip active-battle Doctrine, Order duration and hero-action budget, then continue to the next round without duplicate effects or refreshed spent action. GUI save/quit/reload/continue at supported boundaries; do not claim mid-battle graphical saving if unavailable. A new battle starts with Doctrine NONE, including after an adventure save/reload; no hero-save Doctrine preference is implemented. |
| Identity/compatibility | New content identity is explicit; original-mode saves are not silently reinterpreted. Use clones to check supported load or clear compatibility refusal. No deletion of armies or loss of unrelated save data. |
| Usability/portability | Labels, selected/disabled/targeting states, costs/action consumption and active-effect/expiry feedback agree with actual rules. Missing icons, clipped controls, misleading success or crashes are defects. Record Linux GUI evidence separately from Windows compile/package or actual Windows gameplay. |

Native malformed/duplicate request cases use Runtime's authoritative test fixtures,
not frontend state mutation. GUI uses only normal input. Do not expose developer
controls merely to make acceptance easier. Deterministic damage cases should use
known bounds/fixed fixtures to distinguish an effect from random damage rolls.

## Spellbook artwork and new asset review with Frontend

Frontend provides the actual installed-resource inventory and VCMI lookup sites.
Tester independently reviews and, if needed/leased, captures the existing frozen
spellbook through normal input. Inventory must include:

- Exact resource/key and frame/group names, archive provenance and lookup paths.
- Actual canvas and individual-frame dimensions, offsets and placement/scaling;
  palette/transparency/mask behavior and normal/selected/pressed/disabled states.
- Existing school tabs, emblems and supporting backgrounds as actually installed,
  not assumed names/dimensions or concept-image measurements.
- Original purchaser art stays external/read-only, referenced rather than copied
  into Git. Extracted art/screenshots and source concept images stay local.
- New six-school and command/Doctrine assets require editable originals, generated
  outputs, explicit author/license/provenance, and a reproducible export description.
  Check required UI states and output dimensions against real placement. Procedural
  placeholders are labeled honestly; supplied concepts are not rights-cleared art.

No six-school feature acceptance is implied by first-command acceptance or by an
art inventory. Real effects, classification/casts and AI support remain separate
implementation gates for later slices.

### Offline installed-art evidence reviewed

Tester reviewed Frontend's local parser, complete JSON frame metadata and full
contact-sheet pixels under ignored `research/spellbook-source/` in the Linux build
root. This is offline decoding evidence, **not a rendered-game screenshot**. No
GUI lease or original executable was used; no decoded purchaser art enters Git.

| External resource | Observed dimensions/state semantics |
|---|---|
|`H3sprite.lod:Schools.def`|Type0x47, group0, format1 frames0–3. Each160×96 canvas,160×68 crop at(0,28). `SchoolsA/F/W/E.pcx`: Air blowing cloud face; Fire golden sun face/rays; Water blue breaking wave; Earth tree/mountain. Fire's sun motif is pixel-observed, not inferred from its name.|
|`H3sprite.lod:SPELTAB.def`|Type0x47, group0, format1 frames0–4. Each83×294 full strip; frames select Air/Fire/Water/Earth/Any, **not four button interaction states**. Crops/margins respectively83×289@(0,0),83×283@(0,6),80×283@(0,6),83×283@(0,6),82×287@(1,6).|
|`H3bitmap.lod:SpelBack.pcx`|Indexed620×595 background.|
|`H3bitmap.lod:SpelTrnL.pcx`, `SpelTrnR.pcx`|Indexed page-turn artwork33×39 and29×32 respectively.|

DEF palettes have256 RGB entries; index0 is cyan(0,255,255). The inspected SDL2
and SDL3 loaders set colorkey0. The offline decoder treats other indices as
opaque; this does not independently prove every rendering/blending operation.
`client/windows/CSpellWindow.cpp` places the school header at(117+offL,74+offT)
and legacy strip at(524+offR,88), relative to the book. Existing custom-school
support uses a header plus two bookmark frames: selected0, unselected1. Header
art is shown only on the first school page so it does not obscure later spells.
Reuse this existing book path; no parallel spellbook is required by the inventory.
Observed motifs do not silently assign original art to the new school registry.

Later generic layout correction reviewed independently: above five small-book
custom schools or six large-book schools, bookmarks shrink within the existing
reserved strip and hit rectangles follow rendered positions. Offline arithmetic
checks for0–10 small and0–12 large school counts pass24 cases: positive dimensions,
no overlap/overflow and unchanged ordinary-capacity positions. Six small bookmarks
are68×51; six large remain80×60. `CAnimImage`'s Rect overload preserves the scaled
bounds used by hit testing. Evidence: local
`testing/commands-static/school-layout-independent.json`. This is not six-school
registration, actual click/render acceptance or spell migration.

### First new-art/UI static review

Tester read `assets/new-horizons/README.md`, the full generator, chooser source and
battle-entry changes, and inspected the new-art contact sheet. Provenance declares
original geometric CC0 artwork, with no original/concept inputs. The generator
reads no purchaser artwork; SVG elements are editable geometry, not embedded or
externally linked images. Flat-vector/provisional status is accurately labeled.

Independent checks passed:88 SVGs,88 matching-dimension RGBA PNGs and20 animation
JSONs with contiguous group0 frames and existing image references. Regenerating
from a copied generator in isolated ignored storage reproduced all196 files byte
for byte; project outputs were not overwritten. Evidence is local
`testing/new-art-static-c1txx8pc/reproduction-checks.json` in the Linux build root.

One minor mismatch was sent to Frontend: all six header PNGs initially had alpha
up to3/255 in nominal transparent top28 rows from Lanczos filtering. Frontend
clamped alpha after filtering. Tester verified only those six PNGs changed, the
other190 outputs stayed identical, and top28 alpha is now exactly zero. The
post-reboot durable art audit also passes; this static defect is **resolved**.

Source inspection confirms a640×520 chooser reached through the48×36 battle
entry, existing spellbook reuse, cancellation via close, callback-based eligibility
and authoritative command requests rather than frontend budget/state mutation.
Disabled/readback behavior is wired but **not graphically accepted**. Compilation,
actual layout/input, effect lifecycle and AI journeys await the frozen candidate.
Six-school artwork remains preparation, not a completed casting migration.

Subsequent hero-display art audit passed independently:15 motifs at32×32 and64×64,
118 SVGs/118 PNGs/20 animation JSONs in total. The full256-file isolated regeneration
matches byte-for-byte; all196 previous outputs are unchanged. Tester reviewed the
new generator geometry, contact pixels and README's AI-assisted CC0 provenance;
no purchaser/concept inputs are read. `tools/tests/nh-new-art-audit.py --reproduce`
exited0 and now checks the15 hero glyph names/sizes too; local evidence is
`testing/commands-static/hero-art-audit.json`. These are display assets only—not
accepted hero UI, growth rules, mastery mechanics or creature reclassification.

## Declared provisional first-slice rules

These are explicit implementation rules, not final balance or completion of the
broader philosophy:

- Doctrine persists across rounds **within this battle only**, on the eligible
  stacks present at issue. Late arrivals/clones do not retroactively inherit it;
  switching Doctrine applies to current eligible stacks. Serialized active battle
  state retains it; new battles start NONE. No cross-battle/hero-save Doctrine
  preference in this increment.
- Same-Doctrine reselection is invalid and costs nothing. NONE/removal is
  unsupported and rejects without spending. Switching Aggressive/Defensive
  consumes the common hero action.
- Orders affect all living own ordinary, non-war-machine stacks present at issue
  and expire at nextRound. Test own/enemy, living/dead and ordinary/war-machine
  recipient boundaries; stacks introduced afterward must not inherit the Order.
- Hold the Line has no stationary-history prerequisite in this slice. Charge has
  no hidden charge-distance or flanking rule. Test stated effects, not inferred
  restrictions from names or earlier proposals.
- Coefficients/settings are forthcoming from Build; all nine shared-action-budget
  combinations are planned natively. No GUI before Build's freeze and lease.

## Decisions still requiring explicit declaration

Do not silently resolve philosophy conflicts in test expectations:

- Growth supports both universal4/3/2/1 and class-specific ten-point profiles;
  class tables remain provisional. No new-scale ratings with old raw formulas.
- Necropolis Shadow/Chaos versus Shadow/Sorcery, missing Fortress minor table cell,
  and Blind's prose/list classification remain visible school-design questions.
- Current canonical settings declare Charge melee20+0.5×Attack; Hold reduction
  15+0.5×Defense; Advance speed25+0.25×Attack; Aggressive melee/ranged15+0.25×Attack
  with physical damage taken+10%; Defensive reduction15+0.25×Defense and base
  speed−10%. These are percentages, not raw creature-stat additions. Current
  implementation clamps coefficients to[−90,200], rounds with lround, and caps
  damage reduction at90. Freeze exact settings before treating these as GUI oracles.
  Command-rank specialization remains later breadth.
- Late-unit Doctrine coverage is now explicitly current-stacks-at-issue only,
  as recorded above. New persistence test source covers late arrivals then switch,
  war-machine/enemy exclusions, no ordinary recipients, and a full BattleStart
  round-trip into an independent game/army graph with continued legal play.
  All four tests now pass in the independently inspected92-case combined XML.
  GUI persistence and actual clone behavior must not be inferred from a synthetic
  addStack alone. Frontend's current caption/footer/help explicitly describes
  current troops, no late-summon/clone inheritance and switching for new arrivals;
  this wording still needs the fresh frozen render/help-fit check.
- Ordinary `CGameState` saves exclude current battles. Active-battle persistence
  must be proved with BattleStart/replay/full native packet round-trips, not an
  invented mid-battle graphical save. GUI adventure save/reload and starting a
  new battle with NONE are separate required checks.
- Leadership capacity must never silently discard creatures. Advanced spell/mastery
  mechanics are unfinished breadth, not inert icons counted as completed features.

These are requests for explicit provisional behavior, not a numerical balancing
hold. Record the candidate's declared rules and unresolved breadth in results.
