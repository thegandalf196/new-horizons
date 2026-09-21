/*
 * NewHorizonsShroudTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "BattleTestFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/battle/NewHorizonsShroud.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"

namespace
{
class NewHorizonsShroudTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	SecondarySkill shroud() const
	{
		const int decoded = SecondarySkill::decode(std::string(newHorizonsShroud::SKILL_ID));
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}
};
}

TEST(NewHorizonsShroudRulesTest, RankTableMatchesCanonicalPercentages)
{
	EXPECT_EQ(newHorizonsShroud::flankingDamagePercent(MasteryLevel::NONE), 0);
	EXPECT_EQ(newHorizonsShroud::flankingDamagePercent(MasteryLevel::BASIC), 25);
	EXPECT_EQ(newHorizonsShroud::flankingDamagePercent(MasteryLevel::ADVANCED), 40);
	EXPECT_EQ(newHorizonsShroud::flankingDamagePercent(MasteryLevel::EXPERT), 60);
	EXPECT_FALSE(newHorizonsShroud::deniesRetaliation(MasteryLevel::ADVANCED));
	EXPECT_TRUE(newHorizonsShroud::deniesRetaliation(MasteryLevel::EXPERT));
}

TEST_F(NewHorizonsShroudTest, RankDamagePremiumAppliesOnlyFromRear)
{
	startGame();
	attackerSideHero->setSecSkillLevel(shroud(), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	startBattle();
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 100);
	auto * front = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80), 100);
	auto * rear = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(82), 100);
	forceMaximumDamage(front);
	forceMaximumDamage(rear);

	const BattleAttackInfo frontAttack(front, defender, 0, false);
	const BattleAttackInfo rearAttack(rear, defender, 0, false);
	EXPECT_FALSE(battle()->battleIsShroudFlankingAttack(frontAttack));
	EXPECT_TRUE(battle()->battleIsShroudFlankingAttack(rearAttack));
	const auto frontDamage = battle()->calculateDmgRange(frontAttack).damage.max;
	const auto rearDamage = battle()->calculateDmgRange(rearAttack).damage.max;
	ASSERT_GT(frontDamage, 0);
	EXPECT_EQ(rearDamage, frontDamage * 160 / 100);
}

TEST_F(NewHorizonsShroudTest, ExpertRearAttackDeniesOnlyNormalRetaliation)
{
	startGame();
	attackerSideHero->setSecSkillLevel(shroud(), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	startBattle();
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 100);
	auto * rear = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(82), 100);
	BattleAttackInfo rearAttack(rear, defender, 0, false);
	EXPECT_TRUE(battle()->battleShroudDeniesRetaliation(rearAttack));
	DamageEstimation retaliation;
	battle()->battleEstimateDamage(rearAttack, &retaliation);
	EXPECT_EQ(retaliation.damage.max, 0);
	ASSERT_TRUE(attack(rear, defender->getPosition()));
	EXPECT_EQ(std::count_if(server.attacks.begin(), server.attacks.end(), [&](const auto & result)
	{
		return result.stackAttacking == defender->unitId();
	}), 0);

	rearAttack.shooting = true;
	EXPECT_FALSE(battle()->battleShroudDeniesRetaliation(rearAttack));
}
