/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CastleSpecialtyFixture.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/SpellEffectPreview.h"
#include "../../../lib/spells/NewHorizonsShadowMagnitudes.h"
#include "../../../lib/spells/effects/Effect.h"
#include <limits>

namespace
{
struct LifeDrainPreviewCase
{
	const char * hero;
	int level;
	bool historical;
	int bonusPercent;
};
class LifeDrainSpecialtyPreviewTest : public CastleSpecialtyTest,
	public ::testing::WithParamInterface<LifeDrainPreviewCase>
{
};
}

TEST_P(LifeDrainSpecialtyPreviewTest, CurrentCasterBookAndNativeTargetPreviewMatchAcceptedDamage)
{
	const auto test = GetParam();
	if(test.historical)
		stripped = {"earthboundDrain"};
	ASSERT_NO_FATAL_FAILURE(specialist(test.hero, test.level, false));
	const SpellID drain(SpellID::decode("new-horizons:lifeDrain"));
	ASSERT_TRUE(drain.hasValue());
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, drain, "new-horizons:shadowMagic"));
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 5, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(emptyField());
	auto * friendly = place(BattleSide::ATTACKER, "core:angel", BattleHex(3, 5), 20, 1000);
	auto * victim = place(BattleSide::DEFENDER, "core:pikeman", BattleHex(12, 5), 500, 100);
	ASSERT_TRUE(friendly && victim);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_NO_FATAL_FAILURE(activate(friendly));
	spells::BattleCast probe(battle(), attackerSideHero, spells::Mode::HERO, drain.toSpell());
	const auto mechanics = drain.toSpell()->battleMechanics(&probe);
	ASSERT_TRUE(mechanics);
	const auto ordinary = mechanics->adjustEffectValue(victim);
	const auto expected = ordinary + ordinary * test.bonusPercent / 100;
	const auto beforeHealth = victim->getAvailableHealth();
	const auto beforeMana = attackerSideHero->getManaAvailable();
	const auto preview = newHorizonsMagic::spellEffectPreview(*attackerSideHero, *drain.toSpell(), battle());
	ASSERT_TRUE(preview.primary);
	ASSERT_TRUE(preview.secondary);
	EXPECT_EQ(preview.primary->value, expected);
	const auto adventurePreview = newHorizonsMagic::spellEffectPreview(*attackerSideHero, *drain.toSpell());
	ASSERT_TRUE(adventurePreview.primary);
	EXPECT_EQ(adventurePreview.primary->value, expected);
	EXPECT_EQ(preview.secondary->value, 60);
	EXPECT_EQ(preview.qualificationKey, "new-horizons.spellbook.lifeDrainHealing");
	EXPECT_EQ(preview, newHorizonsMagic::spellEffectPreview(*attackerSideHero, *drain.toSpell(), battle()));
	EXPECT_EQ(victim->getAvailableHealth(), beforeHealth);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), beforeMana);
	int64_t targetDamage = -1;
	spells::Target target;
	target.emplace_back(victim, victim->getPosition());
	mechanics->forEachEffect([&](const spells::effects::Effect & effect)
	{
		if(effect.forecastImplementation(mechanics.get()) != spells::effects::ForecastImplementation::CANONICAL_LIFE_DRAIN)
			return false;
		targetDamage = -effect.getHealthChange(mechanics.get(), target).hpDelta;
		return true;
	});
	EXPECT_EQ(targetDamage, expected);
	auto action = spellAt(BattleSide::ATTACKER, drain, victim);
	action.aimToUnit(friendly);
	ASSERT_TRUE(submit(action));
	EXPECT_EQ(beforeHealth - victim->getAvailableHealth(), expected);
	EXPECT_EQ(newHorizonsMagic::lifeDrainDamageWithSpecialty(attackerSideHero, 34),
		34 + 34 * test.bonusPercent / 100);
	EXPECT_EQ(newHorizonsMagic::lifeDrainDamageWithSpecialty(attackerSideHero, 0), 0);
	EXPECT_EQ(newHorizonsMagic::lifeDrainDamageWithSpecialty(attackerSideHero, std::numeric_limits<int64_t>::max()),
		std::numeric_limits<int64_t>::max());
}

INSTANTIATE_TEST_SUITE_P(CurrentAndHistorical, LifeDrainSpecialtyPreviewTest,
	::testing::Values(
		LifeDrainPreviewCase{"core:aislinn", 1, false, 5},
		LifeDrainPreviewCase{"core:aislinn", 10, false, 50},
		LifeDrainPreviewCase{"core:aislinn", 20, false, 100},
		LifeDrainPreviewCase{"core:aislinn", 10, true, 0},
		LifeDrainPreviewCase{"core:ivor", 10, false, 0}));
