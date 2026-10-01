/*
 * BattleFormAccessibilityTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../../lib/battle/AccessibilityInfo.h"
#include "../../lib/battle/Unit.h"

namespace
{
AccessibilityInfo openField()
{
	AccessibilityInfo result;
	result.fill(EAccessibility::ACCESSIBLE);
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		if(!BattleHex(static_cast<si16>(index)).isAvailable())
			result[index] = EAccessibility::SIDE_COLUMN;
	return result;
}
}

TEST(BattleFormAccessibilityTest, KeepsAnchorWhenBothHexesFit)
{
	const auto field = openField();
	const BattleHex origin(8, 5);
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		EXPECT_EQ(field.nearestLegalPosition(origin, true, side), origin);
}

TEST(BattleFormAccessibilityTest, RelocatesRatherThanOverlappingNewFootprint)
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		auto field = openField();
		const BattleHex origin(8, 5);
		const auto blocked = battle::Unit::occupiedHex(origin, true, side);
		field[blocked.toInt()] = EAccessibility::ALIVE_STACK;
		const auto destination = field.nearestLegalPosition(origin, true, side);
		ASSERT_TRUE(destination);
		EXPECT_NE(*destination, origin);
		EXPECT_TRUE(field.accessible(*destination, true, side));
		EXPECT_EQ(BattleHex::getDistance(origin, *destination), 1);
		EXPECT_EQ(field[blocked.toInt()], EAccessibility::ALIVE_STACK);
	}
}

TEST(BattleFormAccessibilityTest, RespectsGateReservationsAndStableTies)
{
	auto field = openField();
	const BattleHex origin(8, 5);
	field.reserveDemonicGateFootprint(origin, true, BattleSide::ATTACKER);
	const auto destination = field.nearestLegalPosition(origin, true, BattleSide::ATTACKER);
	ASSERT_TRUE(destination);
	EXPECT_FALSE(field.isDemonicGateReserved(*destination));
	EXPECT_FALSE(field.isDemonicGateReserved(battle::Unit::occupiedHex(*destination, true, BattleSide::ATTACKER)));
	EXPECT_EQ(field.nearestLegalPosition(origin, true, BattleSide::ATTACKER), destination);
	for(int index = 0; index < destination->toInt(); ++index)
	{
		const BattleHex candidate(static_cast<si16>(index));
		if(candidate.isAvailable() && field.accessible(candidate, true, BattleSide::ATTACKER))
			EXPECT_GT(BattleHex::getDistance(origin, candidate), BattleHex::getDistance(origin, *destination));
	}
}

TEST(BattleFormAccessibilityTest, FailsClosedWhenNoLegalFootprintExists)
{
	AccessibilityInfo field;
	field.fill(EAccessibility::OBSTACLE);
	EXPECT_FALSE(field.nearestLegalPosition(BattleHex(8, 5), true, BattleSide::ATTACKER));
	EXPECT_FALSE(field.nearestLegalPosition(BattleHex::INVALID, false, BattleSide::ATTACKER));
	EXPECT_FALSE(field.nearestLegalPosition(BattleHex(8, 5), false, BattleSide::NONE));
}

TEST(BattleFormAccessibilityTest, CannotUseAnEnemyMarkedDestructibleByPathfinding)
{
	auto field = openField();
	const BattleHex origin(8, 5);
	const auto blocked = battle::Unit::occupiedHex(origin, true, BattleSide::ATTACKER);
	field[blocked.toInt()] = EAccessibility::ALIVE_STACK;
	auto turns = std::make_shared<TBattlefieldTurnsArray>();
	turns->fill(0);
	field.destructibleEnemyTurns = turns;
	ASSERT_TRUE(field.accessible(origin, true, BattleSide::ATTACKER));
	const auto destination = field.nearestLegalPosition(origin, true, BattleSide::ATTACKER);
	ASSERT_TRUE(destination);
	EXPECT_NE(*destination, origin);
	EXPECT_NE(battle::Unit::occupiedHex(*destination, true, BattleSide::ATTACKER), blocked);
	EXPECT_NE(*destination, blocked);
}
