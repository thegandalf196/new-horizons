/*
 * BattlecraftStatusPresentationTest.cpp, part of VCMI / New Horizons
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "../StdInc.h"

#include "../../client/battle/NewHorizonsBattleStatus.h"
#include "../../lib/battle/NewHorizonsBattlecraft.h"

namespace
{
using namespace newHorizonsBattleStatus;

std::optional<BattlecraftWaitStatus> armedWaitStatus(int rank)
{
	return makeBattlecraftWaitStatus(newHorizonsBattlecraft::rankPercent(rank), true);
}
}

TEST(BattlecraftStatusPresentationTest, WaitReadbackShowsRankBonusOnlyWhileArmed)
{
	EXPECT_FALSE(armedWaitStatus(0).has_value());
	EXPECT_FALSE(makeBattlecraftWaitStatus(newHorizonsBattlecraft::rankPercent(3), false).has_value());
	EXPECT_TRUE(battlecraftWaitTooltip(BattlecraftWaitStatus{}).empty());

	for(int rank = 1; rank <= 3; ++rank)
	{
		const auto status = armedWaitStatus(rank);
		ASSERT_TRUE(status.has_value());
		EXPECT_TRUE(status->active());
		EXPECT_EQ(status->damageBonusPercent, rank * 5);

		const auto tooltip = battlecraftWaitTooltip(*status);
		EXPECT_NE(tooltip.find("The next attack or retaliation before the end of this round"), std::string::npos);
		EXPECT_NE(tooltip.find("+" + std::to_string(rank * 5) + "% physical damage"), std::string::npos);
	}

	StackInfoStatusSnapshot ready;
	ready.battlecraftWait = armedWaitStatus(2);
	StackInfoStatusSnapshot spent;
	spent.battlecraftWait = makeBattlecraftWaitStatus(newHorizonsBattlecraft::rankPercent(2), false);
	EXPECT_NE(ready, spent);
}

TEST(BattlecraftStatusPresentationTest, WaitReadbackPreservesOrdinaryAndDoubledSharedValues)
{
	// Captured query results are presentation inputs, including mastery doubling.
	const auto ordinary = makeBattlecraftWaitStatus(15, true);
	const auto doubled = makeBattlecraftWaitStatus(30, true);
	ASSERT_TRUE(ordinary.has_value());
	ASSERT_TRUE(doubled.has_value());
	EXPECT_EQ(ordinary->damageBonusPercent, 15);
	EXPECT_EQ(doubled->damageBonusPercent, 30);
	EXPECT_NE(battlecraftWaitTooltip(*ordinary).find("+15%"), std::string::npos);
	EXPECT_NE(battlecraftWaitTooltip(*doubled).find("+30%"), std::string::npos);
	StackInfoStatusSnapshot before;
	before.battlecraftWait = ordinary;
	auto after = before;
	after.battlecraftWait = doubled;
	EXPECT_NE(before, after);
	EXPECT_FALSE(makeBattlecraftWaitStatus(30, false).has_value());
}

TEST(BattlecraftStatusPresentationTest, DefendReadbackComposesBattlecraftEntrenchAndBulwark)
{
	DefendStatus inactive;
	inactive.battlecraftReductionPercent = 20;
	EXPECT_TRUE(defendStatusTooltip(inactive).empty());

	DefendStatus defended;
	defended.defending = true;
	// Expert Battlecraft (15%) plus Entrench's additional five percentage points.
	defended.battlecraftReductionPercent = newHorizonsBattlecraft::rankPercent(3) + 5;
	BulwarkStatus bulwark;
	bulwark.damageReductionBasisPoints = 650;
	bulwark.preemptiveDamagePercent = 50;
	bulwark.preemptiveReady = true;
	defended.bulwark = bulwark;

	const auto tooltip = defendStatusTooltip(defended);
	EXPECT_NE(tooltip.find("until its next normal Creature Activation"), std::string::npos);
	EXPECT_NE(tooltip.find("Battlecraft physical creature-damage reduction: 20%"), std::string::npos);
	EXPECT_NE(tooltip.find("shared Physical Damage Reduction cap"), std::string::npos);
	EXPECT_NE(tooltip.find("Physical creature damage reduction: 6.5%"), std::string::npos);
	EXPECT_NE(tooltip.find("Pre-emptive strike: 50% normal damage"), std::string::npos);

	StackInfoStatusSnapshot basic;
	basic.defend = defended;
	StackInfoStatusSnapshot changedValue;
	changedValue.defend = defended;
	changedValue.defend.battlecraftReductionPercent = 15;
	EXPECT_NE(basic, changedValue);
}

TEST(BattlecraftStatusPresentationTest, OrdinaryDefendHasNoInertBattlecraftClaim)
{
	DefendStatus defended;
	defended.defending = true;
	const auto tooltip = defendStatusTooltip(defended);

	EXPECT_NE(tooltip.find("Defend"), std::string::npos);
	EXPECT_NE(tooltip.find("until its next normal Creature Activation"), std::string::npos);
	EXPECT_EQ(tooltip.find("Battlecraft physical creature-damage reduction"), std::string::npos);
	EXPECT_EQ(tooltip.find("Bulwark"), std::string::npos);
	EXPECT_EQ(tooltip.find("through the end of the current battle round"), std::string::npos);
}
