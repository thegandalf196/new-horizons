/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../lib/constants/EntityIdentifiers.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

class CGameState;
class CBattleCallback;
class CBattleGameInterface;

enum class InitialAutofightResult { COMPLETED, REFUSED, FAILED };

struct InitialAutofightRequest
{
	std::shared_ptr<CGameState> world;
	std::shared_ptr<CBattleCallback> callback;
	std::shared_ptr<CBattleGameInterface> controller;
	BattleID battle;
	std::optional<uint32_t> unit;
	int32_t activationSerial = 0;
};

struct InitialAutofightRequestReceipt
{
	uint32_t id;
	bool transportEntered = false;
	bool transportReturned = false;
	bool transportFailed = false;
	bool notSubmitted = false;
	bool realized = false;
	bool accepted = false;
};

struct InitialAutofightCompletion
{
	InitialAutofightRequest source;
	InitialAutofightResult result;
	std::vector<InitialAutofightRequestReceipt> requests;
	std::optional<InitialAutofightRequest> deferredInput;
	std::optional<InitialAutofightRequest> recoveryInput() const;
	bool hasUncertainTransport() const;
	bool waitingForRealization() const;
	bool stillAwaitingInput() const;
};

/// Only the GUI-to-initial-auto-fight handoff, not an AI scheduler. The client
/// drains before ANY packet call-in, session replacement or source retirement.
class InitialAutofightTask final
{
public:
	using Request = InitialAutofightRequest;

	~InitialAutofightTask();
	bool start(Request request);
	void requestStop() noexcept;
	class Pause final
	{
		friend class InitialAutofightTask;
		InitialAutofightTask & owner;
		explicit Pause(InitialAutofightTask & owner);
	public:
		~Pause();
		Pause(const Pause &) = delete;
		Pause & operator=(const Pause &) = delete;
	};
	/// Retain the owner through this scope. Nested packet/drain pauses compose.
	Pause pauseAdmissions();
	/// Caller holds NO interface mutex, state lease or root pin. Concurrent
	/// drainers all wait for the actual thread exit, including another joiner.
	void wait();
	bool idle() const;
	bool acceptsRequest() const;
	bool completed() const;
	/// Observes the existing CClient request/PackageApplied IDs only for this
	/// exact running source+player. No gameplay command is created here.
	bool prepareRequest(const std::shared_ptr<CGameState> & world, PlayerColor player, uint32_t id, BattleID battle);
	void enteringTransport(uint32_t id);
	void returnedFromTransport(uint32_t id);
	void failedInTransport(uint32_t id);
	void requestNotSubmitted(uint32_t id);
	void requestRealized(const std::shared_ptr<CGameState> & world, PlayerColor player, uint32_t id, bool accepted);
	/// Only called from real authoritative active/deployment dispatch, never a
	/// synthetic reactivation. Keeps VALUES while exact acknowledgements arrive.
	bool deferInput(const std::shared_ptr<CGameState> & world, PlayerColor player,
		BattleID battle, std::optional<uint32_t> unit, int32_t serial);
	std::optional<InitialAutofightCompletion> peekCompletion() const;
	std::optional<InitialAutofightCompletion> takeCompletion();

private:
	InitialAutofightResult run(const Request & request) noexcept;
	mutable std::mutex mutex;
	std::condition_variable finishedJoin;
	std::thread worker;
	std::optional<InitialAutofightCompletion> completion;
	std::optional<Request> issuingSource;
	std::vector<InitialAutofightRequestReceipt> issuingRequests;
	bool joining = false;
	size_t pauses = 0;
	std::atomic<bool> stopped{false};
};
