# New Horizons redesign — implementation contract

## Version1.0 scope — latest user confirmation

Read [NH_VERSION_1_0_SCOPE.md](NH_VERSION_1_0_SCOPE.md) for the user-confirmed
release boundary: primary attributes, secondary skills with associated abilities
(replacing old masteries), six magic schools, Core/Elite/Champion creature
organization, Commands and Castellans/governors. The single-expeditionary-Hero
restriction is NOT implicitly included. Older conflicting passages below remain
historical pending reconciled implementation/migration; do not use them to undo
this direction or infer unspecified castellan/Siege rules.

## Canonical gameplay source

The user supplied the current gameplay specification as
[New Horizons.docx](design-sources/New%20Horizons.docx). It is the newest
authority for gameplay scope and mechanics. When its roadmap or summary tables
conflict with a later detailed system section, the detailed section controls.
Read [NEW_HORIZONS_OVERRIDES.md](NEW_HORIZONS_OVERRIDES.md) immediately after
the source document. Accepted entries there are later, narrowly scoped user
instructions and override only the exact scripture rules they name; all other
scripture rules remain authoritative.
This currently means that Version 1.0 uses the ten-perk, choose-at-most-three
Skill model; Governors and Caravans remain future work; the detailed Sorcery
roster's Magic Arrow with Overcharge controls over the earlier Magic Missile
summary entry; and the superseded Doctrine experiment is not part of New
Horizons. Orders remain in scope. Existing Doctrine code must not be exposed as
active New Horizons gameplay; retain only narrowly required legacy-save reading
until that compatibility path can be retired safely.

The MVP and this contract remain authoritative where the source is silent:
server-side command validation, working AI, save/version compatibility,
single-process architecture, licensing, asset provenance, and validation gates.

## Authority and intent

The user supplied their gameplay document and explicitly authorized full-scale
implementation. This is an intentional gameplay redesign on VCMI, not a requirement
to preserve every original rule. The stable original-content MVP is the foundation.
The exact source is retained under `docs/design-sources/` at the user's direction.
Its embedded illustration remains provenance-restricted reference material and
must not be exported as runtime art without a separate rights review.

FUN FIRST. Numerical balance is deferred until much later. Do not gate a working
feature on balanced numbers, multiplayer fairness or exhaustive comparative tuning.
Provisional tunable numbers are acceptable. Correctness, AI support, understandable
feedback, crash prevention and save integrity are not optional balance work.
Single-player is the primary product. Keep existing VCMI execution architecture;
the prior architectural experiment is closed. Deferred smoothness research is in
[NH_INVESTIGATIONS.md](NH_INVESTIGATIONS.md).

## Full authorized scope

- Orders complement spells. Every hero can issue Orders, and an Order competes
  with spellcasting for the shared hero action. Orders have no mana-like currency.
  Might Command and Magic Wisdom specialization distinguish effectiveness/access
  to advanced development, not faction-exclusive Order lists.
- Hero Attack/Defense become inputs to command-specific formulas, rather than
  blindly adding expanded ratings to all creature statistics. Do not apply new
  rating magnitudes to unchanged original damage math.
- Six magic schools: Light, Nature, Sorcery, Havoc, Shadow, Chaos. Towns receive
  distinct major/minor identities. Existing and new spells need real effects,
  AI valuation/targeting, costs, school UI and descriptions.
- Deterministic primary growth totaling ten points per level, with each hero
  class granting its fixed four-attribute vector on every level. There are no
  skill-based extra rolls or level-10 transition. Starting ratings use
  20/15/10/5 arranged by priority as the initial proposal. Knowledge directly supplies base
  mana; all relevant formulas/tooltips must use the new scale coherently.
- Secondary attributes include mana, leadership capacity, movement, morale, luck
  and siege capability. Skills modify capabilities; leadership capacity and siege
  are not additional automatically rolled primary stats.
- Expert skills gain a further level-up choice of one of three masteries. Prefer
  qualitatively different playstyles; the document's percentages are examples,
  not a reason to block initial implementation with numerical tuning.
- Core/Elite/Champion creature categorization. Conflux proposal: Pixies/Sprites
  as separate Core choices, five elementals as Elite, Phoenix as Champion.
  Preserve actual creature/army functionality while category/UI changes land.
- Hero screen expresses primary growth, derived attributes, skills/masteries,
  class and military identity clearly. Concept images are visual direction,
  not finished sprite sheets or authoritative numerical rules.

## Curated usability additions

User authorized visible quick-save/load buttons and creature ability/status icons
(including Undead), matching the familiar conveniences supplied by VCMI Extras.
Follow [NH_USER_FEEDBACK.md](NH_USER_FEEDBACK.md) for exact scope, ownership,
acceptance and unresolved third-party art permissions. These are presentation
additions, not changes to working F8/F9 or creature rules. Integrate directly into
the curated edition; use independently authored artwork if Extras redistribution
rights cannot be established. Do not delay useful equivalent implementation on
optional unlicensed art, or claim that public availability grants reuse rights.

## Asset policy

Inspect actual installed Heroes III spellbook/school artwork and VCMI lookups before
asserting what exists or choosing replacement dimensions. Record external resource
names, sizes, palette/transparency, frame/state needs and placement. Do not launch
Heroes3.exe for this inventory. Purchaser assets remain external/read-only.

