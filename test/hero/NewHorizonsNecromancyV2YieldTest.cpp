/*
 * NewHorizonsNecromancyV2YieldTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../lib/entities/hero/NewHorizonsNecromancy.h"
#include "../../lib/entities/hero/NewHorizonsNecromancyPolicy.h"
#include "../../lib/entities/hero/NewHorizonsReclaimer.h"

namespace
{
newHorizonsNecromancy::Policy versionTwo()
{
	newHorizonsNecromancy::Policy policy{};
	policy.version = 2;
	policy.boneCollectorRaiseBonusPercent = 50;
	policy.soulHarvesterManaPerRaisedCreature = 1;
	policy.soulHarvesterManaCap = 50;
	return policy;
}
}

TEST(NewHorizonsNecromancyV2Yield, CollectorMultipliesFreshYieldWithOneFinalFloor)
{
	const auto policy = versionTwo();
	const auto plain = newHorizonsNecromancy::resolveSkeletonRaising(1, 67, false, true, 0, 1000,
		CreatureID::NONE, 0, 3, policy);
	const auto boosted = newHorizonsNecromancy::resolveSkeletonRaising(1, 67, true, true, 0, 1000,
		CreatureID::NONE, 0, 3, policy);
	EXPECT_EQ(plain.percentage, 13);
	EXPECT_EQ(plain.skeletonsRaised, 8);
	EXPECT_EQ(boosted.percentage, 13); // Base rate is preserved, not a fabricated rounded 19%.
	EXPECT_EQ(boosted.skeletonsRaised, 13); // floor(67 * .13 * 1.5), not floor(8 * 1.5).
	EXPECT_EQ(boosted.manaRecovered, 13);
}

TEST(NewHorizonsNecromancyV2Yield, ManaUsesOnlyFreshRetainedSkeletonsAndNormalCapacity)
{
	const auto policy = versionTwo();
	auto raised = newHorizonsNecromancy::resolveSkeletonRaising(3, 1000, true, true, 0, 1000,
		CreatureID::NONE, 0, 0, policy);
	EXPECT_EQ(raised.skeletonsRaised, 450);
	EXPECT_EQ(raised.manaRecovered, 50);
	raised.recoveredSkeletons = 100;
	raised.recoveredSkeletonWarriors = 100;
	raised.skeletonsRaised = 2;
	raised.zombiesRaised = 100;
	newHorizonsNecromancy::finalizeRaisedResult(raised, 9, 10, &policy);
	EXPECT_EQ(raised.manaRecovered, 1);
	newHorizonsNecromancy::finalizeRaisedResult(raised, 0, 1000, &policy);
	EXPECT_EQ(raised.manaRecovered, 2);
	raised.skeletonsRaised = 0;
	newHorizonsNecromancy::finalizeRaisedResult(raised, 0, 1000, &policy);
	EXPECT_EQ(raised.manaRecovered, 0);
	const auto blocked = newHorizonsNecromancy::resolveSkeletonRaising(3, 1000, true, false, 0, 1000,
		CreatureID::NONE, 0, 0, policy);
	EXPECT_TRUE(blocked.blockedByArmyCapacity);
	EXPECT_EQ(blocked.manaRecovered, 0);
}

TEST(NewHorizonsNecromancyV2Yield, ResidualZombieSpecialtyStillConvertsButNeverGeneratesNewMana)
{
	const auto policy = versionTwo();
	const auto raised = newHorizonsNecromancy::resolveSkeletonRaising(1, 100, true, true, 0, 1000,
		CreatureID::NONE, 0, 0, policy);
	const auto converted = newHorizonsNecromancy::reclaimResidualSkeletons(raised, 100, 0, 1000, &policy);
	EXPECT_EQ(converted.skeletonsRaised, 0);
	EXPECT_EQ(converted.reclaimerZombiesRaised, 5);
	EXPECT_EQ(converted.manaRecovered, 0);
	const auto historical = newHorizonsNecromancy::reclaimResidualSkeletons(raised, 100, 0, 1000);
	EXPECT_EQ(historical.reclaimerZombiesRaised, converted.reclaimerZombiesRaised);
}
