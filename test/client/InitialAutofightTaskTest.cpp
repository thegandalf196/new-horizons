/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/BattleTestFixture.h"
#include "../../client/InitialAutofightTask.h"
#include "../../client/Client.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/callback/IClient.h"
#include "../../lib/networkPacks/PacksForServer.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/callback/CBattleGameInterface.h"
#include <atomic>
#include <chrono>
#include <future>
#include <functional>
#include <shared_mutex>
#include <stdexcept>

namespace
{
using namespace std::chrono_literals;
class ProbeController final : public CBattleGameInterface
{
public:
	std::atomic<int> calls{0};
	std::atomic<uint32_t> observedUnit{0};
	std::function<void()> body = [] {};
	void activeStack(const BattleID &, const CStack * unit) override
	{
		observedUnit = unit->unitId();
		++calls;
		body();
	}
	void yourTacticPhase(const BattleID &, int) override { ++calls; body(); }
};
class RequestClient final : public IClient
{
public:
	InitialAutofightTask & task;
	std::shared_ptr<CGameState> world;
	ThreadSafeVector<int> waiting; // The actual existing pending-request container.
	std::vector<uint32_t> requestIds;
	bool failBeforeQueue = false;
	bool failInTransport = false;
	RequestClient(InitialAutofightTask & task, std::shared_ptr<CGameState> world) : task(task), world(std::move(world)) {}
	std::optional<BattleAction> makeSurrenderRetreatDecision(PlayerColor, const BattleID &, const BattleStateInfoForRetreat &) override
	{ return std::nullopt; }
	int sendRequest(const CPackForServer & pack, PlayerColor player, bool wait) override
	{
		EXPECT_FALSE(wait);
		const auto * action = dynamic_cast<const MakeAction *>(&pack);
		if(!action) throw std::logic_error("Expected real MakeAction callback path");
		const auto id = static_cast<uint32_t>(requestIds.size() + 41);
		EXPECT_TRUE(task.prepareRequest(world, player, id, action->battleID));
		requestIds.push_back(id);
		if(failBeforeQueue)
		{
			task.requestNotSubmitted(id);
			throw std::runtime_error("Before queue/transport");
		}
		waiting.pushBack(id);
		pack.requestID = id;
		pack.player = player;
		task.enteringTransport(id);
		if(failInTransport)
		{
			task.failedInTransport(id);
			throw std::runtime_error("Unknown transport delivery");
		}
		task.returnedFromTransport(id);
		return id;
	}
	bool acknowledge(PlayerColor player, uint32_t id, bool accepted)
	{
		PackageApplied receipt(player, id, 0, accepted);
		if(!waiting.tryRemovingElement(receipt.requestID)) return false;
		task.requestRealized(world, receipt.player, receipt.requestID, receipt.result);
		return true;
	}
};
}

class InitialAutofightTaskTest : public BattleTestFixture
{
protected:
	void prepare()
	{
		startGame();
		startBattle();
		beginCombat();
		ASSERT_NE(battle(), nullptr);
		ASSERT_GE(battle()->activeStack, 0);
	}
	InitialAutofightTask::Request request(const std::shared_ptr<ProbeController> & controller, IClient * client = nullptr)
	{
		const auto * unit = battle()->battleGetStackByID(battle()->activeStack, false);
		auto callback = std::make_shared<CBattleCallback>(unit->unitOwner(), client);
		callback->onBattleStarted(battle());
		return {gameState(), callback, controller, battle()->getBattleID(), unit->unitId(), battle()->getActivationSerial()};
	}
	InitialAutofightTask::Request throwingRequest(const std::shared_ptr<ProbeController> & controller, RequestClient & client, int count = 1)
	{
		auto pending = request(controller, &client);
		controller->body = [this, callback = pending.callback, battleID = pending.battle, unit = *pending.unit, count]
		{
			for(int i = 0; i < count; ++i)
				callback->battleMakeUnitAction(battleID, BattleAction::makeDefend(battle()->battleGetStackByID(unit, false)));
			throw std::runtime_error("Controller threw after submission");
		};
		return pending;
	}
};

