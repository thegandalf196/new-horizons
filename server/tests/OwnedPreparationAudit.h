/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#ifndef NH_OWNED_PREPARATION_SIMULATION_AUDIT
#error Owned preparation simulation audit belongs only to its dedicated integration target
#endif
#include "../../lib/constants/EntityIdentifiers.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/serializer/ESerializationVersion.h"
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <cstddef>
#include <string>
#include <vector>
class CGameHandler;
class CBattleInfoCallback;
namespace battle { class Unit; }
namespace ownedPreparationAudit
{
struct Observation
{
	BattleID battle = BattleID::NONE;
	uint32_t target = 0;
	int32_t activationSerial = 0;
	JsonNode scalar;
	bool stopped = false;
	int healingReduction = 0;
	std::vector<std::byte> simulationRng;
};
/// Internal source-observation values only; none is candidate/capture authority.
/// Bonus metadata stays private in Context and is never printed or exported.
struct ClientUnitObservation
{
	uint32_t id = 0;
	JsonNode scalar;
	bool stopped = false;
	int healingReduction = 0, treeVersion = 0;
	std::weak_ptr<const void> cachedOwner; // Null-alias control block only, no native object/callable.
};
struct ClientSourceObservation
{
	BattleID battle = BattleID::NONE;
	uint32_t active = 0;
	int32_t serial = 0, generation = 0;
	ESerializationVersion version{};
	std::vector<ClientUnitObservation> units;
	std::vector<std::byte> metadata;
};
void beforeClientCapture(const CBattleInfoCallback &, uint32_t, int32_t, ESerializationVersion);
void afterClientCapture(const CBattleInfoCallback &, uint32_t, int32_t, ESerializationVersion);
void afterClientDecision(const CBattleInfoCallback &, uint32_t, int32_t, ESerializationVersion);
/// Test rendezvous of VALUES and completion only. Never owns source/world/Batch,
/// supplies preparation authority, mutates gameplay or sends a player request.
class DLL_LINKAGE Context final
{
	mutable std::mutex mutex;
	std::condition_variable condition;
	std::optional<Observation> initial, final;
	std::optional<std::thread::id> simulationThread;
	bool claimed = false, analyzed = false, cancelled = false;
	std::optional<ClientSourceObservation> clientBefore;
	bool clientCaptureVerified = false, clientDecisionVerified = false;
	friend void beforeClientCapture(const CBattleInfoCallback &, uint32_t, int32_t, ESerializationVersion);
	friend void afterClientCapture(const CBattleInfoCallback &, uint32_t, int32_t, ESerializationVersion);
	friend void afterClientDecision(const CBattleInfoCallback &, uint32_t, int32_t, ESerializationVersion);
	std::string failure;
	const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(240);
public:
	Context();
	~Context();
	bool claim();
	void before(Observation value);
	bool awaitReady(BattleID battle, uint32_t target, int32_t activationSerial);
	void analysisComplete();
	bool awaitAnalysis();
	void after(Observation value);
	void cancel(const std::string & reason);
	bool finished() const;
	bool clientSourceVerified() const;
	std::string problem() const;
	std::optional<std::pair<Observation, Observation>> observations() const;
};
void registerContext(const std::shared_ptr<Context> & context);
/// Called at end of the actual canonical setActiveStack, after activation
/// mutations. Its wait starts AFTER every source/packet/graph scope retired.
void afterActivation(CGameHandler &, const CBattleInfoCallback &, const battle::Unit & actor);
}
