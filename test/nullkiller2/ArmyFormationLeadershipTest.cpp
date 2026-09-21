/*
 * ArmyFormationLeadershipTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of the GNU General Public License can be found in license.txt
 */
#include "StdInc.h"

#include "AI/Nullkiller2/Helpers/ArmyFormation.h"
#include "nullkiller2/NullkillerTest.h"

#include "lib/mapObjects/CGHeroInstance.h"

namespace
{
const PlayerColor PLAYER(0);
const int3 HERO_POS(5, 5, 0);

class NewHorizonsArmyFormationLeadershipTest : public NullkillerTest
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
	}

	void startGame(std::vector<std::pair<CreatureID, uint16_t>> heroArmy)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder
			.size(36, false)
			.playerActive(PLAYER)
			.hero(HERO_POS, HeroTypeID(0), PLAYER)
			.heroGarrison(std::move(heroArmy));
		startWithMap(std::move(builder));
	}
};
}

TEST_F(NewHorizonsArmyFormationLeadershipTest, MergePreflightRejectsAnOversizedIncomingStack)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	startGame({{pikeman, 17}});

	const auto * destination = findHeroAt(HERO_POS);
	ASSERT_NE(destination, nullptr);
	const auto capacity = destination->getLeadershipSlotCapacity(pikeman);
	ASSERT_TRUE(capacity);
	ASSERT_EQ(capacity->maximum, 17);

	CGHeroInstance source(nullptr);
	ASSERT_TRUE(source.setCreature(SlotID(0), pikeman, 1));

	EXPECT_FALSE(NK2AI::armyFormation::canMergeOrSwapStacks(
		&source, destination, SlotID(0), SlotID(0)));
	EXPECT_TRUE(NK2AI::armyFormation::canReceiveStack(destination, pikeman, capacity->maximum));
	EXPECT_FALSE(NK2AI::armyFormation::canReceiveStack(destination, pikeman, capacity->maximum + 1));
}

TEST_F(NewHorizonsArmyFormationLeadershipTest, SplitPreflightAllowsOnlyTheLegalFinalDestinationCount)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	startGame({{pikeman, 16}});

	const auto * destination = findHeroAt(HERO_POS);
	ASSERT_NE(destination, nullptr);
	const auto capacity = destination->getLeadershipSlotCapacity(pikeman);
	ASSERT_TRUE(capacity);

	CGHeroInstance source(nullptr);
	ASSERT_TRUE(source.setCreature(SlotID(0), pikeman, 4));
	const auto destinationSlot = destination->getSlotFor(pikeman);
	ASSERT_TRUE(destinationSlot.validSlot());

	EXPECT_EQ(NK2AI::armyFormation::maxLegalTransferCount(
		&source, destination, SlotID(0), destinationSlot), 1);
	EXPECT_TRUE(NK2AI::armyFormation::canSplitStack(
		&source, destination, SlotID(0), destinationSlot, 17));
	EXPECT_FALSE(NK2AI::armyFormation::canSplitStack(
		&source, destination, SlotID(0), destinationSlot, 18));
}

TEST_F(NewHorizonsArmyFormationLeadershipTest, LegacyHeroesRemainUnrestrictedByThePreflight)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	const CreatureID archer(CreatureID::decode("core:archer"));
	CGHeroInstance destination(nullptr);
	CGHeroInstance source(nullptr);
	ASSERT_TRUE(destination.setCreature(SlotID(0), pikeman, 100));
	ASSERT_TRUE(source.setCreature(SlotID(0), pikeman, 100));
	ASSERT_TRUE(source.setCreature(SlotID(1), archer, 1));

	EXPECT_TRUE(NK2AI::armyFormation::canMergeOrSwapStacks(
		&source, &destination, SlotID(0), SlotID(0)));
}

TEST_F(NewHorizonsArmyFormationLeadershipTest, SwapPreflightHandlesEmptySlotsAndLastStackProtection)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	const CreatureID archer(CreatureID::decode("core:archer"));
	startGame({{pikeman, 16}});

	const auto * destination = findHeroAt(HERO_POS);
	ASSERT_NE(destination, nullptr);
	CGHeroInstance source(nullptr);
	ASSERT_TRUE(source.setCreature(SlotID(0), pikeman, 1));
	ASSERT_TRUE(source.setCreature(SlotID(1), archer, 1));

	EXPECT_TRUE(NK2AI::armyFormation::canSwapStacks(
		&source, destination, SlotID(0), SlotID(1)));

	CGHeroInstance oversizedSource(nullptr);
	ASSERT_TRUE(oversizedSource.setCreature(SlotID(0), pikeman, 2));
	EXPECT_FALSE(NK2AI::armyFormation::canSwapStacks(
		&oversizedSource, destination, SlotID(0), SlotID(1)));

	CGHeroInstance lastStackSource(nullptr);
	ASSERT_TRUE(lastStackSource.setCreature(SlotID(0), pikeman, 1));
	EXPECT_FALSE(NK2AI::armyFormation::canSwapStacks(
		&lastStackSource, destination, SlotID(0), SlotID(1)));
}
