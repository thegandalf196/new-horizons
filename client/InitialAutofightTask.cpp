/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "InitialAutofightTask.h"
#include "../lib/CStack.h"
#include "../lib/CThreadHelper.h"
#include "../lib/battle/BattleInfo.h"
#include "../lib/battle/CPlayerBattleCallback.h"
#include "../lib/callback/CBattleCallback.h"
#include "../lib/callback/CBattleGameInterface.h"
#include "../lib/gameState/CGameState.h"
#include <shared_mutex>
#include <algorithm>
#include <utility>

InitialAutofightTask::~InitialAutofightTask()
{
	requestStop();
	wait();
}

bool InitialAutofightTask::start(Request request)
{
	std::lock_guard lock(mutex);
	if(stopped.load() || pauses != 0 || joining || worker.joinable()
		|| !request.world || !request.callback || !request.controller
		|| request.callback->waitTillRealize)
		return false;
	try
	{
		completion.reset();
		issuingSource = request;
		issuingRequests.clear();
		worker = std::thread([this, request = std::move(request)]
		{
			const auto result = run(request); // All state/graph leases end here.
			std::lock_guard completedLock(mutex);
			completion = InitialAutofightCompletion{request, result, std::move(issuingRequests), std::nullopt};
			issuingSource.reset();
		});
		return true;
	}
	catch(const std::exception & error)
	{
		issuingSource.reset();
		logGlobal->error("Cannot start initial auto-fight task: %s", error.what());
		return false;
	}
}

void InitialAutofightTask::requestStop() noexcept
{
	stopped.store(true);
}


InitialAutofightTask::Pause::Pause(InitialAutofightTask & owner) : owner(owner)
{
	std::lock_guard lock(owner.mutex);
	++owner.pauses;
}

InitialAutofightTask::Pause::~Pause()
{
	std::lock_guard lock(owner.mutex);
	--owner.pauses;
}

InitialAutofightTask::Pause InitialAutofightTask::pauseAdmissions()
{
	return Pause(*this);
}

bool InitialAutofightTask::idle() const
{
	std::lock_guard lock(mutex);
	return !joining && !worker.joinable();
}

bool InitialAutofightTask::acceptsRequest() const
{
	std::lock_guard lock(mutex);
	return !stopped.load() && pauses == 0 && !joining && !worker.joinable();
}

void InitialAutofightTask::wait()
{
	std::unique_lock lock(mutex);
	finishedJoin.wait(lock, [this] { return !joining; });
	if(!worker.joinable())
		return;
	joining = true;
	auto joiningWorker = std::move(worker);
	lock.unlock();
	joiningWorker.join();
	lock.lock();
	joining = false;
	finishedJoin.notify_all();
}

InitialAutofightResult InitialAutofightTask::run(const Request & request) noexcept
{
	try
	{
		setThreadName("autofightingAI");
		std::shared_lock state(CGameState::mutex);
		if(stopped.load())
			return InitialAutofightResult::REFUSED;
		const auto * battle = request.world->getBattle(request.battle);
		if(!battle || battle->battleIsFinished())
			return InitialAutofightResult::REFUSED;
		const auto callback = request.callback->getBattle(request.battle);
		if(callback->getBattle() != battle)
			return InitialAutofightResult::REFUSED; // The request must belong to this exact retained source world.
		if(request.unit)
		{
			if(battle->getActivationSerial() != request.activationSerial
				|| battle->activeStack != static_cast<int64_t>(*request.unit))
				return InitialAutofightResult::REFUSED;
			const auto * unit = battle->battleGetStackByID(*request.unit, false);
			if(!unit || !unit->alive() || callback->battleGetTacticDist() != 0 || stopped.load())
				return InitialAutofightResult::REFUSED;
			request.controller->activeStack(request.battle, unit);
		}
		else
		{
			const int distance = callback->battleGetTacticDist();
			if(distance <= 0 || stopped.load())
				return InitialAutofightResult::REFUSED;
			request.controller->yourTacticPhase(request.battle, distance);
		}
		return InitialAutofightResult::COMPLETED;
	}
	catch(const std::exception & error)
	{
		logGlobal->error("Initial auto-fight task stopped: %s", error.what());
	}
	catch(...)
	{
		logGlobal->error("Initial auto-fight task stopped with an unknown exception");
	}
	return InitialAutofightResult::FAILED;
}

bool InitialAutofightTask::completed() const
{
	std::lock_guard lock(mutex);
	return completion.has_value();
}

std::optional<InitialAutofightCompletion> InitialAutofightTask::peekCompletion() const
{
	std::lock_guard lock(mutex);
	return completion;
}

std::optional<InitialAutofightCompletion> InitialAutofightTask::takeCompletion()
{
	std::lock_guard lock(mutex);
	return std::exchange(completion, std::nullopt);
}

