/*
 * NewHorizonsSorceryTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../../lib/spells/NewHorizonsSorcery.h"

#include <limits>

TEST(NewHorizonsSorceryTest, PhantomIntegrityUsesTheCanonicalCapAndPerkMultiplier)
{
	EXPECT_EQ(newHorizonsSorcery::phantomArmyIntegrityBasisPoints(0), 2000);
	EXPECT_EQ(newHorizonsSorcery::phantomArmyIntegrityBasisPoints(100), 3500);
	EXPECT_EQ(newHorizonsSorcery::phantomArmyIntegrityBasisPoints(200), 4000);
	EXPECT_EQ(newHorizonsSorcery::phantomArmyIntegrityBasisPoints(std::numeric_limits<int32_t>::max()), 4000);
	EXPECT_EQ(newHorizonsSorcery::phantomArmyIntegrityBasisPoints(100, true), 4375);
	EXPECT_EQ(newHorizonsSorcery::phantomArmyIntegrity(1000, 100), 350);
	EXPECT_EQ(newHorizonsSorcery::phantomArmyIntegrity(1234, 100), 431);
	EXPECT_EQ(newHorizonsSorcery::phantomArmyIntegrity(1000, 100, true), 437);
	EXPECT_EQ(newHorizonsSorcery::phantomArmyIntegrity(1234, 100, true), 539);
	EXPECT_EQ(newHorizonsSorcery::phantomArmyIntegrity(1292, 0, true), 323);
	EXPECT_EQ(newHorizonsSorcery::phantomArmyIntegrity(1000000, 1, true), 251875);
	EXPECT_THROW(newHorizonsSorcery::phantomArmyIntegrityBasisPoints(-1), std::invalid_argument);
	EXPECT_THROW(newHorizonsSorcery::phantomArmyIntegrity(-1, 0), std::invalid_argument);
}

TEST(NewHorizonsSorceryTest, PhantomDamageSplitIsNotACloneApproximation)
{
	EXPECT_EQ(newHorizonsSorcery::phantomArmyDamageTakenPercent(false), 25);
	EXPECT_EQ(newHorizonsSorcery::phantomArmyDamageTakenPercent(true), 200);
}

TEST(NewHorizonsSorceryTest, TimeStopRadiusUsesPowerThresholdAndChronomancerBonus)
{
	EXPECT_EQ(newHorizonsSorcery::timeStopRadius(0), 1);
	EXPECT_EQ(newHorizonsSorcery::timeStopRadius(99), 1);
	EXPECT_EQ(newHorizonsSorcery::timeStopRadius(100), 2);
	EXPECT_EQ(newHorizonsSorcery::timeStopRadius(999), 2);
	EXPECT_EQ(newHorizonsSorcery::timeStopRadius(0, true), 1);
	EXPECT_EQ(newHorizonsSorcery::timeStopRadius(100, true), 2);
	EXPECT_EQ(newHorizonsSorcery::timeStopRadius(200, true), 3);
	EXPECT_THROW(newHorizonsSorcery::timeStopRadius(-1), std::invalid_argument);
}

TEST(NewHorizonsSorceryTest, SpellLockDurationAndPolicyRespectTargetAlignment)
{
	EXPECT_EQ(newHorizonsSorcery::spellLockDuration(0), 1);
	EXPECT_EQ(newHorizonsSorcery::spellLockDuration(79), 1);
	EXPECT_EQ(newHorizonsSorcery::spellLockDuration(80), 2);
	EXPECT_EQ(newHorizonsSorcery::spellLockDuration(159), 2);
	EXPECT_EQ(newHorizonsSorcery::spellLockDuration(160), 3);
	EXPECT_EQ(newHorizonsSorcery::spellLockDuration(1000), 3);
	EXPECT_EQ(newHorizonsSorcery::spellLockDuration(0, true), 2);
	EXPECT_EQ(newHorizonsSorcery::spellLockDuration(160, true), 4);
	EXPECT_EQ(newHorizonsSorcery::spellLockDuration(1000, true), 4);
	EXPECT_THROW(newHorizonsSorcery::spellLockDuration(-1), std::invalid_argument);

	const auto friendly = newHorizonsSorcery::spellLockPolicy(true);
	EXPECT_FALSE(friendly.removeBeneficial);
	EXPECT_TRUE(friendly.removeHostile);
	EXPECT_TRUE(friendly.preserveBeneficial);
	EXPECT_FALSE(friendly.preserveHostile);
	EXPECT_TRUE(friendly.freezeTimedEffects);
	EXPECT_TRUE(friendly.blockFurtherMagic);
	EXPECT_FALSE(friendly.affectsOrders);

	const auto enemy = newHorizonsSorcery::spellLockPolicy(false);
	EXPECT_TRUE(enemy.removeBeneficial);
	EXPECT_FALSE(enemy.removeHostile);
	EXPECT_FALSE(enemy.preserveBeneficial);
	EXPECT_TRUE(enemy.preserveHostile);
	EXPECT_TRUE(enemy.freezeTimedEffects);
	EXPECT_TRUE(enemy.blockFurtherMagic);
	EXPECT_FALSE(enemy.affectsOrders);
}
