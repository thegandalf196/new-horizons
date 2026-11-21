/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "../../AI/BattleAI/DispelValue.h"
#include "../../lib/spells/CSpell.h"

namespace heroQueueTest
{
namespace
{
class DispelValueTest : public HeroQueueSelectorTest
{
protected:
	/// Dispels one enemy spell effect changing a primary skill by `change`.
	void dispelEnemyEffect(int change, SpellID source, float & value, PrimarySkill skill = PrimarySkill::ATTACK)
	{
		useRevisedOrders = true;
		ASSERT_NO_FATAL_FAILURE(world());
		ASSERT_NO_FATAL_FAILURE(arena());
		Bonus bonus(BonusDuration::N_TURNS, BonusType::PRIMARY_SKILL, BonusSource::SPELL_EFFECT, change,
			BonusSourceID(source), BonusSubtypeID(skill));
		bonus.turnsRemain = 3;
		effect(enemy, bonus);
		environment = std::make_shared<QueueEnvironment>(gameState());
		live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		parent->removeUnitBonus(enemy->unitId(), {bonus});
		ASSERT_FALSE(parent->battleGetUnitByID(enemy->unitId())->hasBonusFrom(BonusSource::SPELL_EFFECT, BonusSourceID(source)));
		value = dispelAI::removedEffectValue(SpellID(SpellID::DISPEL).toSpell(), *live, parent, PlayerColor(0), 1, 1);
		EXPECT_EQ(dispelAI::removedEffectValue(SpellID(SpellID::CURE).toSpell(), *live, parent, PlayerColor(0), 1, 1), 0);
	}
};
}

TEST_F(DispelValueTest, DispellingOurOwnDebuffFromAnEnemyIsALoss)
{
	float value = 0;
	ASSERT_NO_FATAL_FAILURE(dispelEnemyEffect(-10, SpellID(SpellID::WEAKNESS), value));
	EXPECT_LT(value, 0);
}

TEST_F(DispelValueTest, DispellingOurDefenseCurseFromAnEnemyIsALoss)
{
	float value = 0;
	ASSERT_NO_FATAL_FAILURE(dispelEnemyEffect(-10, SpellID(SpellID::CURSE), value, PrimarySkill::DEFENSE));
	EXPECT_LT(value, 0);
}

TEST_F(DispelValueTest, DispellingAnEnemyBuffIsAGain)
{
	float value = 0;
	ASSERT_NO_FATAL_FAILURE(dispelEnemyEffect(10, SpellID(SpellID::BLOODLUST), value));
	EXPECT_GT(value, 0);
}

TEST_F(DispelValueTest, NothingRemovedIsWorthNothing)
{
	useRevisedOrders = true;
	ASSERT_NO_FATAL_FAILURE(world());
	ASSERT_NO_FATAL_FAILURE(arena());
	environment = std::make_shared<QueueEnvironment>(gameState());
	live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	parent = std::make_shared<HypotheticBattle>(environment.get(), live);
	EXPECT_EQ(dispelAI::removedEffectValue(SpellID(SpellID::DISPEL).toSpell(), *live, parent, PlayerColor(0), 1, 1), 0);
}
}
