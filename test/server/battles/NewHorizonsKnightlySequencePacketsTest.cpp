/*
 * NewHorizonsKnightlySequencePacketsTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "../../../lib/battle/HeroCommand.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"

namespace
{
HeroOrderState chargeOrder(int32_t sacredPercent, int32_t knightlyPercent)
{
	HeroOrderState result;
	result.command = HeroCommand::CHARGE;
	result.issuedRound = 3;
	result.sacredCommandEfficiencyBonusPercent = sacredPercent;
	result.knightlySequenceEfficiencyBonusPercent = knightlyPercent;
	return result;
}

void useCurrentVersion(CMemorySerializer & serializer)
{
	serializer.oser.version = ESerializationVersion::CURRENT;
	serializer.iser.version = ESerializationVersion::CURRENT;
}

constexpr auto previousVersion = ESerializationVersion::NEW_HORIZONS_SACRED_COMMAND;
}

TEST(NewHorizonsKnightlySequencePacketsTest, DirectOrderSnapshotRoundTripsAndOlderReadersDefaultKnightlyToZero)
{
	const auto combinedOrder = chargeOrder(10, 5);
	EXPECT_EQ(combinedOrder.divineMandateEfficiencyBonusPercent(), 15);
	CMemorySerializer current;
	useCurrentVersion(current);
	ASSERT_NO_THROW(current.oser & combinedOrder);
	HeroOrderState restoredCurrent;
	ASSERT_NO_THROW(current.iser & restoredCurrent);
	EXPECT_EQ(restoredCurrent, combinedOrder);
	EXPECT_EQ(restoredCurrent.divineMandateEfficiencyBonusPercent(), 15);

	const auto sacredOnlyOrder = chargeOrder(10, 0);
	CMemorySerializer older;
	older.oser.version = previousVersion;
	older.iser.version = previousVersion;
	ASSERT_NO_THROW(older.oser & sacredOnlyOrder);
	HeroOrderState restoredOlder;
	ASSERT_NO_THROW(older.iser & restoredOlder);
	EXPECT_EQ(restoredOlder.sacredCommandEfficiencyBonusPercent, 10);
	EXPECT_EQ(restoredOlder.knightlySequenceEfficiencyBonusPercent, 0);
	EXPECT_EQ(restoredOlder.divineMandateEfficiencyBonusPercent(), 10);

	CMemorySerializer unsupported;
	unsupported.oser.version = previousVersion;
	EXPECT_THROW(unsupported.oser & combinedOrder, std::runtime_error);
	EXPECT_TRUE(unsupported.extractBuffer().empty())
		<< "Older formats refuse the Knightly snapshot before output bytes are written";
}

TEST(NewHorizonsKnightlySequencePacketsTest, AcceptedOrderPacketsPreserveAndRejectLossySnapshots)
{
	const auto order = chargeOrder(10, 5);
	StartAction startAction(BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
	startAction.battleID = BattleID(1);
	startAction.orderState = order;
	CMemorySerializer startWire;
	useCurrentVersion(startWire);
	ASSERT_NO_THROW(startWire.oser & startAction);
	StartAction restoredStart;
	ASSERT_NO_THROW(startWire.iser & restoredStart);
	ASSERT_TRUE(restoredStart.orderState);
	EXPECT_EQ(restoredStart.orderState->divineMandateEfficiencyBonusPercent(), 15);
	EXPECT_EQ(restoredStart.orderState->knightlySequenceEfficiencyBonusPercent, 5);

	BattleHeroOrderStateChanged update;
	update.battleID = BattleID(1);
	update.side = BattleSide::ATTACKER;
	update.state = order;
	update.states = std::vector<HeroOrderState>{order};
	CMemorySerializer updateWire;
	useCurrentVersion(updateWire);
	ASSERT_NO_THROW(updateWire.oser & update);
	BattleHeroOrderStateChanged restoredUpdate;
	ASSERT_NO_THROW(updateWire.iser & restoredUpdate);
	ASSERT_TRUE(restoredUpdate.state);
	ASSERT_TRUE(restoredUpdate.states);
	ASSERT_EQ(restoredUpdate.states->size(), 1u);
	EXPECT_EQ(restoredUpdate.state->knightlySequenceEfficiencyBonusPercent, 5);
	EXPECT_EQ(restoredUpdate.states->front().divineMandateEfficiencyBonusPercent(), 15);

	CMemorySerializer unsupportedStart;
	unsupportedStart.oser.version = previousVersion;
	EXPECT_THROW(unsupportedStart.oser & startAction, std::runtime_error);
	EXPECT_TRUE(unsupportedStart.extractBuffer().empty());

	CMemorySerializer unsupportedUpdate;
	unsupportedUpdate.oser.version = previousVersion;
	EXPECT_THROW(unsupportedUpdate.oser & update, std::runtime_error);
	EXPECT_TRUE(unsupportedUpdate.extractBuffer().empty());
}

TEST(NewHorizonsKnightlySequencePacketsTest, OnlyZeroOrFiveKnightlySnapshotsAreValid)
{
	for(const int32_t invalidPercent : {-5, 1, 4, 6, 10})
	{
		SCOPED_TRACE(invalidPercent);
		const auto invalid = chargeOrder(0, invalidPercent);
		CMemorySerializer wire;
		useCurrentVersion(wire);
		EXPECT_THROW(wire.oser & invalid, std::runtime_error);
		EXPECT_TRUE(wire.extractBuffer().empty())
			<< "Invalid Knightly Sequence snapshots are rejected before output bytes";
	}
}
