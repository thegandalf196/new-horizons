# New Horizons content development

## Authority and source map

Gameplay follows [New Horizons.md](../design-sources/New%20Horizons.md) and only
unintegrated amendments in [Pending Changes](../NEW_HORIZONS_PENDING_CHANGES.md).
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

Normal Spell Points and Buffer are separate pools, spent Buffer first. Capacity
changes are event-driven complete equipment/bonus transactions, not render/movement
polling; capacity growth does not refill Normal or alter Buffer. Inscribed eligible
combat spells do not require School proficiency to cast; acquisition gates remain.
Hat access is temporary without erasing independent learning. See
[Spell Point migration](../NEW_HORIZONS_SPELL_POINTS.md).

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
