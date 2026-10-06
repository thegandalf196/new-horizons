/*
 * BattleLuckReadbackTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "../StdInc.h"

#include "../../client/battle/NewHorizonsBattleStatus.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/modding/CModHandler.h"
#include "../server/battles/HeroCommandFixture.h"

namespace
{
using namespace newHorizonsBattleStatus;
}

class BattleLuckReadbackSharedQueryTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}
};

TEST(BattleLuckReadbackTest, DisabledSnapshotIsEmptyAndSignedValuesArePreserved)
{
	const auto disabled = makeBattleLuckReadback(false, -10, false, false, false, 0,
		{"Hero: -10 Luck"});
	EXPECT_EQ(disabled, BattleLuckReadback{});
	EXPECT_FALSE(disabled.active());

	const auto negative = makeBattleLuckReadback(true, -10, false, false, false, 0,
		{"Artifact: -10 Luck"});
	ASSERT_TRUE(negative.active());
	EXPECT_EQ(negative.ordinaryAttackLuck, -10);
	ASSERT_EQ(negative.bonusDescriptions.size(), 1u);

	const auto positive = makeBattleLuckReadback(true, 10, false, false, false, 0,
		{"Hero: +10 Luck"});
	ASSERT_TRUE(positive.active());
	EXPECT_EQ(positive.ordinaryAttackLuck, 10);
}

TEST(BattleLuckReadbackTest, NoLuckSuppressesRawLuckSources)
{
	const auto status = makeBattleLuckReadback(true, 0, true, false, false, 0,
		{"Artifact: +3 Luck", "Sylvan Luck: +2 Luck"});
	ASSERT_TRUE(status.active());
	EXPECT_EQ(status.ordinaryAttackLuck, 0);
	EXPECT_TRUE(status.noLuck);
	EXPECT_FALSE(status.maxLuck);
	EXPECT_TRUE(status.bonusDescriptions.empty());
	EXPECT_FALSE(status.hasSources());
}

TEST(BattleLuckReadbackTest, MaxLuckAndItsLimitRemainExplainedAfterTheSharedQuery)
{
	// MAX_LUCK overrides NO_LUCK in the shared calculation; MAXIMUM_LUCK can
	// still cap that result. Keep all three facts alongside the final value.
	const auto status = makeBattleLuckReadback(true, 3, true, true, true, 3,
		{"Artifact: +4 Luck"});
	ASSERT_TRUE(status.active());
	EXPECT_EQ(status.ordinaryAttackLuck, 3);
	EXPECT_TRUE(status.noLuck);
	EXPECT_TRUE(status.maxLuck);
	EXPECT_TRUE(status.maximumLuckLimitPresent);
	EXPECT_EQ(status.maximumLuckLimit, 3);
	EXPECT_TRUE(status.bonusDescriptions.empty());
	EXPECT_FALSE(status.hasSources());
}

TEST(BattleLuckReadbackTest, SourceChangesCompareUnequalAtTheSameEffectiveLuck)
{
	const auto artifactLuck = makeBattleLuckReadback(true, 2, false, false, false, 0,
		{"Artifact: +2 Luck"});
	const auto heroLuck = makeBattleLuckReadback(true, 2, false, false, false, 0,
		{"Hero: +2 Luck"});
	ASSERT_EQ(artifactLuck.ordinaryAttackLuck, heroLuck.ordinaryAttackLuck);
	EXPECT_NE(artifactLuck, heroLuck)
		<< "A changed source list must invalidate the cached Luck help even when its value is unchanged";
}

TEST(BattleLuckReadbackTest, BonusDescriptionCacheKeyTracksUnitTreeVersionAndCallback)
{
	const int callbackIdentity = 0;
	const int otherCallbackIdentity = 0;
	const BattleBonusDescriptionCacheKey original{42, 7, &callbackIdentity};
	const BattleBonusDescriptionCacheKey unchanged{42, 7, &callbackIdentity};
	const BattleBonusDescriptionCacheKey otherUnit{43, 7, &callbackIdentity};
	const BattleBonusDescriptionCacheKey otherTreeVersion{42, 8, &callbackIdentity};
	const BattleBonusDescriptionCacheKey otherCallback{42, 7, &otherCallbackIdentity};

	EXPECT_EQ(original, unchanged);
	EXPECT_NE(original, otherUnit);
	EXPECT_NE(original, otherTreeVersion);
	EXPECT_NE(original, otherCallback);
}

TEST_F(BattleLuckReadbackSharedQueryTest, OrdinarySharedQueryHonorsNoLuckOverrideAndMaximumCap)
{
	startGame();
	startBattle();
	auto * unit = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
	ASSERT_NE(unit, nullptr);
	auto ordinaryLuck = [this, unit]()
	{
		return battle()->battleGetAttackLuck(unit, nullptr, false, false);
	};
	ASSERT_EQ(ordinaryLuck(), 0);

	unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::LUCK,
		BonusSource::OTHER, 2, BonusSourceID()));
	EXPECT_EQ(ordinaryLuck(), 2);

	unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::NO_LUCK,
		BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_EQ(ordinaryLuck(), 0);

	const auto maximum = static_cast<int32_t>(battle()->getLuckRollRules().goodChance.size());
	ASSERT_GE(maximum, 3);
	unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MAX_LUCK,
		BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_EQ(ordinaryLuck(), maximum)
		<< "MAX_LUCK takes precedence over NO_LUCK when queried through the shared battle calculation";

	unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MAXIMUM_LUCK,
		BonusSource::OTHER, 3, BonusSourceID()));
	EXPECT_EQ(ordinaryLuck(), 3)
		<< "The shared MAXIMUM_LUCK cap still limits the forced MAX_LUCK result";
}