bool InitialAutofightCompletion::stillAwaitingInput() const
{
	const auto pending = recoveryInput();
	if(!pending) return false;
	const auto & input = *pending;
	std::shared_lock state(CGameState::mutex);
	const auto * battle = input.world->getBattle(input.battle);
	if(!battle || battle->battleIsFinished() || battle->getActivationSerial() != input.activationSerial)
		return false;
	const auto callbacks = input.callback->getActiveBattles();
	const auto found = callbacks.find(input.battle);
	if(found == callbacks.end()) return false;
	const auto & callback = found->second;
	if(callback->getBattle() != battle)
		return false;
	if(!input.unit)
		return callback->battleGetTacticDist() > 0;
	const auto * unit = battle->battleGetStackByID(*input.unit, false);
	if(!unit || !unit->alive() || battle->activeStack != static_cast<int64_t>(*input.unit)
		|| callback->battleGetTacticDist() != 0) return false;
	const auto & crisis = battle->getCrisisCommandState();
	const auto player = crisis.choice() ? battle->sideToPlayer(crisis.chooser())
		: callback->battleGetActionController(unit); // Same chooser as real active-stack dispatch.
	return callback->getPlayerID() == player;
}

bool InitialAutofightTask::prepareRequest(const std::shared_ptr<CGameState> & world, PlayerColor player, uint32_t id, BattleID battle)
{
	std::lock_guard lock(mutex);
	if(!issuingSource || issuingSource->world != world || issuingSource->battle != battle
		|| issuingSource->callback->getPlayerID() != player)
		return false;
	if(std::any_of(issuingRequests.begin(), issuingRequests.end(), [id](const auto & r) { return r.id == id; }))
		throw std::logic_error("Duplicate initial auto-fight request ID");
	issuingRequests.push_back({id}); // Before transport or waitingRequest mutation.
	return true;
}

void InitialAutofightTask::enteringTransport(uint32_t id)
{
	std::lock_guard lock(mutex);
	auto & requests = completion ? completion->requests : issuingRequests;
	for(auto & receipt : requests)
		if(receipt.id == id) receipt.transportEntered = true;
}

void InitialAutofightTask::returnedFromTransport(uint32_t id)
{
	std::lock_guard lock(mutex);
	auto & requests = completion ? completion->requests : issuingRequests;
	for(auto & receipt : requests)
		if(receipt.id == id) receipt.transportReturned = true;
}

void InitialAutofightTask::failedInTransport(uint32_t id)
{
	std::lock_guard lock(mutex);
	auto & requests = completion ? completion->requests : issuingRequests;
	for(auto & receipt : requests)
		if(receipt.id == id) receipt.transportFailed = true;
}

void InitialAutofightTask::requestNotSubmitted(uint32_t id)
{
	std::lock_guard lock(mutex);
	auto & requests = completion ? completion->requests : issuingRequests;
	for(auto & receipt : requests)
		if(receipt.id == id && !receipt.transportEntered) receipt.notSubmitted = true;
}

void InitialAutofightTask::requestRealized(const std::shared_ptr<CGameState> & world, PlayerColor player, uint32_t id, bool accepted)
{
	std::lock_guard lock(mutex);
	const auto * source = completion ? &completion->source : (issuingSource ? &*issuingSource : nullptr);
	if(!source || source->world != world || source->callback->getPlayerID() != player) return;
	auto & requests = completion ? completion->requests : issuingRequests;
	for(auto & receipt : requests)
		if(receipt.id == id && receipt.transportEntered)
		{
			receipt.realized = true;
			receipt.accepted = accepted;
		}
}

bool InitialAutofightCompletion::hasUncertainTransport() const
{
	return std::any_of(requests.begin(), requests.end(), [](const auto & r)
	{
		return r.transportFailed && !r.transportReturned && !r.realized;
	});
}

bool InitialAutofightCompletion::waitingForRealization() const
{
	return std::any_of(requests.begin(), requests.end(), [](const auto & r)
	{
		return !r.notSubmitted && !r.realized;
	});
}

bool InitialAutofightTask::deferInput(const std::shared_ptr<CGameState> & world, PlayerColor player,
	BattleID battle, std::optional<uint32_t> unit, int32_t serial)
{
	std::lock_guard lock(mutex);
	if(!completion || completion->result != InitialAutofightResult::FAILED
		|| completion->source.world != world || completion->source.battle != battle
		|| completion->source.callback->getPlayerID() != player)
		return false;
	completion->deferredInput = completion->source;
	completion->deferredInput->unit = unit;
	completion->deferredInput->activationSerial = serial;
	return true;
}

std::optional<InitialAutofightRequest> InitialAutofightCompletion::recoveryInput() const
{
	if(hasUncertainTransport() || waitingForRealization()) return std::nullopt;
	if(deferredInput) return deferredInput;
	// Tactics has no consumed unit activation. After every exact ACK, its
	// still-active player phase is checked again by stillAwaitingInput().
	if(!source.unit) return source;
	// An accepted command can consume/change the old input even before its
	// effects reach the GUI. Only a real subsequent activation can replace it.
	if(std::any_of(requests.begin(), requests.end(), [](const auto & r) { return r.accepted; }))
		return std::nullopt;
	return source; // No request was sent, or every issued request was rejected.
}
