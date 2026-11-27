/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "mock/mock_battle_Unit.h"
#include "mock/mock_BonusBearer.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/ResidualLife.h"
#include "../../lib/entities/hero/NewHorizonsNecropolisCombatSpecialties.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/json/JsonNode.h"
#include <limits>
using namespace testing;
namespace
{
class EternalServitudeTest : public Test
{
protected:
	UnitMock unit;
	BonusBearerMock bonuses;
	battle::CHealth health{&unit};
	EternalServitudeTest()
	{
		EXPECT_CALL(unit, getAllBonuses(_, _)).WillRepeatedly(Invoke(&bonuses, &BonusBearerMock::getAllBonuses));
		EXPECT_CALL(unit, getTreeVersion()).WillRepeatedly(Return(1));
		EXPECT_CALL(unit, unitBaseAmount()).WillRepeatedly(Return(20));
		bonuses.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACK_HEALTH,
			BonusSource::CREATURE_ABILITY, 10, BonusSourceID()));
		health.init();
	}
	void damage(int64_t amount) { health.damage(amount, false, false, battle::DamageProvenance::SPELL); }
	void reanimate(int64_t amount)
	{
		const auto before = health.prepareEternalServitudeRestoration();
		health.heal(amount, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE);
		health.markEternalServitudeRestoration(before);
	}
};
}
TEST(ResidualLifeTest, FractionalDamageAccumulatesAndCombatCapCannotOverflow)
{
	auto first = battle::residualLife::record(0, 48, 49, 20);
	EXPECT_EQ(first.grant, 0);
	auto second = battle::residualLife::record(first.accumulatedDamage, 1, 49, 20);
	EXPECT_EQ(second.grant, 1);
	auto capped = battle::residualLife::record(second.accumulatedDamage, std::numeric_limits<int64_t>::max(), 49, 20);
	EXPECT_EQ(capped.accumulatedDamage, 980);
	EXPECT_EQ(capped.grant, 19);
	EXPECT_EQ(battle::residualLife::record(980, 1000, 49, 20).grant, 0);
	EXPECT_THROW(battle::residualLife::record(-1, 10, 49, 20), std::invalid_argument);
	EXPECT_THROW(battle::residualLife::record(981, 10, 49, 20), std::invalid_argument);
}
TEST_F(EternalServitudeTest, RetainsOnlyFractionOfRestoredMembersStillAlive)
{
	damage(100);
	reanimate(100);
	damage(40);
	EXPECT_EQ(health.getCount(), 16);
	EXPECT_EQ(health.getResurrected(), 6);
	EXPECT_EQ(health.retainEternalServitude(50), 3);
	health.takeResurrected();
	EXPECT_EQ(health.getCount(), 13);
	EXPECT_EQ(health.getResurrected(), 0);
}
TEST_F(EternalServitudeTest, RepeatedPartialHealingDoesNotPromoteUnrelatedRevival)
{
	damage(100);
	int64_t ordinary = 20;
	health.heal(ordinary, EHealLevel::RESURRECT, EHealPower::ONE_BATTLE);
	reanimate(15);
	reanimate(65);
	EXPECT_EQ(health.retainEternalServitude(50), 4);
	health.takeResurrected();
	EXPECT_EQ(health.getCount(), 14);
}
TEST_F(EternalServitudeTest, WipeAndNoTaggedResurrectionCannotRetainMembers)
{
	damage(100);
	reanimate(100);
	damage(200);
	EXPECT_EQ(health.retainEternalServitude(60), 0);
	health.takeResurrected();
	EXPECT_EQ(health.getCount(), 0);
}
TEST(NecropolisCombatRulesTest, CapturesOptionalRowsAndPreservesLevelOneBases)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
	const auto typed = newHorizonsHeroes::necropolisCombatSpecialtyRules(rules);
	ASSERT_TRUE(typed.residualLife && typed.earthboundDrain && typed.eternalServitude && typed.unyieldingBones);
	EXPECT_EQ(typed.residualLife->damagePerBuffer, 50);
	EXPECT_EQ(typed.earthboundDrain->at(1), 5);
	EXPECT_EQ(typed.earthboundDrain->at(20), 100);
	EXPECT_EQ(typed.eternalServitude->at(1), 20);
	EXPECT_EQ(typed.eternalServitude->at(21), 60);
	EXPECT_EQ(typed.unyieldingBones->at(1), 50);
	EXPECT_EQ(typed.unyieldingBones->at(2), 55);
	EXPECT_EQ(typed.unyieldingBones->at(21), 150);
	for(const auto * key : {"residualLife", "earthboundDrain", "eternalServitude", "unyieldingBones"})
		rules["revisedSpecialties"].Struct().erase(key);
	const auto historical = newHorizonsHeroes::necropolisCombatSpecialtyRules(rules);
	EXPECT_FALSE(historical.residualLife || historical.earthboundDrain || historical.eternalServitude || historical.unyieldingBones);
}
