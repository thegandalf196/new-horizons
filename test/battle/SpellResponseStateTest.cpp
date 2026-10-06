/*
 * SpellResponseStateTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license is available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../../lib/battle/SpellResponseState.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/serializer/CMemorySerializer.h"

TEST(SpellResponseState, ReadinessIncludesTriggerRoundAndExactlyOneFollowingRound)
{
	SpellResponseState state;
	EXPECT_FALSE(state.hasState());
	EXPECT_FALSE(state.isReadyAt(4));

	state.armAt(4);
	EXPECT_TRUE(state.hasState());
	EXPECT_FALSE(state.isReadyAt(3));
	EXPECT_TRUE(state.isReadyAt(4));
	EXPECT_TRUE(state.isReadyAt(5));
	EXPECT_FALSE(state.isReadyAt(6));
}

TEST(SpellResponseState, ConsumptionIsOneShotAndDoesNotConsumeOutsideTheWindow)
{
	SpellResponseState state;
	state.armAt(7);

	EXPECT_FALSE(state.consumeAt(6));
	EXPECT_TRUE(state.hasState());
	EXPECT_TRUE(state.consumeAt(8));
	EXPECT_FALSE(state.hasState());
	EXPECT_FALSE(state.consumeAt(8));
}

TEST(SpellResponseState, RoundArithmeticRemainsSafeAtInt32Limits)
{
	SpellResponseState state;
	state.armAt(std::numeric_limits<int32_t>::max() - 1);

	EXPECT_TRUE(state.isReadyAt(std::numeric_limits<int32_t>::max()));
	EXPECT_FALSE(state.isReadyAt(std::numeric_limits<int32_t>::min()));

	EXPECT_THROW(state.armAt(-1), std::runtime_error);
	EXPECT_EQ(state.armedInRound, std::numeric_limits<int32_t>::max() - 1);
}

TEST(SpellResponseState, CurrentFormatRoundTripsAndOlderReadersDefaultUnarmed)
{
	SpellResponseState original;
	original.armAt(12);

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.oser & original;

	CMemorySerializer currentReader(current.extractBuffer());
	currentReader.iser.version = ESerializationVersion::CURRENT;
	SpellResponseState restored;
	currentReader.iser & restored;
	EXPECT_EQ(restored, original);

	CMemorySerializer legacy;
	legacy.oser.version = ESerializationVersion::NEW_HORIZONS_INCOMING_ELEMENTAL_SPELL_DAMAGE;
	legacy.iser.version = ESerializationVersion::NEW_HORIZONS_INCOMING_ELEMENTAL_SPELL_DAMAGE;
	SpellResponseState empty;
	legacy.oser & empty;

	CMemorySerializer legacyReader(legacy.extractBuffer());
	legacyReader.iser.version = ESerializationVersion::NEW_HORIZONS_INCOMING_ELEMENTAL_SPELL_DAMAGE;
	SpellResponseState defaulted;
	defaulted.armedInRound = 91;
	legacyReader.iser & defaulted;
	EXPECT_FALSE(defaulted.hasState());
	EXPECT_EQ(defaulted.armedInRound, -1);
}

TEST(SpellResponseState, OlderWriterRejectsDroppingPendingReadinessWithoutWritingBytes)
{
	SpellResponseState state;
	state.armAt(3);

	CMemorySerializer legacy;
	legacy.oser.version = ESerializationVersion::NEW_HORIZONS_INCOMING_ELEMENTAL_SPELL_DAMAGE;
	EXPECT_THROW(legacy.oser & state, std::runtime_error);
	EXPECT_TRUE(legacy.extractBuffer().empty());
}

TEST(SpellResponseState, MalformedSerializedRoundIsRejected)
{
	CMemorySerializer malformed;
	malformed.oser.version = ESerializationVersion::CURRENT;
	int32_t invalidRound = -2;
	malformed.oser & invalidRound;

	CMemorySerializer reader(malformed.extractBuffer());
	reader.iser.version = ESerializationVersion::CURRENT;
	SpellResponseState state;
	EXPECT_THROW(reader.iser & state, std::runtime_error);
}

TEST(SpellResponseStatePacket, AcceptsOnlyCurrentRoundArmingAndOneLegalConsumption)
{
	SpellResponseState empty;
	SetSpellResponseState update;
	update.battleID = BattleID(0);
	update.side = BattleSide::DEFENDER;
	update.state.armAt(5);
	EXPECT_NO_THROW(update.validateTransitionFrom(empty, 5));
	EXPECT_THROW(update.validateTransitionFrom(empty, 4), std::runtime_error);

	auto consumed = update.state;
	ASSERT_TRUE(consumed.consumeAt(6));
	update.state = consumed;
	EXPECT_NO_THROW(update.validateTransitionFrom(SpellResponseState{.armedInRound = 5}, 6));
	EXPECT_THROW(update.validateTransitionFrom(SpellResponseState{.armedInRound = 5}, 7), std::runtime_error);
}

TEST(SpellResponseStatePacket, CurrentFormatRoundTripsAndOlderPacketsAreRejected)
{
	SetSpellResponseState original;
	original.battleID = BattleID(0);
	original.side = BattleSide::ATTACKER;
	original.state.armAt(2);

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.oser & original;
	const auto currentBytes = current.extractBuffer();
	CMemorySerializer currentReader(currentBytes);
	currentReader.iser.version = ESerializationVersion::CURRENT;
	SetSpellResponseState restored;
	currentReader.iser & restored;
	EXPECT_EQ(restored.battleID, original.battleID);
	EXPECT_EQ(restored.side, original.side);
	EXPECT_EQ(restored.state, original.state);

	CMemorySerializer oldReader(currentBytes);
	oldReader.iser.version = ESerializationVersion::NEW_HORIZONS_INCOMING_ELEMENTAL_SPELL_DAMAGE;
	SetSpellResponseState legacyTarget;
	EXPECT_THROW(oldReader.iser & legacyTarget, std::runtime_error);

	CMemorySerializer oldWriter;
	oldWriter.oser.version = ESerializationVersion::NEW_HORIZONS_INCOMING_ELEMENTAL_SPELL_DAMAGE;
	EXPECT_THROW(oldWriter.oser & original, std::runtime_error);
	EXPECT_TRUE(oldWriter.extractBuffer().empty());
}

TEST(SpellResponseStatePacket, MalformedTargetIsRejectedBeforeWritingPacketBytes)
{
	SetSpellResponseState malformed;
	malformed.battleID = BattleID(0);
	malformed.side = BattleSide::NONE;

	CMemorySerializer writer;
	writer.oser.version = ESerializationVersion::CURRENT;
	EXPECT_THROW(writer.oser & malformed, std::runtime_error);
	EXPECT_TRUE(writer.extractBuffer().empty());
}
