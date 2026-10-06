/*
 * BattleMoraleReadbackTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "../StdInc.h"

#include "../../client/battle/NewHorizonsBattleStatus.h"

namespace
{
using namespace newHorizonsBattleStatus;
}

TEST(BattleMoraleReadbackTest, PreservesNegativeAndPositiveRealAndEffectiveValues)
{
	const auto negative = makeBattleMoraleReadback(true, -10, 0, 1, -1, 1,
		true, false, false, {"Enemy spell: -2 Morale"});
	ASSERT_TRUE(negative.active());
	EXPECT_EQ(negative.real, -10);
	EXPECT_EQ(negative.effective, 0);
	EXPECT_TRUE(negative.hasSources());
	EXPECT_TRUE(negative.commandingPresenceFloorApplied);

	const auto positive = makeBattleMoraleReadback(true, 10, 10, 0, 0, 0,
		false, false, false, {"Discipline: +2 Morale"});
	ASSERT_TRUE(positive.active());
	EXPECT_EQ(positive.real, 10);
	EXPECT_EQ(positive.effective, 10);
}

TEST(BattleMoraleReadbackTest, DisabledAndMoraleImmuneSnapshotsDoNotExposeSources)
{
	const auto disabled = makeBattleMoraleReadback(false, -10, 0, 1, -1, 1,
		true, false, false, {"Hidden source"});
	EXPECT_EQ(disabled, BattleMoraleReadback{});
	EXPECT_FALSE(disabled.active());

	const auto immune = makeBattleMoraleReadback(true, -10, 0, 1, -1, 1,
		true, true, true, {"Hidden source"});
	ASSERT_TRUE(immune.active());
	EXPECT_TRUE(immune.unaffectedByMorale);
	EXPECT_EQ(immune.real, -10);
	EXPECT_EQ(immune.effective, 0);
	EXPECT_FALSE(immune.hasSources());
	EXPECT_TRUE(immune.bonusDescriptions.empty());
	EXPECT_EQ(immune.standardBearerBonus, 0);
	EXPECT_EQ(immune.firstRoundModifier, 0);
	EXPECT_EQ(immune.steadfastAdjustment, 0);
	EXPECT_FALSE(immune.commandingPresenceFloorApplied);
	EXPECT_FALSE(immune.furyUnboundFloorApplied);
}

TEST(BattleMoraleReadbackTest, EqualEffectiveFloorsStillTrackRealValueAndFloorReason)
{
	const auto commandingPresence = makeBattleMoraleReadback(true, -2, 0, 0, 0, 0,
		true, false, false, {});
	const auto furyUnbound = makeBattleMoraleReadback(true, -10, 0, 0, 0, 0,
		false, true, false, {});
	ASSERT_EQ(commandingPresence.effective, furyUnbound.effective);
	EXPECT_NE(commandingPresence, furyUnbound);
	EXPECT_TRUE(commandingPresence.hasSources());
	EXPECT_TRUE(furyUnbound.hasSources());
}
