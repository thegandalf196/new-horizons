/*
 * NewHorizonsSkeletonRecoverySerializationTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../lib/entities/hero/NewHorizonsNecromancy.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/serializer/CMemorySerializer.h"

#include <algorithm>

namespace
{
newHorizonsNecromancy::NecromancyResult ownRecovery()
{
	newHorizonsNecromancy::NecromancyResult value;
	value.active = true;
	value.rank = 2;
	value.recoveredSkeletons = 3;
	value.recoveredSkeletonWarriors = 4;
	return value;
}
}

TEST(NewHorizonsSkeletonRecoverySerialization, CurrentCodecKeepsOwnFormsWithoutFreshOutputOrMana)
{
	auto value = ownRecovery();
	CMemorySerializer wire;
	ASSERT_NO_THROW(value.serialize(wire.oser));
	newHorizonsNecromancy::NecromancyResult restored;
	ASSERT_NO_THROW(restored.serialize(wire.iser));
	EXPECT_EQ(restored.recoveredSkeletons, 3);
	EXPECT_EQ(restored.recoveredSkeletonWarriors, 4);
	EXPECT_EQ(restored.skeletonsRaised + restored.zombiesRaised + restored.wightsRaised, 0);
	EXPECT_EQ(restored.manaRecovered, 0);
	EXPECT_FALSE(restored.empty());
}

TEST(NewHorizonsSkeletonRecoverySerialization, PreviousWritersRejectBeforeDirectAndPacketPrefixes)
{
	auto value = ownRecovery();
	CMemorySerializer direct;
	direct.oser.version = ESerializationVersion::NEW_HORIZONS_REVISED_ORDERS;
	EXPECT_THROW(value.serialize(direct.oser), std::runtime_error);
	EXPECT_TRUE(direct.extractBuffer().empty());
	BattleResultsApplied packet;
	packet.battleID = BattleID(0);
	packet.necromancy = value;
	CMemorySerializer envelope;
	envelope.oser.version = ESerializationVersion::NEW_HORIZONS_REVISED_ORDERS;
	EXPECT_THROW(packet.serialize(envelope.oser), std::runtime_error);
	EXPECT_TRUE(envelope.extractBuffer().empty());
}

TEST(NewHorizonsSkeletonRecoverySerialization, PreviousReaderClearsReusedRecoveryWithoutLosingOldFields)
{
	newHorizonsNecromancy::NecromancyResult legacy;
	legacy.active = true;
	legacy.rank = 2;
	legacy.skeletonsOffered = legacy.skeletonsRaised = 10;
	CMemorySerializer wire;
	wire.oser.version = wire.iser.version = ESerializationVersion::NEW_HORIZONS_REVISED_ORDERS;
	ASSERT_NO_THROW(legacy.serialize(wire.oser));
	auto reused = ownRecovery();
	ASSERT_NO_THROW(reused.serialize(wire.iser));
	EXPECT_EQ(reused.recoveredSkeletons + reused.recoveredSkeletonWarriors, 0);
	EXPECT_EQ(reused.skeletonsRaised, 10);
}

TEST(NewHorizonsSkeletonRecoverySerialization, NegativeAndInactiveRecoveryReceiptsAreRejected)
{
	auto value = ownRecovery();
	value.recoveredSkeletons = -1;
	CMemorySerializer invalid;
	EXPECT_THROW(value.serialize(invalid.oser), std::runtime_error);
	EXPECT_TRUE(invalid.extractBuffer().empty());
	value = ownRecovery();
	value.active = false;
	EXPECT_THROW(value.serialize(invalid.oser), std::runtime_error);
	EXPECT_TRUE(invalid.extractBuffer().empty());

	value = ownRecovery();
	CMemorySerializer good;
	ASSERT_NO_THROW(value.serialize(good.oser));
	auto bytes = good.extractBuffer();
	CMemorySerializer negative;
	int32_t invalidCount = -1;
	negative.oser & invalidCount;
	const auto suffix = negative.extractBuffer();
	ASSERT_GE(bytes.size(), suffix.size());
	std::copy(suffix.begin(), suffix.end(), bytes.end() - suffix.size());
	CMemorySerializer forged(bytes);
	newHorizonsNecromancy::NecromancyResult rejected;
	EXPECT_THROW(rejected.serialize(forged.iser), std::runtime_error);
}
