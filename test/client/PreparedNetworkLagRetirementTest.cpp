/*
 * PreparedNetworkLagRetirementTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../client/netlag/PreparedNetworkLagRetirement.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForServer.h"
#include "../../lib/serializer/BinarySerializer.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Propagators.h"
#include <type_traits>

namespace
{
static_assert(!std::is_copy_constructible_v<PreparedNetworkLagRetirement>);
static_assert(!std::is_move_constructible_v<PreparedNetworkLagRetirement>);
static_assert(noexcept(std::declval<const PreparedNetworkLagRetirement &>().ready()));
struct ObservedRollback final : CPackForClient
{
	int & destructions;
	bool & accepting;
	bool & destroyedDuringAcceptance;
	ObservedRollback(int & count, bool & active, bool & bad)
		: destructions(count), accepting(active), destroyedDuringAcceptance(bad) {}
	~ObservedRollback() override
	{
		++destructions;
		destroyedDuringAcceptance |= accepting;
	}
};

BattleUnitsChanged packet(int id = 0)
{
	BattleUnitsChanged result;
	result.battleID = BattleID(id);
	return result;
}

NetworkLagPredictedPack predicted(const CPackForClient & packet)
{
	NetworkLagPredictedPack result;
	BinarySerializer writer(&result);
	writer & &packet;
	return result;
}

void begin(NetworkLagReplyState & state, uint32_t id)
{
	ArrangeStacks request;
	request.requestID = id;
	request.player = PlayerColor(0);
	state.beginPrediction(request);
}

void open(NetworkLagReplyState & state, uint32_t id)
{
	PackageReceived received;
	received.requestID = id;
	received.player = PlayerColor(0);
	state.openReplyPackage(received);
}

std::unique_ptr<HeroVisitCastle> visit(int id)
{
	auto result = std::make_unique<HeroVisitCastle>();
	result->hid = ObjectInstanceID(id);
	result->tid = ObjectInstanceID(9);
	result->startVisit = true;
	return result;
}
}

TEST(PreparedNetworkLagRetirementTest, MatchUnlinksExactPayloadWithoutDestructionUntilControlledRetirement)
{
	std::mutex mutex;
	std::unique_lock access(mutex);
	int destroyed = 0;
	bool accepting = false, destroyedDuringAcceptance = false;
	NetworkLagReplyState source(&mutex);
	begin(source, 5);
	auto written = predicted(packet());
	auto payload = std::make_unique<ObservedRollback>(destroyed, accepting, destroyedDuringAcceptance);
	const auto * identity = payload.get();
	written.rollbackPacks.push_back(std::move(payload));
	source.appendPrediction(std::move(written));
	open(source, 5);
	auto inspection = source.inspectBattleReply(packet());
	auto plan = source.prepareReplyRetirement(inspection, access);
	ASSERT_EQ(plan->rollbackPackets().size(), 1);
	EXPECT_EQ(plan->rollbackPackets().front(), identity);
	EXPECT_EQ(destroyed, 0);
	accepting = true;
	static_assert(noexcept(plan->commit()));
	ASSERT_TRUE(plan->commit());
	accepting = false;
	EXPECT_FALSE(destroyedDuringAcceptance);
	EXPECT_EQ(destroyed, 0);
	EXPECT_EQ(plan->rollbackPackets().front(), identity);
	EXPECT_FALSE(plan->ready());
	EXPECT_FALSE(plan->commit());
	EXPECT_FALSE(inspection.stillCurrent());
	EXPECT_EQ(source.inspectBattleReply(packet()).result, BattleReplyInspection::Result::APPLY);
	plan.reset();
	EXPECT_EQ(destroyed, 1);
}

TEST(PreparedNetworkLagRetirementTest, PartialMatchPreservesRemainingWrittenAndFollowingPredictionOrder)
{
	std::mutex mutex;
	std::unique_lock access(mutex);
	NetworkLagReplyState source(&mutex);
	begin(source, 5);
	source.appendPrediction(predicted(packet(0)));
	source.appendPrediction(predicted(packet(1)));
	begin(source, 6);
	source.appendPrediction(predicted(packet(2)));
	open(source, 5);
	auto first = source.prepareReplyRetirement(source.inspectBattleReply(packet(0)), access);
	ASSERT_TRUE(first->commit());
	EXPECT_EQ(source.inspectBattleReply(packet(1)).result, BattleReplyInspection::Result::CONSUMED);
	auto second = source.prepareReplyRetirement(source.inspectBattleReply(packet(1)), access);
	ASSERT_TRUE(second->commit());
	EXPECT_EQ(source.inspectBattleReply(packet(2)).result, BattleReplyInspection::Result::APPLY);
	open(source, 6);
	EXPECT_EQ(source.inspectBattleReply(packet(2)).result, BattleReplyInspection::Result::CONSUMED);
}

TEST(PreparedNetworkLagRetirementTest, MismatchRetainsOriginalReverseWrittenForwardRollbackAndNextRequest)
{
	std::mutex mutex;
	std::unique_lock access(mutex);
	NetworkLagReplyState source(&mutex);
	begin(source, 5);
	auto old = visit(1);
	auto first = predicted(*old), second = predicted(*old);
	auto a = visit(4), b = visit(5), c = visit(6);
	const CPackForClient * expected[]{c.get(), a.get(), b.get()};
	// Transfer original physical packet owners; inspection's private copies are
	// NOT the objects unlinked/retained by this storage participant.
	first.rollbackPacks.push_back(std::move(a));
	first.rollbackPacks.push_back(std::move(b));
	second.rollbackPacks.push_back(std::move(c));
	source.appendPrediction(std::move(first));
	source.appendPrediction(std::move(second));
	begin(source, 6);
	source.appendPrediction(predicted(packet(2)));
	open(source, 5);
	auto inspection = source.inspectBattleReply(packet());
	ASSERT_EQ(inspection.result, BattleReplyInspection::Result::ROLLBACK_THEN_APPLY);
	auto plan = source.prepareReplyRetirement(inspection, access);
	ASSERT_EQ(plan->rollbackPackets().size(), 3);
	for(size_t i = 0; i < 3; ++i)
	{
		EXPECT_EQ(plan->rollbackPackets()[i], expected[i]);
		EXPECT_NE(plan->rollbackPackets()[i], inspection.rollbacks[i].get());
	}
	ASSERT_TRUE(plan->commit());
	for(size_t i = 0; i < 3; ++i)
		EXPECT_EQ(plan->rollbackPackets()[i], expected[i]);
	open(source, 6);
	EXPECT_EQ(source.inspectBattleReply(packet(2)).result, BattleReplyInspection::Result::CONSUMED);
}

TEST(PreparedNetworkLagRetirementTest, NormalQueueMutationInvalidatesBeforeAnyUnlink)
{
	std::mutex mutex;
	std::unique_lock access(mutex);
	NetworkLagReplyState source(&mutex);
	begin(source, 5);
	source.appendPrediction(predicted(packet()));
	open(source, 5);
	auto plan = source.prepareReplyRetirement(source.inspectBattleReply(packet()), access);
	begin(source, 6); // real owner invalidation, not a synthetic epoch rewind
	source.appendPrediction(predicted(packet(1)));
	EXPECT_FALSE(plan->ready());
	EXPECT_FALSE(plan->commit());
	EXPECT_THROW(plan->rollbackPackets(), std::logic_error);
	EXPECT_EQ(source.inspectBattleReply(packet()).result, BattleReplyInspection::Result::CONSUMED);
}

TEST(PreparedNetworkLagRetirementTest, ForeignTamperedAndNoActiveInspectionCannotForgeFrontReceipt)
{
	std::mutex mutex;
	std::unique_lock access(mutex);
	NetworkLagReplyState source(&mutex), foreign(&mutex);
	begin(source, 5); source.appendPrediction(predicted(packet())); open(source, 5);
	begin(foreign, 5); foreign.appendPrediction(predicted(packet())); open(foreign, 5);
	auto inspection = source.inspectBattleReply(packet());
	EXPECT_THROW(foreign.prepareReplyRetirement(inspection, access), std::invalid_argument);
	inspection.result = BattleReplyInspection::Result::ROLLBACK_THEN_APPLY;
	EXPECT_FALSE(inspection.stillCurrent());
	EXPECT_THROW(source.prepareReplyRetirement(inspection, access), std::invalid_argument);
	inspection.result = static_cast<BattleReplyInspection::Result>(99);
	EXPECT_FALSE(inspection.stillCurrent());
	EXPECT_THROW(source.prepareReplyRetirement(inspection, access), std::invalid_argument);
	NetworkLagReplyState inactive(&mutex);
	auto absent = inactive.inspectBattleReply(packet());
	EXPECT_THROW(inactive.prepareReplyRetirement(absent, access), std::invalid_argument);
	EXPECT_EQ(source.inspectBattleReply(packet()).result, BattleReplyInspection::Result::CONSUMED);
}

TEST(PreparedNetworkLagRetirementTest, MissingWrongAndReleasedActualLockRefuseWithoutReacquisition)
{
	std::mutex mutex, wrong;
	std::unique_lock access(mutex), wrongAccess(wrong);
	std::unique_lock<std::mutex> absentAccess(mutex, std::defer_lock);
	NetworkLagReplyState source(&mutex), unbound;
	begin(source, 5); source.appendPrediction(predicted(packet())); open(source, 5);
	begin(unbound, 5); unbound.appendPrediction(predicted(packet())); open(unbound, 5);
	auto inspection = source.inspectBattleReply(packet());
	EXPECT_THROW(source.prepareReplyRetirement(inspection, absentAccess), std::invalid_argument);
	EXPECT_THROW(source.prepareReplyRetirement(inspection, wrongAccess), std::invalid_argument);
	EXPECT_THROW(unbound.prepareReplyRetirement(unbound.inspectBattleReply(packet()), access), std::invalid_argument);
	auto plan = source.prepareReplyRetirement(inspection, access);
	access.unlock(); // violated interval must not trigger hidden reacquire/wait
	EXPECT_FALSE(plan->ready());
	EXPECT_FALSE(plan->commit());
	plan.reset(); // abandon BEFORE any new lock acquisition
	access.lock();
	EXPECT_EQ(source.inspectBattleReply(packet()).result, BattleReplyInspection::Result::CONSUMED);
}

TEST(PreparedNetworkLagRetirementTest, SourceRetirementMakesPendingReceiptSafeStaleBeforePacketAccess)
{
	std::mutex mutex;
	std::unique_lock access(mutex);
	int destroyed = 0;
	bool accepting = false, destroyedDuringAcceptance = false;
	auto source = std::make_unique<NetworkLagReplyState>(&mutex);
	begin(*source, 5);
	auto written = predicted(packet());
	written.rollbackPacks.push_back(std::make_unique<ObservedRollback>(destroyed, accepting, destroyedDuringAcceptance));
	source->appendPrediction(std::move(written));
	open(*source, 5);
	auto plan = source->prepareReplyRetirement(source->inspectBattleReply(packet()), access);
	source.reset(); // deliberate same-thread lifetime refusal fixture
	EXPECT_EQ(destroyed, 1);
	EXPECT_FALSE(plan->ready());
	EXPECT_FALSE(plan->commit());
	EXPECT_THROW(plan->rollbackPackets(), std::logic_error);
	plan.reset();
	EXPECT_EQ(destroyed, 1);
	EXPECT_FALSE(destroyedDuringAcceptance);
}

TEST(PreparedNetworkLagRetirementTest, AcceptedOriginalPacketsOutliveSourceUntilReceiptRetirement)
{
	std::mutex mutex;
	std::unique_lock access(mutex);
	int destroyed = 0;
	bool accepting = false, destroyedDuringAcceptance = false;
	auto source = std::make_unique<NetworkLagReplyState>(&mutex);
	begin(*source, 5);
	auto written = predicted(packet());
	auto observed = std::make_unique<ObservedRollback>(destroyed, accepting, destroyedDuringAcceptance);
	const auto * identity = observed.get();
	written.rollbackPacks.push_back(std::move(observed));
	source->appendPrediction(std::move(written));
	open(*source, 5);
	auto plan = source->prepareReplyRetirement(source->inspectBattleReply(packet()), access);
	ASSERT_TRUE(plan->commit());
	source.reset();
	EXPECT_EQ(destroyed, 0);
	ASSERT_EQ(plan->rollbackPackets().size(), 1);
	EXPECT_EQ(plan->rollbackPackets().front(), identity);
	plan.reset();
	EXPECT_EQ(destroyed, 1);
	EXPECT_FALSE(destroyedDuringAcceptance);
}
