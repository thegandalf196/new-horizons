/*
 * HeroManagerTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "AI/Nullkiller2/Analyzers/HeroManager.h"

TEST(Nullkiller2_Analyzers_HeroManager, armyCarrierBeatsBetterHeroProfileForMainRole)
{
	constexpr uint64_t strongestArmy = 10000;

	EXPECT_GT(
		NK2AI::evaluateMainHeroRoleScore(5.0f, strongestArmy, strongestArmy),
		NK2AI::evaluateMainHeroRoleScore(40.0f, strongestArmy / 20, strongestArmy))
		<< "the hero carrying the army should not be treated as a scout just because another hero has better skills";
}

TEST(Nullkiller2_Analyzers_HeroManager, heroProfileBreaksSimilarArmyTies)
{
	constexpr uint64_t strongestArmy = 10000;

	EXPECT_GT(
		NK2AI::evaluateMainHeroRoleScore(30.0f, strongestArmy, strongestArmy),
		NK2AI::evaluateMainHeroRoleScore(20.0f, strongestArmy, strongestArmy))
		<< "normal hero profile scoring should still decide between comparable army carriers";
}

TEST(Nullkiller2_Analyzers_HeroManager, canonicalStrategicSkillsReceiveRoleAwareScores)
{
	using NK2AI::evaluateNewHorizonsStrategicSkillRoleScore;
	using NK2AI::HeroRole;

	EXPECT_EQ(evaluateNewHorizonsStrategicSkillRoleScore("new-horizons:estates", HeroRole::MAIN), 0.5f);
	EXPECT_EQ(evaluateNewHorizonsStrategicSkillRoleScore("new-horizons:estates", HeroRole::SCOUT), 2.0f);
	EXPECT_EQ(evaluateNewHorizonsStrategicSkillRoleScore("new-horizons:learning", HeroRole::MAIN), 1.0f);
	EXPECT_EQ(evaluateNewHorizonsStrategicSkillRoleScore("new-horizons:learning", HeroRole::SCOUT), 0.5f);
	EXPECT_FALSE(evaluateNewHorizonsStrategicSkillRoleScore("core:estates", HeroRole::MAIN));
}
