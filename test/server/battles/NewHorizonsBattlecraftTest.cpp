/*
 * NewHorizonsBattlecraftTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "BattleTestFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/battle/NewHorizonsBattlecraft.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

namespace
{
class NewHorizonsBattlecraftTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	SecondarySkill battlecraft() const
	{
		const int decoded = SecondarySkill::decode("new-horizons:battlecraft");
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void setBattlecraftRank(CGHeroInstance * hero, int rank, bool entrench = false)
	{
		hero->setSecSkillLevel(battlecraft(), rank, ChangeValueMode::ABSOLUTE);
		if(entrench)
			hero->applyPerkSelection({"new-horizons:battlecraft", "new-horizons:battlecraft.entrench"});
	}

	static int64_t attackDamage(const BattleAttack & attack)
	{
		EXPECT_FALSE(attack.bsa.empty());
		return attack.bsa.empty() ? 0 : attack.bsa.front().damageAmount;
	}

	std::pair<int64_t, int64_t> defendedDamage(bool entrench)
	{
		startGame();
		defenderSideHero->setSecSkillLevel(battlecraft(), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		if(entrench)
			defenderSideHero->applyPerkSelection({"new-horizons:battlecraft", "new-horizons:battlecraft.entrench"});
		startBattle();
		auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 100);
		auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 100);
		forceMaximumDamage(attacker);
		defender->defending = true;
		BattleAttackInfo physical(attacker, defender, 0, false);
		BattleAttackInfo nonPhysical(attacker, defender, 0, false);
		nonPhysical.physicalDamage = false;
		return {battle()->calculateDmgRange(physical).damage.max,
			battle()->calculateDmgRange(nonPhysical).damage.max};
	}
};
}

TEST(NewHorizonsBattlecraftRulesTest, RankAndEntrenchTablesMatchCanonicalPercentages)
{
	EXPECT_EQ(newHorizonsBattlecraft::rankPercent(0), 0);
	EXPECT_EQ(newHorizonsBattlecraft::rankPercent(1), 5);
	EXPECT_EQ(newHorizonsBattlecraft::rankPercent(2), 10);
	EXPECT_EQ(newHorizonsBattlecraft::rankPercent(3), 15);
	EXPECT_EQ(newHorizonsBattlecraft::rankPercent(-1), 0);
	EXPECT_EQ(newHorizonsBattlecraft::rankPercent(4), 15);
}

TEST_F(NewHorizonsBattlecraftTest, DefendReductionAffectsOnlyPhysicalCreatureDamage)
{
	const auto [physical, nonPhysical] = defendedDamage(false);
	EXPECT_EQ(physical, nonPhysical * 85 / 100);
}

TEST_F(NewHorizonsBattlecraftTest, EntrenchAddsFivePercentagePoints)
{
	const auto [physical, nonPhysical] = defendedDamage(true);
	EXPECT_EQ(physical, nonPhysical * 80 / 100);
}

TEST_F(NewHorizonsBattlecraftTest, DefendReductionUsesBasicAdvancedAndExpertRanks)
{
	startGame();
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 100);
	forceMaximumDamage(attacker);
	defender->defending = true;
	const BattleAttackInfo physical(attacker, defender, 0, false);
	BattleAttackInfo nonPhysical = physical;
	nonPhysical.physicalDamage = false;
	const auto before = battle()->calculateDmgRange(nonPhysical).damage.max;

	for(const auto [rank, reduction] : {std::pair{1, 5}, std::pair{2, 10}, std::pair{3, 15}})
	{
		SCOPED_TRACE(rank);
		setBattlecraftRank(defenderSideHero, rank);
		EXPECT_EQ(battle()->calculateDmgRange(physical).damage.max, before * (100 - reduction) / 100);
	}
}

TEST_F(NewHorizonsBattlecraftTest, DefendReductionEndsAtTheStackNextActivation)
{
	startGame();
	setBattlecraftRank(defenderSideHero, MasteryLevel::EXPERT);
	startBattle();
	const auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 100);
	forceMaximumDamage(const_cast<CStack *>(attacker));
	const BattleAttackInfo attack(attacker, defender, 0, false);
	const auto before = battle()->calculateDmgRange(attack).damage.max;
	ASSERT_GT(before, 0);

	battle()->activeStack = defender->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), defender->unitOwner(),
		BattleAction::makeDefend(defender)));
	EXPECT_TRUE(defender->defended());
	const auto whileDefending = battle()->calculateDmgRange(attack).damage.max;
	EXPECT_LT(whileDefending, before);

	// nextTurn is the authoritative activation boundary; it is intentionally
	// kept in the same round so afterNewRound cannot mask a stale Defend state.
	battle()->nextTurn(defender->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	const auto afterActivation = battle()->calculateDmgRange(attack).damage.max;
	EXPECT_EQ(afterActivation, before);
}

TEST_F(NewHorizonsBattlecraftTest, WaitAddsRankedDamageToOnePhysicalAttackOnly)
{
	startGame();
	setBattlecraftRank(attackerSideHero, MasteryLevel::ADVANCED);
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 100);
	forceMaximumDamage(attacker);
	blockRetaliation(attacker);
	blockRetaliation(defender);

	const BattleAttackInfo attackInfo(attacker, defender, 0, false);
	const auto before = battle()->calculateDmgRange(attackInfo).damage.max;
	ASSERT_GT(before, 0);

	battle()->activeStack = attacker->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->battleGetOwner(attacker),
		BattleAction::makeWait(attacker)));
	EXPECT_TRUE(attacker->waitedThisTurn);
	EXPECT_FALSE(attacker->battlecraftWaitBonusUsed);
	EXPECT_EQ(battle()->calculateDmgRange(attackInfo).damage.max, before * 110 / 100);

	battle()->activeStack = attacker->unitId();
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	ASSERT_EQ(server.attacks.size(), 1u);
	EXPECT_EQ(attackDamage(server.attacks.back()), before * 110 / 100);
	EXPECT_TRUE(attacker->battlecraftWaitBonusUsed);

	// A second physical attack in the same round is ordinary damage.
	battle()->activeStack = attacker->unitId();
	ASSERT_TRUE(attack(attacker, defender->getPosition()));
	ASSERT_EQ(server.attacks.size(), 2u);
	EXPECT_EQ(attackDamage(server.attacks.back()), before);
}

TEST_F(NewHorizonsBattlecraftTest, WaitBonusAppliesToAPhysicalRetaliation)
{
	startGame();
	setBattlecraftRank(defenderSideHero, MasteryLevel::BASIC);
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 100);
	forceMaximumDamage(defender);

	const BattleAttackInfo retaliationInfo(defender, attacker, 0, false);
	const auto retaliationBefore = battle()->calculateDmgRange(retaliationInfo).damage.max;
	ASSERT_GT(retaliationBefore, 0);

	battle()->activeStack = defender->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->battleGetOwner(defender),
		BattleAction::makeWait(defender)));
	battle()->activeStack = attacker->unitId();
	ASSERT_TRUE(attack(attacker, defender->getPosition()));

	const auto retaliation = std::find_if(server.attacks.begin(), server.attacks.end(),
		[&](const BattleAttack & attack) { return attack.stackAttacking == defender->unitId(); });
	ASSERT_NE(retaliation, server.attacks.end());
	EXPECT_TRUE(retaliation->counter());
	EXPECT_TRUE(defender->battlecraftWaitBonusUsed);
	const auto remainingStackBaseline = battle()->calculateDmgRange(retaliationInfo).damage.max;
	EXPECT_EQ(attackDamage(*retaliation), remainingStackBaseline * 105 / 100);
}

TEST_F(NewHorizonsBattlecraftTest, WaitBonusDoesNotModifyNonPhysicalDamage)
{
	startGame();
	setBattlecraftRank(attackerSideHero, MasteryLevel::EXPERT);
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 100);
	const BattleAttackInfo physical(attacker, defender, 0, false);
	BattleAttackInfo nonPhysical = physical;
	nonPhysical.physicalDamage = false;
	const auto before = battle()->calculateDmgRange(physical).damage.max;

	battle()->activeStack = attacker->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->battleGetOwner(attacker),
		BattleAction::makeWait(attacker)));
	EXPECT_EQ(battle()->calculateDmgRange(physical).damage.max, before * 115 / 100);
	EXPECT_EQ(battle()->calculateDmgRange(nonPhysical).damage.max, before);
	EXPECT_FALSE(attacker->battlecraftWaitBonusUsed);
}
