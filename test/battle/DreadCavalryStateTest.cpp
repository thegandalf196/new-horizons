/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/battle/DreadCavalryState.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/serializer/CMemorySerializer.h"
namespace
{
battle::DreadCavalryState grant(int remaining = 6, int shared = 6, int vanish = 0)
{
	battle::DreadCavalryState state;
	state.arm(7, BattleSide::ATTACKER, remaining, 50, shared, vanish);
	return state;
}
}
TEST(DreadCavalryState, OncePerActivationAndDeclineRetainsUsed)
{
	auto state = grant();
	EXPECT_THROW(state.arm(8, BattleSide::ATTACKER, 3, 50, 3, 0), std::runtime_error);
	state.retirePending();
	EXPECT_TRUE(state.usedThisActivation);
	EXPECT_FALSE(state.pending);
	EXPECT_THROW(state.arm(8, BattleSide::ATTACKER, 3, 50, 3, 0), std::runtime_error);
	EXPECT_NO_THROW(state.validateShape());
}
TEST(DreadCavalryState, PursuitConsumesSharedUnusedMovementOnlyOnce)
{
	const auto state = grant();
	EXPECT_EQ(state.legacyTailAfterMovement(0), 6);
	EXPECT_EQ(state.legacyTailAfterMovement(2), 4);
	EXPECT_EQ(state.legacyTailAfterMovement(6), 0);
	EXPECT_EQ(state.legacyTailAfterMovement(9), 0);
}
TEST(DreadCavalryState, VanishIsIndependentAndMixedMergeUsesMaximum)
{
	EXPECT_EQ(grant(6, 0, 4).legacyTailAfterMovement(2), 4);
	EXPECT_EQ(grant(6, 6, 4).legacyTailAfterMovement(1), 5);
	EXPECT_EQ(grant(6, 6, 4).legacyTailAfterMovement(5), 4);
	EXPECT_EQ(grant(6, 2, 0).legacyTailAfterMovement(5), 1);
	EXPECT_EQ(grant(0, 0, 0).legacyTailAfterMovement(0), 0);
	EXPECT_THROW(grant().legacyTailAfterMovement(-1), std::runtime_error);
}
TEST(DreadCavalryState, StrictJsonRoundTripsWithoutInventingHistoricalState)
{
	const auto state = grant();
	EXPECT_EQ(battle::DreadCavalryState::fromJson(state.toJson()), state);
	EXPECT_FALSE(battle::DreadCavalryState::fromJson(JsonNode()).hasState());
	auto malformed = state.toJson();
	malformed["damagePercent"].Integer() = 0;
	EXPECT_THROW(battle::DreadCavalryState::fromJson(malformed), std::runtime_error);
	malformed = state.toJson();
	malformed["unknown"].Bool() = true;
	EXPECT_THROW(battle::DreadCavalryState::fromJson(malformed), std::runtime_error);
}
TEST(DreadCavalryState, CurrentWireRoundTripAndOldWriterRefusesBeforePrefix)
{
	auto state = grant();
	CMemorySerializer bytes;
	bytes.oser & state;
	battle::DreadCavalryState decoded;
	bytes.iser & decoded;
	EXPECT_EQ(decoded, state);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_RECRUITMENT_GROWTH;
	EXPECT_THROW(old.oser & state, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
}
TEST(DreadCavalryState, ReverseAttackDoesNotInheritAcceptedActionOutput)
{
	BattleAttackInfo attack(nullptr, nullptr, 0, false);
	attack.dreadCavalryDamagePercent = 50;
	const auto reversed = attack.reverse();
	EXPECT_TRUE(reversed.retaliation);
	EXPECT_EQ(reversed.dreadCavalryDamagePercent, 100);
}

TEST(DreadCavalryState, OldReaderClearsPreviouslyUsedDestination)
{
	battle::DreadCavalryState neutral;
	CMemorySerializer bytes;
	bytes.oser.version = ESerializationVersion::NEW_HORIZONS_RECRUITMENT_GROWTH;
	bytes.iser.version = bytes.oser.version;
	bytes.oser & neutral;
	auto decoded = grant();
	bytes.iser & decoded;
	EXPECT_FALSE(decoded.hasState());
}
TEST(DreadCavalryState, UnitAndOuterPacketPreflightRejectOldWriterBeforePrefix)
{
	UnitChanges change(3, UnitChanges::EOperation::UPDATE);
	change.data["state"]["dreadCavalry"] = grant().toJson();
	BattleUnitsChanged packet;
	packet.battleID = BattleID(0);
	packet.changedStacks.push_back(change);
	CMemorySerializer bytes;
	bytes.oser.version = ESerializationVersion::NEW_HORIZONS_RECRUITMENT_GROWTH;
	EXPECT_THROW(bytes.oser & packet, std::runtime_error);
	EXPECT_TRUE(bytes.extractBuffer().empty());
}

namespace
{
std::unique_ptr<BattleInfo> serializableBattleInfo(BattleID id)
{
	// Same valid descriptor identity as BattleActionExportTest; no combat effect.
	auto info = std::make_unique<BattleInfo>(nullptr);
	info->battleID = id;
	info->replayAllowed = false;
	info->battlefieldType = BattleField(BattleField::decode("core:sand_shore"));
	info->terrainType = TerrainId(TerrainId::GRASS);
	info->tile = int3(4, 4, 0);
	info->getSide(BattleSide::ATTACKER).color = PlayerColor(0);
	info->getSide(BattleSide::DEFENDER).color = PlayerColor(1);
	return info;
}
BattleStart dreadBattleEntry(bool pending)
{
	BattleStart entry;
	entry.battleID = BattleID(7);
	entry.info = serializableBattleInfo(entry.battleID);
	CStackBasicDescriptor descriptor(CreatureID(0), 1);
	entry.info->stacks.push_back(std::make_unique<CStack>(&descriptor, PlayerColor(0), 41, BattleSide::ATTACKER));
	entry.info->stacks.push_back(std::make_unique<CStack>(&descriptor, PlayerColor(1), 42, BattleSide::DEFENDER));
	entry.info->activeStack = 41;
	auto & state = entry.info->stacks.front()->dreadCavalryState;
	state.arm(42, BattleSide::ATTACKER, 6, 50, 4, 2);
	if(!pending)
		state.retirePending();
	return entry;
}
}
TEST(DreadCavalryState, BattleStartUsedOnlyRejects1139BeforeAnyOuterPrefix)
{
	auto entry = dreadBattleEntry(false);
	const auto before = entry.info->stacks.front()->dreadCavalryState;
	ASSERT_TRUE(before.usedThisActivation);
	ASSERT_FALSE(before.pending);
	ASSERT_NO_THROW(entry.info->validateDreadCavalryStateSerialization(true));
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_AERIAL_SUPREMACY;
	EXPECT_THROW(entry.serialize(old.oser), std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	EXPECT_EQ(entry.info->stacks.front()->dreadCavalryState, before);
}
TEST(DreadCavalryState, BattleStartPendingActiveOwnerAndVictimReject1139BeforeAnyOuterPrefix)
{
	auto entry = dreadBattleEntry(true);
	const auto before = entry.info->stacks.front()->dreadCavalryState;
	ASSERT_TRUE(before.pending);
	ASSERT_EQ(entry.info->getActiveStackID(), 41);
	ASSERT_NE(entry.info->getStack(before.victimId, false), nullptr);
	ASSERT_NO_THROW(entry.info->validateDreadCavalryStateSerialization(true));
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_AERIAL_SUPREMACY;
	EXPECT_THROW(entry.serialize(old.oser), std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	EXPECT_EQ(entry.info->stacks.front()->dreadCavalryState, before);
}
TEST(DreadCavalryState, BattleStartSupportedCurrentWritesBothPendingAndUsedOnly)
{
	for(const bool pending : {false, true})
	{
		auto entry = dreadBattleEntry(pending);
		const auto before = entry.info->stacks.front()->dreadCavalryState;
		CMemorySerializer current;
		ASSERT_NO_THROW(entry.serialize(current.oser));
		EXPECT_FALSE(current.extractBuffer().empty());
		EXPECT_EQ(entry.info->stacks.front()->dreadCavalryState, before);
	}
}
TEST(DreadCavalryState, BattleStartDefaultAndNullInfoRemainWritableAt1139)
{
	for(const bool withInfo : {false, true})
	{
		BattleStart entry;
		entry.battleID = BattleID(7);
		if(withInfo)
			entry.info = serializableBattleInfo(entry.battleID);
		CMemorySerializer old;
		old.oser.version = ESerializationVersion::NEW_HORIZONS_AERIAL_SUPREMACY;
		ASSERT_NO_THROW(entry.serialize(old.oser));
		EXPECT_FALSE(old.extractBuffer().empty());
	}
}
