/*
 * NewHorizonsFlankReadbackTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../../client/battle/NewHorizonsBattleStatus.h"
#include "../../lib/battle/BattleHex.h"

TEST(NewHorizonsFlankReadbackTest, FormatsContributedSidesFromTheMarkedTargetsView)
{
	using newHorizonsFlankReadback::directionNames;
	using newHorizonsFlankReadback::formatDirections;
	const auto bit = [](BattleHex::EDir direction)
	{
		return static_cast<uint8_t>(1u << static_cast<unsigned>(direction));
	};

	EXPECT_TRUE(directionNames(0).empty());
	EXPECT_EQ(formatDirections(0), "none yet");

	const uint8_t allDirections = bit(BattleHex::TOP_LEFT) | bit(BattleHex::TOP_RIGHT)
		| bit(BattleHex::RIGHT) | bit(BattleHex::BOTTOM_RIGHT)
		| bit(BattleHex::BOTTOM_LEFT) | bit(BattleHex::LEFT);
	const std::vector<std::string_view> expectedAll{
		"top-left", "top-right", "right", "bottom-right", "bottom-left", "left"};
	EXPECT_EQ(directionNames(allDirections), expectedAll);
	EXPECT_EQ(formatDirections(allDirections), "top-left, top-right, right, bottom-right, bottom-left, left");

	const uint8_t partialMask = bit(BattleHex::TOP_RIGHT) | bit(BattleHex::BOTTOM_LEFT) | bit(BattleHex::LEFT);
	const std::vector<std::string_view> expectedPartial{"top-right", "bottom-left", "left"};
	EXPECT_EQ(directionNames(partialMask), expectedPartial);
	EXPECT_EQ(formatDirections(partialMask), "top-right, bottom-left, left");
	EXPECT_TRUE(directionNames(0xc0).empty()) << "Unassigned bits are not directions";
}