TEST_F(InitialAutofightTaskTest, RetainsControllerAndCallbackAcrossSourceHandleReset)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	InitialAutofightTask owner;
	auto controller = std::make_shared<ProbeController>();
	auto pending = request(controller);
	const auto id = *pending.unit;
	std::weak_ptr<ProbeController> controllerLifetime = controller;
	std::weak_ptr<CBattleCallback> callbackLifetime = pending.callback;
	std::promise<void> entered;
	controller->body = [&] { entered.set_value(); };
	{
		std::unique_lock state(CGameState::mutex);
		ASSERT_TRUE(owner.start(std::move(pending)));
		controller.reset();
		EXPECT_FALSE(controllerLifetime.expired());
		EXPECT_FALSE(callbackLifetime.expired());
	}
	EXPECT_EQ(entered.get_future().wait_for(2s), std::future_status::ready);
	owner.wait();
	EXPECT_TRUE(owner.idle());
	EXPECT_FALSE(controllerLifetime.expired()); // Completion retains exact GUI recovery identity.
	EXPECT_FALSE(callbackLifetime.expired());
	{
		const auto completion = owner.takeCompletion();
		ASSERT_TRUE(completion);
		EXPECT_EQ(completion->result, InitialAutofightResult::COMPLETED);
	}
	EXPECT_TRUE(controllerLifetime.expired());
	EXPECT_TRUE(callbackLifetime.expired());
	EXPECT_EQ(id, battle()->battleGetStackByID(battle()->activeStack, false)->unitId());
}

TEST_F(InitialAutofightTaskTest, StopBeforeStateAdmissionSkipsEntryAndClosesFurtherRequests)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	InitialAutofightTask owner;
	auto controller = std::make_shared<ProbeController>();
	{
		std::unique_lock state(CGameState::mutex);
		ASSERT_TRUE(owner.start(request(controller)));
		owner.requestStop();
	}
	owner.wait();
	EXPECT_EQ(controller->calls, 0);
	EXPECT_FALSE(owner.start(request(controller)));
}

TEST_F(InitialAutofightTaskTest, StaleBattleActorAndCallbackSourceNeverEnterController)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	InitialAutofightTask owner;
	auto controller = std::make_shared<ProbeController>();
	auto wrongBattle = request(controller);
	wrongBattle.battle = BattleID(123456);
	ASSERT_TRUE(owner.start(std::move(wrongBattle)));
	owner.wait();
	auto wrongUnit = request(controller);
	wrongUnit.unit = uint32_t(123456);
	ASSERT_TRUE(owner.start(std::move(wrongUnit)));
	owner.wait();
	BattleInfo foreign(gameState().get());
	foreign.battleID = battle()->getBattleID();
	auto wrongSource = request(controller);
	wrongSource.callback = std::make_shared<CBattleCallback>(PlayerColor(0), nullptr);
	wrongSource.callback->onBattleStarted(&foreign);
	ASSERT_TRUE(owner.start(std::move(wrongSource)));
	owner.wait();
	EXPECT_EQ(controller->calls, 0);
}

TEST_F(InitialAutofightTaskTest, NestedReceivePausesNeverReopenAdmissionsPrematurely)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	InitialAutofightTask owner;
	auto controller = std::make_shared<ProbeController>();
	{
		auto packet = owner.pauseAdmissions();
		{
			auto nestedDrain = owner.pauseAdmissions();
			EXPECT_FALSE(owner.acceptsRequest());
		}
		EXPECT_FALSE(owner.acceptsRequest());
		EXPECT_FALSE(owner.start(request(controller)));
	}
	EXPECT_TRUE(owner.acceptsRequest());
	ASSERT_TRUE(owner.start(request(controller)));
	owner.wait();
	EXPECT_EQ(controller->calls, 1);
}

TEST_F(InitialAutofightTaskTest, OneFlightAndEveryDrainerWaitForTheActualControllerExit)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	InitialAutofightTask owner;
	auto controller = std::make_shared<ProbeController>();
	std::promise<void> entered, release, drained1, drained2;
	auto releaseSignal = release.get_future().share();
	controller->body = [&] { entered.set_value(); releaseSignal.wait_for(2s); };
	ASSERT_TRUE(owner.start(request(controller)));
	EXPECT_EQ(entered.get_future().wait_for(2s), std::future_status::ready);
	EXPECT_FALSE(owner.start(request(controller)));
	auto done1 = drained1.get_future();
	auto done2 = drained2.get_future();
	std::thread first([&] { owner.wait(); drained1.set_value(); });
	std::thread second([&] { owner.wait(); drained2.set_value(); });
	EXPECT_EQ(done1.wait_for(20ms), std::future_status::timeout);
	EXPECT_EQ(done2.wait_for(20ms), std::future_status::timeout);
	release.set_value();
	first.join();
	second.join();
	EXPECT_EQ(done1.wait_for(0s), std::future_status::ready);
	EXPECT_EQ(done2.wait_for(0s), std::future_status::ready);
	EXPECT_EQ(controller->calls, 1);
	EXPECT_TRUE(owner.idle());
}