Reuse original assets by reference where suitable; never commit extracted original
art or assume another Heroes game's art is redistributable. New school emblems,
Order/Doctrine icons, hero-attribute/mastery indicators and needed UI states require
original editable source art plus runtime outputs, with explicit provenance/license.
Create functional bespoke assets now; mark placeholders honestly. Do not claim a
procedural icon is finished commissioned illustration or invent an art generator.
No assets from the supplied concept images may be treated as rights-cleared without
provenance review. Scope later new creature animation sets explicitly.

## Unresolved choices — keep visible, do not silently decide

- Earlier universal 4/3/2/1 growth versus later class-specific ten-point patterns:
  implement a data-driven profile mechanism supporting both. Later class examples
  are provisional inputs, not a complete final class table.
- Prose Necropolis Shadow/Chaos versus table Shadow/Sorcery; Fortress minor-school
  cell missing despite prose Nature/Shadow. Keep the conflict flagged; author the
  six-school registry/UI without making an irreversible faction assignment.
- Early Orders grant raw hero stats; later scale discussion explicitly replaces
  these with coefficients. Use tunable coefficients for the first playable slice.
- Command ranks, target coverage and Leadership capacity need
  provisional declared rules. Army capacity must never silently delete creatures.
- New spells/masteries imply additional mechanics (forced movement, flanking,
  damage sharing, resurrection timing). Implement their prerequisites explicitly;
  a tooltip or inert icon is not a completed feature.

## Delivery plan — full scope, incremental playable commits

1. Combat command foundation: implement the eight canonical Orders—Charge,
   Focus Fire, Riposte, Hold the Line, Brace, Protect, Flank and Second Wind—with
   a common spell/Order hero-action budget, clear UI and original icons. Human and
   AI must both choose/execute legally. No default AI auto-picking a fixed Order
   irrespective of available spells. Provide deterministic tests, active effect
   feedback and save/load behavior. Unsupported combinations fail visibly rather
   than pretending success. This is first delivery, not the full project scope.
2. Add the six-school definitions/artwork and migrate spellbook/classification and
   casts as a coherent ruleset. Implement required new spell effects and AI logic
   in families, rather than displaying a fictional complete spell list.
3. Integrate primary profiles/scaled formulas and secondary attributes, followed
   by mastery progression and hero-screen implementation. Preserve separate content
   identity/version checks so old saves are not silently interpreted as new rules.
4. Expand remaining Orders, masteries, spell roster and creature tiers.
   Test real journeys across implemented systems; replace provisional assets where
   required. Balance tuning is later, not a reason to stop a functioning feature.

A fixed internal content module is compatible with the curated edition: players
must not have to manage a mod collection. Preserve original-mode saves/candidates
and make ruleset activation explicit to serialization/compatibility checks. Keep
Windows/Linux source portability, ordinary in-process runtime, GPL attribution,
required source/dependency notices and asset exclusion in distributions.

## Active ownership

User has additionally commissioned a fifth worker, **Artist (Sorcery)**, under
[NH_ARTIST_SORCERY.md](NH_ARTIST_SORCERY.md). It owns isolated original-art drafts
and exports only; Frontend/Build retain live asset/UI integration, Content retains
graphical acceptance. This explicitly activates an art role, not the optional
packaging worker. The full Sorcery family needs user visual approval before it
is called definitive; preserve references as purchaser-only material.

Use [NH_WORKER_PLAN.md](NH_WORKER_PLAN.md) for each active worker's objective,
acceptance criteria, next checkpoint and existing durable handoff. Follow
[NH_DELIVERY_PIPELINE.md](NH_DELIVERY_PIPELINE.md) for separate feature/release
lanes and early cross-platform/testing gates; [NH_AGENT_START.md](NH_AGENT_START.md)
contains role-specific `/goal` startup prompts. Markdown alone does not activate
a session's Goal mode; the harness contract is authoritative.

- Runtime: authoritative combat commands and hero-action gating,
  packets/validation/save integration in lib/server plus AI/ logic and native tests.
  New common API and data schema must be sent to Frontend promptly. No UI edits.
- Frontend: client/clientsdl UI and input, actual installed-resource inspection,
  original editable icon art under assets/new-horizons/ and generated game artwork
  under Mods/new-horizons/Images/. Coordinate data identifiers with Runtime.
  No AI/lib/server edits or unreviewed changes to the Windows launch helper.
- Build/Integrator: CMake, required content registration/config/Mods-new-horizons
  metadata and non-Image data, serialized builds, scoped commits/pushes and ongoing
  repaired Windows preview publication. Preserve frozen accepted candidates.
- Content/Tester: independent asset provenance/packaging review, normal-input
  graphical execution and tests/results; no product source/art implementation.
  Only this worker launches private guarded Xvfb/XTest game journeys. Never use
  host desktop input or launch the original binary. No competing GUI sessions.

Workers implement now, not proposals awaiting another approval. Coordinate exact
files and packet/schema/API ownership before crossing boundaries. Build remains
sole integrator; Dispatcher edits these design/investigation docs only while it
integrates. Every feature checkpoint reports working behavior, AI integration,
source/build identity, validation and unfinished breadth. UI screenshots and real
journeys are necessary for usability claims; native tests are complementary.
