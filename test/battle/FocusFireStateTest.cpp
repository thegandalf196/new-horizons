/*
 * FocusFireStateTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/FocusFireState.h"

namespace
{
FocusFireState mark()
{
	FocusFireState result;
	result.targetUnitId = 2;
	result.recipientUnitIds = {0, 1};
	result.issuedRound = 3;
	result.rangedDamagePercent = 30;
	return result;
}
}

TEST(FocusFireState, ValidShapeKeepsExactIdsAndValueCopy)
{
	const auto original = mark();
	EXPECT_NO_THROW(original.validateShape());
	auto changed = original;
	changed.recipientUnitIds.pop_back();
	EXPECT_NE(original, changed);
	EXPECT_EQ(original.recipientUnitIds, (std::vector<uint32_t>{0, 1}));
}

TEST(FocusFireState, RejectsEmptyDuplicateUnsortedAndTargetInCohort)
{
	for(const auto & ids : std::vector<std::vector<uint32_t>>{{}, {0, 0}, {1, 0}, {0, 2}})
	{
		auto invalid = mark();
		invalid.recipientUnitIds = ids;
		EXPECT_THROW(invalid.validateShape(), std::runtime_error);
	}
}

TEST(FocusFireState, RejectsInvalidWireIdRoundAndPremium)
{
	auto invalid = mark();
	invalid.targetUnitId = std::numeric_limits<uint32_t>::max();
	EXPECT_THROW(invalid.validateShape(), std::runtime_error);
	invalid = mark();
	invalid.recipientUnitIds = {std::numeric_limits<uint32_t>::max()};
	EXPECT_THROW(invalid.validateShape(), std::runtime_error);
	invalid = mark();
	invalid.issuedRound = 0;
	EXPECT_THROW(invalid.validateShape(), std::runtime_error);
	for(const int value : {-91, 201})
	{
		invalid = mark();
		invalid.rangedDamagePercent = value;
		EXPECT_THROW(invalid.validateShape(), std::runtime_error);
	}
}

TEST(FocusFireState, TargetedFactoryUsesUnitIdentityAndInvalidHexNotSpell)
{
	const auto action = BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE, 0);
	ASSERT_EQ(action.target.size(), 1);
	EXPECT_EQ(action.actionType, EActionType::HERO_COMMAND);
	EXPECT_EQ(action.command, HeroCommand::FOCUS_FIRE);
	EXPECT_FALSE(action.spell.hasValue());
	EXPECT_EQ(action.target.front().unitValue, 0);
	EXPECT_EQ(action.target.front().hexValue, BattleHex::INVALID);
}

TEST(FocusFireState, TargetedFactoryRejectsNonTargetedCommandSideAndUnrepresentableId)
{
	EXPECT_THROW(BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE, 0),
		std::invalid_argument);
	EXPECT_THROW(BattleAction::makeTargetedHeroCommand(BattleSide::NONE, HeroCommand::FOCUS_FIRE, 0),
		std::invalid_argument);
	EXPECT_THROW(BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE,
		std::numeric_limits<uint32_t>::max()), std::invalid_argument);
}
