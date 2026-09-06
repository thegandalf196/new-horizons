# First Orders/Doctrines — independent acceptance plan

Status: **PLANNED, not executed or accepted.** Tester owns this document and
normal-input journeys; no product code or art implementation. Authority:
`NEW_HORIZONS_DESIGN.md` and updated `NEW_HORIZONS_MVP.md`, read with the user's
external philosophy text. Fun first; numerical balance is not a gate. Correctness,
AI participation, feedback, crash prevention and save integrity are gates.
Repaired Windows archive publication continues independently; feature workers
need not wait for these tests. Existing architecture investigation stays closed.

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

## Declared provisional first-slice rules

These are explicit implementation rules, not final balance or completion of the
broader philosophy:

- Doctrine persists across rounds **within this battle only**. Serialized active
  battle state retains it; new battles start NONE. No cross-battle/hero-save
  Doctrine preference in this increment.
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
- Coefficients, rounding/caps and Doctrine penalties still need declared settings;
  recipient coverage, nextRound expiry and Hold the Line history are now specified
  above. Command-rank specialization remains later breadth.
- Supported graphical save boundaries still need confirmation. Battle-state
  serialization is required, but does not imply an available mid-battle save UI.
- Leadership capacity must never silently discard creatures. Advanced spell/mastery
  mechanics are unfinished breadth, not inert icons counted as completed features.

These are requests for explicit provisional behavior, not a numerical balancing
hold. Record the candidate's declared rules and unresolved breadth in results.
