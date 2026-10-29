# New Horizons content development

## Authority and source map

Gameplay follows the [private game-design canon](https://github.com/thegandalf196/new-horizons-docs/blob/main/canon/game-design.md) and only
accepted amendments. The [amendment boundary](../NEW_HORIZONS_PENDING_CHANGES.md)
retains public compatibility contracts, not new design proposals; use the
private design library's `proposals/` and accepted-decision process.
The current phase is stabilization/integration. Governors/Castellans and Caravans
are deferred; Doctrine is retired. Provisional coefficients are tunable, not final
balance. Never turn obsolete summaries, artwork or saved legacy profiles into
current default rules.

`config/newHorizonsPerks.json` is the generated Skill/perk catalogue;
`config/newHorizonsHeroes.json` supplies captured Hero policies;
`Mods/new-horizons/Content/config/` contains curated entity overrides.
Use `tools/update-new-horizons-module.py` and its focused tests rather than
hand-editing generated metadata. Confirm actual tool options before invocation.
Canonical source identity and regeneration consistency are content gates, not
gameplay certification.

### Initial Leadership and generated recruitment

Fresh `config/newHorizonsCapabilities.json` captures restore all eighteen class
`classProfiles.base` values from before the 0.87.1 starting-Leadership increase.
`classProfiles.perLevel`, the global Leadership scale, creature requirements and
upgrade parity are unchanged. Existing hero captures retain their actual stored
profiles; do not rewrite them from the installed registry during load.

The independent neutral-army and recruitment-growth settings, including initial
recruitment supply, use the [army generation runtime contract](NewHorizonsNeutralArmies.md).
Regenerate module metadata after changing authored captures; a generated copy must
agree with its registry rather than silently overriding it with stale settings.

### Perk-offer policy2

`offerWeightPolicyVersion: 2` selects the compressed parent-first lottery.
`perkOfferNormalization.maximumPreferenceRatio` is a finite value at least1
(default2); `mathVersion: 1` identifies its saved numerical policy. Reuse the
complete captured class Skill row, including positive unlearned/capped Skills,
to determine normalization bounds. Do not derive bounds from eligible parents.
The implementation uses software-decimal normalization and a common2^32
integer quantum, followed by integer sampling from the local seeded generator.
This is a quantized distribution, not a claim of exact real arithmetic or of
unperformed cross-platform certification. Keep the transaction-local normalized
row reusable when a pending query is reopened, but reevaluate eligibility live.

Parent selection and uniform perk selection are separate. New games also capture
`perkProgressionPolicyVersion: 1`: one selected perk per Basic/Advanced/Expert
tier, in that order, with ordinary rank advancement alternating with perks.
Exceptional rank grants do not grant or skip perk tiers. Two eligible parents
produce distinct parents; a sole eligible parent supplies only one offer. Filter
exhausted or rank-blocked parents before sampling. Reuse `canAcquirePerk` for
authoritative offers, selection and consumer legality rather than UI/AI rules.

The progression marker is separate from weighting policy2 and the mathematical
ratio `R`. Marker absence preserves historical policy2 duplicate-tier selections,
its two-distinct-perk sole-parent fallback, and its existing captured queries.
Policy1 and absent-weighting-policy worlds retain their earlier behavior.
Never add the strict marker to an old world or normalize an invalid strict save
by dropping selections. `NEW_HORIZONS_STRICT_PERK_PROGRESSION` (1118) protects
strict captures; older formats must refuse them rather than omit the rule.
Zero-weight learned exceptions retain minimum parent weight and all existing
acquisition restrictions. Test offers, query replies and serialization together.

Principal consumers are entity handlers/bonus rules in `lib/`, validated
execution in `server/`, detached projections and target valuation in
`AI/BattleAI/`, adventure decisions in `AI/Nullkiller2/`, and presentation in
`client/`. UI submits requests rather than mutating authoritative state.
New config admission must reject malformed input through the real loader/schema,
with explicit old-save compatibility and unsupported-format behavior.

## Captured identities and effects

Fresh default Hero starts, map-prescribed starts and captured historical heroes
are distinct contexts. Specialty graphics/descriptions must follow the actually
resolved policy/effect, not overwrite legacy captures with a new default image.
Creature-line specialties follow their real target identity, including historical
internal naming such as Stone Golem's `core:ironGolem`.
Do not invent absent level scaling, Masterful variants or extra riders from an
art brief. Configuration, bonus producer and resolved presentation must agree.

The current perk catalogue preserves saved identities when effects are revised:
`offense.encirclement` is displayed as Spearhead, `armorer.shieldMaster` remains
Shield Master, `command.combinedArms` remains Combined Arms, and
`lightMagic.crusader` is displayed as Exalted Champion (all in `new-horizons:`).
Resolve names and effects from the captured rules context for historical heroes;
never relabel or reinterpret saved historical perks with current defaults.

The Chosen has its own `new-horizons:theChosen` spell identity. Current magic
configuration keeps historical Crusade inactive rather than reusing its ID.
The Chosen is a level-5 Light spell costing 25 Mana, granting native additional
attack, unlimited retaliations, flight and jousting to one eligible recipient
per original army. Its captured profile supplies a nominal 2–4-round duration;
with Expert Light's coefficient, raw Spell Power 104 and 207 reach 3 and 4 rounds.
Apply Exalted Champion's extra round after the nominal cap, alongside the
established duration extensions. Validate replacement, immunity, stasis and
Reality Warp against the one-recipient invariant, including dead stacks.
These are content/runtime contracts; source checks do not establish gameplay
acceptance.

Normal Spell Points and Buffer are separate pools, spent Buffer first. Capacity
changes are event-driven complete equipment/bonus transactions, not render/movement
polling; capacity growth does not refill Normal or alter Buffer. Inscribed eligible
combat spells do not require School proficiency to cast; acquisition gates remain.
Hat access is temporary without erasing independent learning. See
[Spell Point migration](../NEW_HORIZONS_SPELL_POINTS.md).

Where a captured spell roster applies, admit IDs before definition, school, icon
or castability lookup. Use one captured presentation context for filtering, counts,
tabs, sorting and paging; show-all bypasses possession, not roster admission.
Missing battle context must not fall back to the current world. Navigation follows
available saved schools, with stale selection returning to All and a valid page.
Keep excluded stored quick-slot preferences reserved and noncastable rather than
erasing or autofilling them; write canonical scoped keys only after valid explicit
selection. These contracts do not activate an inactive roster policy.

Use `SpellSchool::serializationKey()` when comparing module-qualified school identity; a bare `encode()` key cannot distinguish curated schools from third-party schools with the same local name. Preserve the supported legacy numeric decoding path separately.

Creature-category presentation comes from the actual stack's saved battle context
or its actual world callback, captured before a preview node is fabricated. Never
infer a category from legacy tier, upgrade graph or installed defaults; an absent
battle view does not fall through to world data. Recapture and clear optional
widgets when their subject changes, including commander rebuild paths.

### Quicksand Spell Power units

New Quicksand rows capture `rawSpellPowerPerPatch: 60`: ordinary patch count is
`min(5, 2 + floor(scaled displayed Spell Power / 60))`. School, Spellcraft and
eligible cast modifiers strengthen only the power term; Mire Shaper adds its
patch after the ordinary cap. The field is a positive integer exclusive to the
Quicksand row in magic rules version 3.

An absent field preserves the earlier count denominator of `60 × caster power
divisor`; a historical hero with divisor 10 therefore retains unmodified thresholds
600/1200/1800. Never add the field to a loaded historical profile or rewrite its
other captured values. Count generation and descriptions share the same unit
selection. The existing JSON representation retains the optional field without a
new binary state layout. Synthetic tests projecting current data into v1/v2 must
remove the v3-only field, rather than widening historical schemas.

## Focused verification

Use the narrow fixtures corresponding to the changed consumer, then the relevant
compound interaction. Useful maintained entry points include:

- `tools/tests/test_new_horizons_perk_data.py`,
  `test_new_horizons_content.py` and `test_new_horizons_hero_data.py`:
  source/catalogue/entity consistency.
- `test/hero/NewHorizonsHeroRulesTest.cpp`,
  `NewHorizonsPrimaryGrowthTest.cpp`, `NewHorizonsPerkRulesTest.cpp` and
  `NewHorizonsLeadershipTest.cpp`: captured rules and progression/capacity.
- `test/server/battles/NewHorizonsCommandTest.cpp`,
  `NewHorizonsDemonicGatingTest.cpp`, `NewHorizonsPlagueTest.cpp` and
  `NewHorizonsSpellLockControlTest.cpp`: real validated battle transitions.
- `test/battleAI/NewHorizonsGenieSupportAITest.cpp`: shared creature casts,
  target-pool legality, deterministic forecasting and actual AI requests.
- `test/nullkiller2/` and `NH_AI_EXCHANGES.md`: adventure planning, physical
  army conservation and carrier-aware Leadership.

A detached forecast must not consume live RNG or mutate live/parent/sibling state.
Exercise lifecycle before and after accepted actions, not only synthetic bonuses.
A zero-benefit legal spell pool need not cause AI to cast; positive-value controls
must demonstrate a genuine beneficial outcome instead of forcing bad decisions.
Tests cannot prove missing graphical cues, control routing or whole-match latency.

## Runtime art and ordinary acceptance

`Mods/new-horizons/NewHorizons.nhart` is the selected runtime-art container;
`assets/new-horizons/runtime-art-manifest.json` records portable provenance.
Do not add loose public exports or personal authoring paths. Pack tooling requires
explicit complete inputs and stages artifacts without modifying artist originals.
Preserve existing payload bytes, selection ledgers and recoverable baselines;
failure must not truncate a valid package or partially publish a checkout.

Role bindings come from actual `CSpell::registerIcons`, Hero/icon consumers,
schemas and UI scaling. Current purpose-made spell roles use book44, scroll/
scenario32 and effect/immunity30 where the consumer requires them; schema/template
canvas dimensions are not universal rendered sizes. Internal statuses such as
Arcane Breach and Lucidity use status roles, not fabricated castable book entries.
Native reduction must preserve alpha/contrast and central subjects; corner alpha
alone does not establish uncut edges or margins.

Selection, origin, approval and rendered verification are separate records.
Provisional drafted art is authorized for selection; Final needs approval evidence.
Selected Cabir/Magi/Guild/UI edits and composites belong in NHART, while unchanged
purchaser archives remain external. Follow [asset contracts](../NH_ART_ASSET_SPECIFICATIONS.md)
and [UI style](../NH_HOMM3_UI_STYLE_GUIDE.md).

Validate the committed pack without developer overlays, then ordinary control/
save/AI journeys. [Content acceptance](../NH_CONTENT_ACCEPTANCE.md),
[delivery](../NH_DELIVERY_PIPELINE.md), [integration gaps](../NH_INTEGRATION_GAPS.md)
and [regression guards](../NH_REGRESSION_GUARDS.md) define the remaining gates.
