/*
 * OverwhelmingFormulaProtocolTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/gameState/GameStatePackVisitor.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
SetOverwhelmingFormulaState updateFor(const OverwhelmingFormulaState & state)
{
	SetOverwhelmingFormulaState update;
	update.battleID = BattleID(0);
	update.side = BattleSide::ATTACKER;
	update.state = state;
	return update;
}

class FormulaEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit FormulaEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

TEST(OverwhelmingFormulaProtocol, RegistersOneCastOrClaimsOneRegisteredWinner)
{
	OverwhelmingFormulaState empty;
	auto candidate = empty;
	ASSERT_EQ(candidate.registerAcceptedEligibleCast(), 1u);
	auto update = updateFor(candidate);
	EXPECT_NO_THROW(update.validateTransitionFrom(empty));
	EXPECT_NO_THROW(update.validateTransitionFrom(candidate));
	auto secondCandidate = candidate;
	ASSERT_EQ(secondCandidate.registerAcceptedEligibleCast(), 2u);
	update.state = secondCandidate;
	EXPECT_NO_THROW(update.validateTransitionFrom(candidate));
	ASSERT_TRUE(update.state.claimActualDamage(1, true, 10, true));
	EXPECT_NO_THROW(update.validateTransitionFrom(secondCandidate));
	EXPECT_NO_THROW(update.validateTransitionFrom(update.state));
}

TEST(OverwhelmingFormulaProtocol, RejectsSkippingRewindingCombinedRegistrationAndClaimOrReassigningWinner)
{
	OverwhelmingFormulaState pending;
	ASSERT_EQ(pending.registerAcceptedEligibleCast(), 1u);
	ASSERT_EQ(pending.registerAcceptedEligibleCast(), 2u);
	auto update = updateFor(pending);
	EXPECT_THROW(update.validateTransitionFrom(OverwhelmingFormulaState{}), std::runtime_error);
	update.state = {};
	EXPECT_THROW(update.validateTransitionFrom(pending), std::runtime_error);
	update.state = pending;
	ASSERT_EQ(update.state.registerAcceptedEligibleCast(), 3u);
	ASSERT_TRUE(update.state.claimActualDamage(3, true, 10, true));
	EXPECT_THROW(update.validateTransitionFrom(pending), std::runtime_error);
	ASSERT_TRUE(pending.claimActualDamage(1, true, 10, true));
	update.state = pending;
	update.state.winningCastToken = 2;
	EXPECT_THROW(update.validateTransitionFrom(pending), std::runtime_error);
	update.state.winningCastToken = 0;
	EXPECT_THROW(update.validateTransitionFrom(pending), std::runtime_error);
	update.state = pending;
	ASSERT_EQ(update.state.registerAcceptedEligibleCast(), 3u);
	EXPECT_NO_THROW(update.validateTransitionFrom(pending));
}

TEST(OverwhelmingFormulaProtocol, InvalidTargetStateAndOverflowTransitionAreRejected)
{
	auto update = updateFor({});
	update.side = BattleSide::NONE;
	EXPECT_THROW(update.validateTransitionFrom({}), std::runtime_error);
	update.side = BattleSide::ALL_KNOWING;
	EXPECT_THROW(update.validateShape(), std::runtime_error);
	update.side = BattleSide::ATTACKER;
	update.battleID = BattleID::NONE;
	EXPECT_THROW(update.validateShape(), std::runtime_error);
	update.battleID = BattleID(0);
	update.state.winningCastToken = 1;
	EXPECT_THROW(update.validateShape(), std::runtime_error);
	OverwhelmingFormulaState exhausted;
	exhausted.lastCandidateCastToken = std::numeric_limits<uint64_t>::max();
	update.state = {};
	EXPECT_THROW(update.validateTransitionFrom(exhausted), std::runtime_error);
	update.state = exhausted;
	EXPECT_NO_THROW(update.validateTransitionFrom(exhausted));
}

TEST(OverwhelmingFormulaProtocol, PacketRoundTripsAndOlderFormatsRejectBeforeWriting)
{
	OverwhelmingFormulaState state;
	const auto token = state.registerAcceptedEligibleCast();
	ASSERT_TRUE(state.claimActualDamage(token, true, 10, true));
	auto original = updateFor(state);
	CMemorySerializer writer;
	writer.oser.version = ESerializationVersion::CURRENT;
	writer.oser & original;
	const auto bytes = writer.extractBuffer();
	CMemorySerializer reader(bytes);
	reader.iser.version = ESerializationVersion::CURRENT;
	SetOverwhelmingFormulaState restored;
	reader.iser & restored;
	EXPECT_EQ(restored.battleID, original.battleID);
	EXPECT_EQ(restored.side, original.side);
	EXPECT_EQ(restored.state, original.state);
	CMemorySerializer olderWriter;
	olderWriter.oser.version = ESerializationVersion::NEW_HORIZONS_CREATURE_TRANSIT_REACH;
	EXPECT_THROW(olderWriter.oser & original, std::runtime_error);
	EXPECT_TRUE(olderWriter.extractBuffer().empty());
	CMemorySerializer olderReader(bytes);
	olderReader.iser.version = ESerializationVersion::NEW_HORIZONS_CREATURE_TRANSIT_REACH;
	EXPECT_THROW(olderReader.iser & restored, std::runtime_error);
}

TEST(OverwhelmingFormulaProtocol, FullUint64TokensRoundTripThroughNativeSerializerPendingAndClaimed)
{
	const auto maximumToken = std::numeric_limits<OverwhelmingFormulaState::CastToken>::max();
	for(const auto winner : {OverwhelmingFormulaState::INVALID_CAST_TOKEN, maximumToken - 1, maximumToken})
	{
		OverwhelmingFormulaState original;
		original.lastCandidateCastToken = maximumToken;
		original.winningCastToken = winner;
		CMemorySerializer writer;
		writer.oser.version = ESerializationVersion::CURRENT;
		writer.oser & original;
		CMemorySerializer reader(writer.extractBuffer());
		reader.iser.version = ESerializationVersion::CURRENT;
		OverwhelmingFormulaState restored;
		reader.iser & restored;
		EXPECT_EQ(restored, original);
		EXPECT_EQ(restored.registerAcceptedEligibleCast(), OverwhelmingFormulaState::INVALID_CAST_TOKEN);
		EXPECT_EQ(restored.canPenetrate(maximumToken), winner == 0 || winner == maximumToken);
		EXPECT_EQ(restored.canPenetrate(maximumToken - 1), winner == 0 || winner == maximumToken - 1);

		auto packet = updateFor(original);
		CMemorySerializer packetWriter;
		packetWriter.oser.version = ESerializationVersion::CURRENT;
		packetWriter.oser & packet;
		CMemorySerializer packetReader(packetWriter.extractBuffer());
		packetReader.iser.version = ESerializationVersion::CURRENT;
		SetOverwhelmingFormulaState restoredPacket;
		packetReader.iser & restoredPacket;
		EXPECT_EQ(restoredPacket.state, original);
	}
}

TEST(OverwhelmingFormulaProtocol, SideStateRoundTripsAndLegacyLoadClearsReusedState)
{
	SideInBattle original(nullptr);
	ASSERT_EQ(original.overwhelmingFormulaState.registerAcceptedEligibleCast(), 1u);
	ASSERT_EQ(original.overwhelmingFormulaState.registerAcceptedEligibleCast(), 2u);
	ASSERT_TRUE(original.overwhelmingFormulaState.claimActualDamage(2, true, 10, true));
	CMemorySerializer writer;
	writer.oser.version = ESerializationVersion::CURRENT;
	writer.oser & original;
	CMemorySerializer reader(writer.extractBuffer());
	reader.iser.version = ESerializationVersion::CURRENT;
	SideInBattle restored(nullptr);
	reader.iser & restored;
	EXPECT_EQ(restored.overwhelmingFormulaState, original.overwhelmingFormulaState);

	CMemorySerializer legacy;
	legacy.oser.version = ESerializationVersion::NEW_HORIZONS_CREATURE_TRANSIT_REACH;
	SideInBattle empty(nullptr);
	legacy.oser & empty;
	CMemorySerializer legacyReader(legacy.extractBuffer());
	legacyReader.iser.version = ESerializationVersion::NEW_HORIZONS_CREATURE_TRANSIT_REACH;
	legacyReader.iser & restored;
	EXPECT_EQ(restored.overwhelmingFormulaState, OverwhelmingFormulaState{});

	CMemorySerializer downgrade;
	downgrade.oser.version = ESerializationVersion::NEW_HORIZONS_CREATURE_TRANSIT_REACH;
	EXPECT_THROW(downgrade.oser & original, std::runtime_error);
	EXPECT_TRUE(downgrade.extractBuffer().empty());
}

class OverwhelmingFormulaProtocolBattle : public HeroCommandFixture {};

TEST_F(OverwhelmingFormulaProtocolBattle, RealAndNestedDetachedPacketsPreserveIndependentSideState)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(startBattle());
	OverwhelmingFormulaState state;
	ASSERT_EQ(state.registerAcceptedEligibleCast(), 1u);
	auto initial = updateFor(state);
	BattleStatePackVisitor realVisitor(*battle());
	initial.visitTyped(realVisitor);
	FormulaEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	EXPECT_EQ(parent->getOverwhelmingFormulaState(BattleSide::ATTACKER), state);
	auto parentUpdate = updateFor(state);
	ASSERT_TRUE(parentUpdate.state.claimActualDamage(1, true, 10, true));
	parent->getServerCallback()->apply(parentUpdate);
	EXPECT_EQ(battle()->getOverwhelmingFormulaState(BattleSide::ATTACKER), state);
	HypotheticBattle child(&environment, parent);
	EXPECT_EQ(child.getOverwhelmingFormulaState(BattleSide::ATTACKER), parentUpdate.state);
	auto childUpdate = parentUpdate;
	ASSERT_EQ(childUpdate.state.registerAcceptedEligibleCast(), 2u);
	child.getServerCallback()->apply(childUpdate);
	EXPECT_EQ(parent->getOverwhelmingFormulaState(BattleSide::ATTACKER), parentUpdate.state);
	EXPECT_EQ(child.getOverwhelmingFormulaState(BattleSide::DEFENDER), OverwhelmingFormulaState{});
	auto invalid = childUpdate;
	invalid.state = {};
	EXPECT_THROW(child.getServerCallback()->apply(invalid), std::runtime_error);
	EXPECT_EQ(child.getOverwhelmingFormulaState(BattleSide::ATTACKER), childUpdate.state);
	invalid = childUpdate;
	invalid.battleID = BattleID(1);
	EXPECT_THROW(child.getServerCallback()->apply(invalid), std::runtime_error);
	EXPECT_EQ(child.getOverwhelmingFormulaState(BattleSide::ATTACKER), childUpdate.state);
}