TEST_F(InitialAutofightTaskTest, EntryExceptionIsContainedAndThreadCanBeDrained)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	InitialAutofightTask owner;
	auto controller = std::make_shared<ProbeController>();
	controller->body = [] { throw std::runtime_error("Injected auto-fight failure"); };
	ASSERT_TRUE(owner.start(request(controller)));
	owner.wait();
	EXPECT_TRUE(owner.idle());
	controller->body = [] {};
	ASSERT_TRUE(owner.start(request(controller)));
	owner.wait();
	EXPECT_EQ(controller->calls, 2);
}

TEST_F(InitialAutofightTaskTest, ControllerEntryOwnsARealStateReadLeaseAndReleasesItBeforeDrainReturns)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	InitialAutofightTask owner;
	auto controller = std::make_shared<ProbeController>();
	std::atomic<bool> writeAdmitted{true};
	controller->body = [&]
	{
		std::thread writer([&]
		{
			std::unique_lock state(CGameState::mutex, std::try_to_lock);
			writeAdmitted = state.owns_lock();
		});
		writer.join();
	};
	ASSERT_TRUE(owner.start(request(controller)));
	owner.wait();
	EXPECT_FALSE(writeAdmitted);
	std::unique_lock state(CGameState::mutex, std::try_to_lock);
	EXPECT_TRUE(state.owns_lock());
}

TEST_F(InitialAutofightTaskTest, BlockingCallbackIsRefusedWithoutLaunchingOrSynthesizingAnAction)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	InitialAutofightTask owner;
	auto controller = std::make_shared<ProbeController>();
	auto pending = request(controller);
	pending.callback->waitTillRealize = true;
	EXPECT_FALSE(owner.start(std::move(pending)));
	EXPECT_TRUE(owner.idle());
	EXPECT_EQ(controller->calls, 0);
}

TEST_F(InitialAutofightTaskTest, FailureBeforeSubmissionRetainsExactManualActivationAndWorld)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	InitialAutofightTask owner;
	auto controller = std::make_shared<ProbeController>();
	controller->body = [] { throw std::runtime_error("Before any callback submission"); };
	const auto before = gameState()->saveReplaySnapshot();
	ASSERT_TRUE(owner.start(request(controller)));
	owner.wait();
	const auto completion = owner.peekCompletion();
	ASSERT_TRUE(completion);
	EXPECT_EQ(completion->result, InitialAutofightResult::FAILED);
	EXPECT_TRUE(completion->stillAwaitingInput());
	EXPECT_EQ(before, gameState()->saveReplaySnapshot()); // Full state/RNG, no fake action.
	auto stale = *completion;
	++stale.source.activationSerial;
	EXPECT_FALSE(stale.stillAwaitingInput());
	EXPECT_TRUE(owner.takeCompletion());
	EXPECT_FALSE(owner.completed());
}


TEST_F(InitialAutofightTaskTest, SubmitThenThrowCannotRestoreBeforeExactRealization)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	InitialAutofightTask owner;
	RequestClient client(owner, gameState());
	auto controller = std::make_shared<ProbeController>();
	ASSERT_TRUE(owner.start(throwingRequest(controller, client)));
	owner.wait();
	ASSERT_EQ(client.requestIds.size(), 1);
	const auto id = client.requestIds.front();
	const auto player = *owner.peekCompletion()->source.callback->getPlayerID();
	EXPECT_TRUE(owner.peekCompletion()->waitingForRealization());
	EXPECT_FALSE(owner.peekCompletion()->stillAwaitingInput());
	owner.requestRealized({}, player, id, false);
	owner.requestRealized(gameState(), PlayerColor(7), id, false);
	EXPECT_FALSE(client.acknowledge(player, id + 1, false));
	EXPECT_FALSE(owner.peekCompletion()->stillAwaitingInput());
	ASSERT_TRUE(client.acknowledge(player, id, false)); // Rejected means no accepted old action remains.
	EXPECT_FALSE(owner.peekCompletion()->waitingForRealization());
	EXPECT_TRUE(owner.peekCompletion()->stillAwaitingInput());
	EXPECT_EQ(controller->calls, 1); // ACK never retries the AI or submits a fallback.
}

