/*
 * NewHorizonsBulwarkRuntimeTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "BattleTestFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/battle/NewHorizonsBulwark.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"

namespace
{
struct BulwarkReductionCase
{
	int rank;
	int basisPoints;
};

class NewHorizonsBulwarkRuntimeTest : public BattleTestFixture,
	public ::testing::WithParamInterface<BulwarkReductionCase>
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	SecondarySkill bulwark() const
	{
		const int decoded = SecondarySkill::decode("new-horizons:bulwarkOfTheMire");
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	std::pair<int64_t, int64_t> damageBeforeAndWhileDefending(int rank)
	{
		startGame();
		attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 20, ChangeValueMode::ABSOLUTE);
		defenderSideHero->setPrimarySkill(PrimarySkill::DEFENSE, 20, ChangeValueMode::ABSOLUTE);
		defenderSideHero->setSecSkillLevel(bulwark(), rank, ChangeValueMode::ABSOLUTE);
		EXPECT_EQ(defenderSideHero->getSecSkillLevel(bulwark()), rank);
		EXPECT_EQ(defenderSideHero->getPerkSkillRank(std::string(newHorizonsBulwark::SKILL_ID)), rank);
		EXPECT_EQ(newHorizonsBulwark::rank(defenderSideHero), rank);
		startBattle();
		EXPECT_EQ(battle()->getSideHero(BattleSide::DEFENDER), defenderSideHero);
		EXPECT_EQ(newHorizonsBulwark::rank(battle()->getSideHero(BattleSide::DEFENDER)), rank);

		auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"),
			BattleHex(leftHex), 100);
		auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"),
			BattleHex(rightHex), 100);
		forceMaximumDamage(attacker);
		const BattleAttackInfo info(attacker, defender, 0, false);
		const auto before = battle()->calculateDmgRange(info).damage.max;
		defender->defending = true;
		EXPECT_TRUE(defender->defended());
		EXPECT_EQ(battle()->battleGetOwnerHero(defender), defenderSideHero);
		const auto whileDefending = battle()->calculateDmgRange(info).damage.max;
		return {before, whileDefending};
	}
};
}

TEST_P(NewHorizonsBulwarkRuntimeTest, DefendingPhysicalStackUsesExactRankReduction)
{
	const auto [before, after] = damageBeforeAndWhileDefending(GetParam().rank);
	ASSERT_GT(before, 0);
	EXPECT_EQ(after, before * (10000 - GetParam().basisPoints) / 10000);
}

TEST_F(NewHorizonsBulwarkRuntimeTest, FirstMeleeAttackTriggersOneScaledPreemptiveStrikeWithoutSpendingRetaliation)
{
	startGame();
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 100);
	forceMaximumDamage(defender);
	blockRetaliation(attacker);
	defender->defending = true;

	const BattleAttackInfo normal(defender, attacker, 0, false);
	const auto normalDamage = battle()->calculateDmgRange(normal).damage.max;
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	ASSERT_GE(server.attacks.size(), 2u);
	EXPECT_EQ(server.attacks.front().stackAttacking, defender->unitId());
	ASSERT_EQ(server.attacks.front().bsa.size(), 1u);
	EXPECT_EQ(server.attacks.front().bsa.front().damageAmount, normalDamage * 50 / 100);
	EXPECT_FALSE(server.attacks.front().counter());
	EXPECT_EQ(defender->counterAttacks.total(), 1);
	EXPECT_TRUE(defender->bulwarkPreemptiveUsed);

	const auto defenderAttacks = std::count_if(server.attacks.begin(), server.attacks.end(), [&](const auto & result)
	{
		return result.stackAttacking == defender->unitId();
	});
	battle()->activeStack = attacker->unitId();
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	EXPECT_EQ(std::count_if(server.attacks.begin(), server.attacks.end(), [&](const auto & result)
	{
		return result.stackAttacking == defender->unitId();
	}), defenderAttacks);
}

TEST_F(NewHorizonsBulwarkRuntimeTest, AdvancedReflectsActualReducedMeleeDamageWithoutAnotherAttack)
{
	startGame();
	defenderSideHero->setSecSkillLevel(bulwark(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(81), 100);
	forceMaximumDamage(attacker);
	blockRetaliation(attacker);
	defender->defending = true;
	defender->bulwarkPreemptiveUsed = true;
	const auto attackerHealth = attacker->getAvailableHealth();
	const auto defenderHealth = defender->getAvailableHealth();

	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	ASSERT_EQ(server.attacks.size(), 1u);
	ASSERT_EQ(server.attacks.front().bsa.size(), 1u);
	const int64_t received = std::min<int64_t>(server.attacks.front().bsa.front().damageAmount, defenderHealth);
	EXPECT_EQ(attackerHealth - attacker->getAvailableHealth(), received * 25 / 100);
}

INSTANTIATE_TEST_SUITE_P(Ranks, NewHorizonsBulwarkRuntimeTest,
	::testing::Values(
		BulwarkReductionCase{MasteryLevel::NONE, 0},
		BulwarkReductionCase{MasteryLevel::BASIC, 700},
		BulwarkReductionCase{MasteryLevel::ADVANCED, 1050},
		BulwarkReductionCase{MasteryLevel::EXPERT, 1400}));
