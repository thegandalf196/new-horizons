/*
 * LuckSerendipityStateTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../lib/battle/LuckSerendipityState.h"
#include "../../lib/battle/SylvanLuckState.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/serializer/CMemorySerializer.h"

TEST(LuckSerendipityState, RequiresActualPreviousCombatRound)
{
	LuckSerendipityState state;
	EXPECT_FALSE(state.availableAt(2));
	state.enabled = true;
	EXPECT_FALSE(state.availableAt(0));
	state.nextRound(1);
	EXPECT_FALSE(state.availableAt(1));
	state.nextRound(2);
	EXPECT_TRUE(state.availableAt(2));
	EXPECT_FALSE(state.availableAt(3));
}

TEST(LuckSerendipityState, OrdinaryFirstAttackConsumesEvenWithoutLuckOutcome)
{
	LuckSerendipityState state{true};
	state.nextRound(1);
	state.nextRound(2);
	state.recordStrike(false, false);
	EXPECT_TRUE(state.availableAt(2));
	state.recordStrike(true, false);
	EXPECT_FALSE(state.availableAt(2));
	EXPECT_FALSE(state.currentRoundPositiveLuck);
	state.nextRound(3);
	EXPECT_TRUE(state.availableAt(3));
}

TEST(LuckSerendipityState, AnyActualPositiveFlagSuppressesFollowingRound)
{
	LuckSerendipityState state{true};
	state.nextRound(1);
	state.recordStrike(false, true);
	EXPECT_FALSE(state.firstAttackUsed);
	state.nextRound(2);
	EXPECT_FALSE(state.availableAt(2));
	state.recordStrike(true, false);
	state.nextRound(3);
	EXPECT_TRUE(state.availableAt(3));
}

TEST(LuckSerendipityState, SylvanCombatHistoryIsIndependent)
{
	SylvanLuckState sylvan;
	sylvan.serendipity = true;
	LuckSerendipityState generic{true};
	generic.nextRound(1);
	generic.nextRound(2);
	generic.recordStrike(true, false);
	EXPECT_EQ(sylvan.chanceLuck(0, 5, false), 1);
	sylvan.recordStrike(5, true, false);
	EXPECT_EQ(sylvan.chanceLuck(0, 5, false), 0);
	generic.nextRound(3);
	EXPECT_TRUE(generic.availableAt(3));
}

TEST(LuckSerendipityState, RejectsInvalidShapeAndNonmonotoneTransitions)
{
	LuckSerendipityState invalid;
	invalid.firstAttackUsed = true;
	EXPECT_THROW(invalid.validate(), std::runtime_error);
	LuckSerendipityState state{true};
	EXPECT_THROW(state.nextRound(2), std::runtime_error);
	state.nextRound(1);
	state.nextRound(2);
	const auto previous = state;
	state.recordStrike(true, true);
	EXPECT_NO_THROW(state.validateTransitionFrom(previous, 2));
	EXPECT_THROW(previous.validateTransitionFrom(state, 2), std::runtime_error);
	EXPECT_THROW(state.validateTransitionFrom(previous, 3), std::runtime_error);
	state.round = std::numeric_limits<int32_t>::max();
	EXPECT_THROW(state.nextRound(0), std::runtime_error);
}

TEST(LuckSerendipityState, NativeSerializerPreservesPendingAndConsumedHistory)
{
	LuckSerendipityState state{true};
	state.nextRound(1);
	state.nextRound(2);
	CMemorySerializer pending;
	pending.oser & state;
	LuckSerendipityState restored;
	pending.iser & restored;
	EXPECT_EQ(restored, state);
	state.recordStrike(true, true);
	CMemorySerializer consumed;
	consumed.oser & state;
	consumed.iser & restored;
	EXPECT_EQ(restored, state);
}

TEST(LuckSerendipityState, PacketRejectsMalformedSidesAndPreservesNativeSnapshot)
{
	BattleAttack packet;
	packet.battleID = BattleID(0);
	packet.luckSerendipityState = LuckSerendipityState{true, 2, false, true, true};
	EXPECT_THROW(packet.validateLuckSerendipityMarker(), std::runtime_error);
	packet.luckSerendipitySide = BattleSide::ATTACKER;
	packet.bsa.emplace_back();
	EXPECT_NO_THROW(packet.validateLuckSerendipityMarker());
	CMemorySerializer serializer;
	serializer.oser & packet;
	BattleAttack restored;
	serializer.iser & restored;
	EXPECT_EQ(restored.luckSerendipitySide, packet.luckSerendipitySide);
	EXPECT_EQ(restored.luckSerendipityState, packet.luckSerendipityState);
}

TEST(LuckSerendipityState, OlderWireResetsStateAndRejectsDiscardingHistoryBeforeWriting)
{
	const auto oldVersion = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_LUCK_SERENDIPITY) - 1);
	LuckSerendipityState pending{true, 2};
	CMemorySerializer rejected;
	rejected.oser.version = oldVersion;
	EXPECT_THROW(rejected.oser & pending, std::runtime_error);
	EXPECT_TRUE(rejected.extractBuffer().empty());
	CMemorySerializer legacy;
	legacy.oser.version = legacy.iser.version = oldVersion;
	LuckSerendipityState empty;
	legacy.oser & empty;
	legacy.iser & pending;
	EXPECT_EQ(pending, empty);
}
