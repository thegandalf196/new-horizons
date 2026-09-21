/*
 * NewHorizonsCombatSkillsTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "BattleTestFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"

namespace
{
class NewHorizonsCombatSkillsTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	SecondarySkill skill(const char * identifier) const
	{
		const int decoded = SecondarySkill::decode(identifier);
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}
};
}

TEST(NewHorizonsCombatSkillsRulesTest, RankTablesMatchCanonicalPercentages)
{
	EXPECT_EQ(newHorizonsCombatSkills::armorerReductionPercent(0), 0);
	EXPECT_EQ(newHorizonsCombatSkills::armorerReductionPercent(1), 5);
	EXPECT_EQ(newHorizonsCombatSkills::armorerReductionPercent(2), 10);
	EXPECT_EQ(newHorizonsCombatSkills::armorerReductionPercent(3), 15);
	EXPECT_EQ(newHorizonsCombatSkills::archeryDamagePercent(0), 0);
	EXPECT_EQ(newHorizonsCombatSkills::archeryDamagePercent(1), 10);
	EXPECT_EQ(newHorizonsCombatSkills::archeryDamagePercent(2), 20);
	EXPECT_EQ(newHorizonsCombatSkills::archeryDamagePercent(3), 30);
}

TEST_F(NewHorizonsCombatSkillsTest, ExpertArmorerReducesOnlyPhysicalCreatureDamage)
{
	startGame();
	defenderSideHero->setSecSkillLevel(skill("new-horizons:armorer"), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 100);
	forceMaximumDamage(attacker);
	BattleAttackInfo physical(attacker, defender, 0, false);
	BattleAttackInfo nonPhysical(attacker, defender, 0, false);
	nonPhysical.physicalDamage = false;
	const auto physicalDamage = battle()->calculateDmgRange(physical).damage.max;
	const auto nonPhysicalDamage = battle()->calculateDmgRange(nonPhysical).damage.max;
	EXPECT_EQ(physicalDamage, nonPhysicalDamage * 85 / 100);
}

TEST_F(NewHorizonsCombatSkillsTest, ExpertArcheryAddsThirtyPercentOnlyToPhysicalShots)
{
	startGame();
	attackerSideHero->setSecSkillLevel(skill("new-horizons:archery"), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:titan"), BattleHex(leftHex), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 4), 100);
	forceMaximumDamage(attacker);
	BattleAttackInfo physical(attacker, defender, 0, true);
	BattleAttackInfo nonPhysical(attacker, defender, 0, true);
	nonPhysical.physicalDamage = false;
	const auto physicalDamage = battle()->calculateDmgRange(physical).damage.max;
	const auto nonPhysicalDamage = battle()->calculateDmgRange(nonPhysical).damage.max;
	EXPECT_EQ(nonPhysicalDamage, 7200);
	EXPECT_EQ(physicalDamage, 9000);
}

TEST_F(NewHorizonsCombatSkillsTest, SpellLikeCreatureShotIsClassifiedNonPhysicalForServerAndAI)
{
	startGame();
	startBattle();
	auto * lich = addStack(BattleSide::ATTACKER, creatureByName("core:lich"), BattleHex(leftHex), 10);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 4), 10);
	ASSERT_TRUE(lich->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
	const BattleAttackInfo shot(lich, target, 0, true);
	EXPECT_FALSE(shot.physicalDamage);
	const auto retaliation = shot.reverse();
	EXPECT_TRUE(retaliation.physicalDamage);
}