TEST_F(InitialAutofightTaskTest, AcceptedAckRequiresRealInputDispatchThenLiveActorRevalidation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	InitialAutofightTask owner;
	RequestClient client(owner, gameState());
	auto controller = std::make_shared<ProbeController>();
	ASSERT_TRUE(owner.start(throwingRequest(controller, client)));
	owner.wait();
	auto completion = *owner.peekCompletion();
	const auto player = *completion.source.callback->getPlayerID();
	ASSERT_TRUE(client.acknowledge(player, client.requestIds.front(), true));
	EXPECT_FALSE(owner.peekCompletion()->stillAwaitingInput()); // Same local actor is NOT sufficient.
	EXPECT_FALSE(owner.deferInput({}, player, completion.source.battle, completion.source.unit, completion.source.activationSerial));
	EXPECT_TRUE(owner.deferInput(gameState(), player, completion.source.battle, completion.source.unit, completion.source.activationSerial));
	EXPECT_TRUE(owner.peekCompletion()->stillAwaitingInput());
	completion = *owner.peekCompletion();
	++completion.deferredInput->activationSerial;
	EXPECT_FALSE(completion.stillAwaitingInput());
	EXPECT_EQ(controller->calls, 1);
}

TEST_F(InitialAutofightTaskTest, InputBeforeAckIsDeferredWithoutAnotherControllerCall)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	InitialAutofightTask owner;
	RequestClient client(owner, gameState());
	auto controller = std::make_shared<ProbeController>();
	ASSERT_TRUE(owner.start(throwingRequest(controller, client)));
	owner.wait();
	const auto source = owner.peekCompletion()->source;
	const auto player = *source.callback->getPlayerID();
	ASSERT_TRUE(owner.deferInput(gameState(), player, source.battle, source.unit, source.activationSerial));
	EXPECT_FALSE(owner.peekCompletion()->stillAwaitingInput());
	ASSERT_TRUE(client.acknowledge(player, client.requestIds.front(), true));
	EXPECT_TRUE(owner.peekCompletion()->stillAwaitingInput());
	EXPECT_EQ(controller->calls, 1);
}

TEST_F(InitialAutofightTaskTest, ProvenBeforeTransportFailureNeedsNoInventedAck)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	InitialAutofightTask owner;
	RequestClient client(owner, gameState());
	client.failBeforeQueue = true;
	auto controller = std::make_shared<ProbeController>();
	ASSERT_TRUE(owner.start(throwingRequest(controller, client)));
	owner.wait();
	const auto completion = owner.peekCompletion();
	ASSERT_TRUE(completion);
	EXPECT_FALSE(completion->waitingForRealization());
	EXPECT_FALSE(completion->hasUncertainTransport());
	EXPECT_TRUE(completion->stillAwaitingInput());
	EXPECT_FALSE(client.waiting.tryRemovingElement(client.requestIds.front()));
}

TEST_F(InitialAutofightTaskTest, UnknownTransportDeliveryNeverAuthorizesManualRecovery)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	InitialAutofightTask owner;
	RequestClient client(owner, gameState());
	client.failInTransport = true;
	auto controller = std::make_shared<ProbeController>();
	ASSERT_TRUE(owner.start(throwingRequest(controller, client)));
	owner.wait();
	const auto completion = owner.peekCompletion();
	ASSERT_TRUE(completion);
	EXPECT_TRUE(completion->hasUncertainTransport());
	EXPECT_TRUE(completion->waitingForRealization());
	EXPECT_FALSE(completion->stillAwaitingInput()); // Production GUI closes this exact failed session normally.
}

TEST_F(InitialAutofightTaskTest, EveryIssuedIdMustBeRealizedNotOnlyTheLast)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	InitialAutofightTask owner;
	RequestClient client(owner, gameState());
	auto controller = std::make_shared<ProbeController>();
	ASSERT_TRUE(owner.start(throwingRequest(controller, client, 2)));
	owner.wait();
	ASSERT_EQ(client.requestIds.size(), 2);
	const auto player = *owner.peekCompletion()->source.callback->getPlayerID();
	ASSERT_TRUE(client.acknowledge(player, client.requestIds.back(), false));
	EXPECT_TRUE(owner.peekCompletion()->waitingForRealization());
	EXPECT_FALSE(owner.peekCompletion()->stillAwaitingInput());
	ASSERT_TRUE(client.acknowledge(player, client.requestIds.front(), false));
	EXPECT_TRUE(owner.peekCompletion()->stillAwaitingInput());
}
