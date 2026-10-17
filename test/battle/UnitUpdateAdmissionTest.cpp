/*
 * UnitUpdateAdmissionTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "../mock/mock_battle_Unit.h"
#include "../../lib/battle/UnitUpdateAdmission.h"
#include "../../lib/battle/UnitAdditionRules.h"
#include <limits>

namespace
{
using namespace ::testing;
using Gate = battle::UnitUpdateGate;
class GateUnit final : public UnitMock
{
public:
	std::vector<bool> stopped;
	mutable size_t queries = 0;
	bool isTimeStopped() const override { return stopped.at(queries++); }
};

TEST(UnitUpdateAdmissionTest, NonzeroHealthTimeStopReturnsAfterFirstQueryBeforeAlive)
{
	GateUnit unit;
	unit.stopped = {true};
	EXPECT_CALL(unit, alive()).Times(0);
	EXPECT_EQ(battle::classifyUnitUpdateGate(unit, -1), Gate::IGNORE_HEALTH_STOP);
	EXPECT_EQ(unit.queries, 1);
}

TEST(UnitUpdateAdmissionTest, ZeroHealthTimeStopStillIgnoresStateAfterSecondQuery)
{
	GateUnit unit;
	unit.stopped = {true, true};
	EXPECT_CALL(unit, alive()).Times(0);
	EXPECT_EQ(battle::classifyUnitUpdateGate(unit, 0), Gate::IGNORE_STATE_STOP);
	EXPECT_EQ(unit.queries, 2);
}

TEST(UnitUpdateAdmissionTest, DistinctSecondQueryCannotBeCollapsedOrMemoized)
{
	GateUnit unit;
	unit.stopped = {false, true};
	EXPECT_CALL(unit, alive()).Times(0);
	EXPECT_EQ(battle::classifyUnitUpdateGate(unit, 1), Gate::IGNORE_STATE_STOP);
	EXPECT_EQ(unit.queries, 2);
}

TEST(UnitUpdateAdmissionTest, DeadPositiveHealthAloneRequiresPlacementBeforeParsing)
{
	GateUnit unit;
	unit.stopped = {false, false};
	EXPECT_CALL(unit, alive()).WillOnce(Return(false));
	EXPECT_EQ(battle::classifyUnitUpdateGate(unit, 1), Gate::CHECK_RESURRECTION_SPACE);
}

TEST(UnitUpdateAdmissionTest, ZeroHealthStillReadsAliveInOriginalShortCircuitOrder)
{
	GateUnit unit;
	unit.stopped = {false, false};
	EXPECT_CALL(unit, alive()).WillOnce(Return(false));
	EXPECT_EQ(battle::classifyUnitUpdateGate(unit, 0), Gate::APPLY);
}

TEST(UnitUpdateAdmissionTest, LivePositiveHealthDoesNotRequirePlacement)
{
	GateUnit unit;
	unit.stopped = {false, false};
	EXPECT_CALL(unit, alive()).WillOnce(Return(true));
	EXPECT_EQ(battle::classifyUnitUpdateGate(unit, 1), Gate::APPLY);
}

TEST(UnitUpdateAdmissionTest, LegacyAllocationDoesNotEagerlyImposeTargetedIntegerBound)
{
	JsonNode rules;
	rules.Struct();
	const auto size = static_cast<size_t>(std::numeric_limits<int32_t>::max()) + 1;
	EXPECT_EQ(battle::nextUnitAdditionId(size, rules), static_cast<uint32_t>(size));
}
}
