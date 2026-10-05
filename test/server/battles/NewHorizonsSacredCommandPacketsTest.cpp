/*
 * NewHorizonsSacredCommandPacketsTest.cpp, part of VCMI engine
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
HeroOrderState chargeOrder(int32_t sacredCommandPercent)
{
	HeroOrderState result;
	result.command = HeroCommand::CHARGE;
	result.issuedRound = 3;
	result.sacredCommandEfficiencyBonusPercent = sacredCommandPercent;
	return result;
}

void useCurrentVersion(CMemorySerializer & serializer)
{
	serializer.oser.version = ESerializationVersion::CURRENT;
	serializer.iser.version = ESerializationVersion::CURRENT;
}

constexpr auto previousVersion = ESerializationVersion::NEW_HORIZONS_ELEMENTAL_REBIRTH;
}

TEST(NewHorizonsSacredCommandPacketsTest, DirectStateRoundTripsAndOlderReadersDefaultToNoBonus)
{
	const auto sacredOrder = chargeOrder(10);
	CMemorySerializer current;
	useCurrentVersion(current);
	ASSERT_NO_THROW(current.oser & sacredOrder);
	HeroOrderState restoredCurrent;
	ASSERT_NO_THROW(current.iser & restoredCurrent);
	EXPECT_EQ(restoredCurrent, sacredOrder);

	const auto ordinaryOrder = chargeOrder(0);
	CMemorySerializer legacy;
	legacy.oser.version = previousVersion;
	legacy.iser.version = previousVersion;
	ASSERT_NO_THROW(legacy.oser & ordinaryOrder);
	HeroOrderState restoredLegacy;
	ASSERT_NO_THROW(legacy.iser & restoredLegacy);
	EXPECT_EQ(restoredLegacy.sacredCommandEfficiencyBonusPercent, 0);
	EXPECT_EQ(restoredLegacy.command, HeroCommand::CHARGE);

	CMemorySerializer unsupported;
	unsupported.oser.version = previousVersion;
	EXPECT_THROW(unsupported.oser & sacredOrder, std::runtime_error);
	EXPECT_TRUE(unsupported.extractBuffer().empty())
		<< "An older format rejects the populated descriptor before writing any state bytes";
}

TEST(NewHorizonsSacredCommandPacketsTest, EnclosingAcceptedOrderPacketsRoundTripAndRejectUnsupportedDownsave)
{
	const auto order = chargeOrder(10);

	StartAction startAction(BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
	startAction.battleID = BattleID(1);
	startAction.orderState = order;
	CMemorySerializer startWire;
	useCurrentVersion(startWire);
	ASSERT_NO_THROW(startWire.oser & startAction);
	StartAction restoredStart;
	ASSERT_NO_THROW(startWire.iser & restoredStart);
	ASSERT_TRUE(restoredStart.orderState);
	EXPECT_EQ(restoredStart.orderState->sacredCommandEfficiencyBonusPercent, 10);

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
	EXPECT_EQ(restoredUpdate.state->sacredCommandEfficiencyBonusPercent, 10);
	EXPECT_EQ(restoredUpdate.states->front().sacredCommandEfficiencyBonusPercent, 10);

	CMemorySerializer unsupportedStart;
	unsupportedStart.oser.version = previousVersion;
	EXPECT_THROW(unsupportedStart.oser & startAction, std::runtime_error);
	EXPECT_TRUE(unsupportedStart.extractBuffer().empty());

	CMemorySerializer unsupportedUpdate;
	unsupportedUpdate.oser.version = previousVersion;
	EXPECT_THROW(unsupportedUpdate.oser & update, std::runtime_error);
	EXPECT_TRUE(unsupportedUpdate.extractBuffer().empty());
}

TEST(NewHorizonsSacredCommandPacketsTest, OnlyZeroAndTenAreValidSnapshots)
{
	for(const int32_t invalidPercent : {-10, 1, 9, 11, 20})
	{
		SCOPED_TRACE(invalidPercent);
		const auto invalid = chargeOrder(invalidPercent);
		CMemorySerializer wire;
		useCurrentVersion(wire);
		EXPECT_THROW(wire.oser & invalid, std::runtime_error);
		EXPECT_TRUE(wire.extractBuffer().empty())
			<< "Invalid Sacred Command snapshots are rejected before output bytes";
	}
}
