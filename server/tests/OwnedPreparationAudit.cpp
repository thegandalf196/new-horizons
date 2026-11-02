/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedPreparationAudit.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/HealingReceipt.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/bonuses/NativeBonusGraphAccess.h"
#include "../../lib/bonuses/OwnedBonusPayloads.h"
#include "../../lib/bonuses/PreparedBonusGraphStorage.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include <unordered_set>
#include <stdexcept>
namespace ownedPreparationAudit
{
namespace
{
std::mutex registryMutex;
std::weak_ptr<Context> registered;
bool attached = false;
std::shared_ptr<Context> observationOwner()
{
	std::lock_guard lock(registryMutex);
	auto owner = registered.lock();
	if(!owner) throw std::logic_error("Actual integration source observation has no registered owner");
	return owner;
}
ClientSourceObservation observeClient(const CBattleInfoCallback & source, uint32_t active,
	int32_t serial, ESerializationVersion version)
{
	const auto * native = dynamic_cast<const BattleInfo *>(source.getBattle());
	if(!native || source.getPlayerID() != PlayerColor(0) || native->getActiveStackID() != active
		|| native->getActivationSerial() != serial)
		throw std::logic_error("Client source observation requires its actual current native callback");
	ClientSourceObservation result;
	result.battle = native->getBattleID(); result.active = active; result.serial = serial; result.version = version;
	std::vector<std::vector<std::shared_ptr<const Bonus>>> exports, cached;
	std::vector<std::shared_ptr<const Bonus>> roots;
	for(const auto * unit : source.battleGetUnitsIf([](const battle::Unit *) { return true; }))
	{
		const auto * stack = dynamic_cast<const CStack *>(unit);
		if(!stack || stack->getBattle() != native) throw std::logic_error("Foreign client observation occurrence");
		// Warm the baseline's real held query BEFORE the readonly comparison
		// interval. This explicit observer work is not attributed to capture.
		const auto held = stack->getAllBonuses(Selector::all, "owned-preparation-client-source-audit");
		ClientUnitObservation value;
		value.id = stack->unitId(); value.scalar = const_cast<CStack *>(stack)->save();
		value.stopped = stack->isTimeStopped(); value.healingReduction = battle::healingReceivedReduction(*stack);
		value.treeVersion = stack->getTreeVersion(); value.cachedOwner = std::shared_ptr<const void>(held, nullptr);
		result.units.push_back(std::move(value));
		const auto & local = stack->getExportedBonusList();
		exports.emplace_back(local.begin(), local.end()); cached.emplace_back(held->begin(), held->end());
		roots.insert(roots.end(), exports.back().begin(), exports.back().end());
		roots.insert(roots.end(), cached.back().begin(), cached.back().end());
	}
	ownedBonusPayloads::validateTypesAndTopology(roots);
	// Serialize VALUES only, never deserialize/clone a second decision pool.
	// One pointer identity map preserves nested aliases/order/cycles. Runtime
	// Bonus owners omitted by persistence are compared explicitly as well.
	std::vector<PlayerColor> owners; std::unordered_set<const Bonus *> seen;
	for(size_t i = 0; i < roots.size(); ++i)
	{
		const auto record = roots[i]; if(!record || !seen.insert(record.get()).second) continue;
		owners.push_back(record->bonusOwner);
		if(const auto * effects = record->parameters ? record->parameters->combatEffects() : nullptr)
			for(const auto & effect : effects->effects)
				if(const auto * child = std::get_if<BonusParametersOnCombatEvent::CombatEffectBonus>(&effect); child && child->bonus)
					roots.push_back(child->bonus);
	}
	CMemorySerializer wire; wire.oser.version = version;
	wire.oser & exports; wire.oser & cached; wire.oser & owners; result.metadata = wire.extractBuffer();
	result.generation = PreparedBonusGraphStorage::currentGeneration();
	return result; // No world/callback/unit/Bonus/native metadata pointer survives.
}
bool sameClient(const ClientSourceObservation & before, const ClientSourceObservation & after)
{
	if(before.battle != after.battle || before.active != after.active || before.serial != after.serial
		|| before.version != after.version || before.generation != after.generation
		|| before.metadata != after.metadata || before.units.size() != after.units.size()) return false;
	for(size_t i = 0; i < before.units.size(); ++i)
	{
		const auto & first = before.units[i]; const auto & last = after.units[i];
		if(first.id != last.id || first.scalar != last.scalar || first.stopped != last.stopped
			|| first.healingReduction != last.healingReduction || first.treeVersion != last.treeVersion
			|| first.cachedOwner.owner_before(last.cachedOwner) || last.cachedOwner.owner_before(first.cachedOwner)) return false;
	}
	return true;
}
Observation observe(CGameHandler & handler, const CBattleInfoCallback & battle, uint32_t id)
{
	NativeBonusGraphAccess read; // Retired BEFORE either rendezvous wait.
	const auto * source = dynamic_cast<const CStack *>(battle.battleGetUnitByID(id));
	auto * random = dynamic_cast<CRandomGenerator *>(&handler.getRandomGenerator());
	if(!source || !random) throw std::runtime_error("Audit requires actual native stack and simulation random stream");
	Observation value; value.battle = battle.getBattle()->getBattleID(); value.target = id; value.activationSerial = battle.getBattle()->getActivationSerial();
	value.scalar = const_cast<CStack *>(source)->save();
	value.stopped = source->isTimeStopped(); value.healingReduction = battle::healingReceivedReduction(*source);
	CMemorySerializer wire; wire.oser & *random; value.simulationRng = wire.extractBuffer();
	return value;
}
}
void registerContext(const std::shared_ptr<Context> & context)
{
	std::lock_guard lock(registryMutex);
	if(!context || attached) throw std::logic_error("Audit context attaches only once per integration process");
	attached = true; registered = context;
}
void afterActivation(CGameHandler & handler, const CBattleInfoCallback & battle, const battle::Unit & actor)
{
	std::shared_ptr<Context> owner;
	{ std::lock_guard lock(registryMutex); owner = registered.lock(); }
	if(!owner || actor.unitOwner() != PlayerColor(0) || actor.unitSide() != BattleSide::ATTACKER
		|| actor.creatureId() != CreatureID(CreatureID::decode("core:angel")) || actor.getCount() != 2 || battle.battleGetRound() != 1)
		return;
	if(!owner->claim()) return;
	try
	{
		const auto id = actor.unitId(); const auto battleID = battle.getBattle()->getBattleID();
		if(handler.gameState().getBattle(battleID) != battle.getBattle() || battle.getBattle()->getActiveStackID() != id)
			throw std::runtime_error("Audit source is not the actual completed native activation");
		// All legitimate activation work has already finished. The client waits
		// for this observation BEFORE executing owned analysis. No gameplay work
		// is admitted here while the same simulation thread awaits completion.
		owner->before(observe(handler, battle, id));
		if(!owner->awaitAnalysis()) throw std::runtime_error("Owned analysis audit was cancelled or timed out");
		owner->after(observe(handler, battle, id));
	}
	catch(const std::exception & error) { owner->cancel(error.what()); }
}
void beforeClientCapture(const CBattleInfoCallback & source, uint32_t active, int32_t serial, ESerializationVersion version)
{
	auto owner = observationOwner(); auto captured = observeClient(source, active, serial, version);
	std::lock_guard lock(owner->mutex);
	if(owner->cancelled || !owner->initial || owner->clientBefore || owner->initial->battle != captured.battle
		|| owner->initial->target != active || owner->initial->activationSerial != serial)
		throw std::logic_error("Client source baseline is outside the actual quiescent observation");
	const auto target = std::find_if(captured.units.begin(), captured.units.end(), [active](const auto & unit) { return unit.id == active; });
	if(target == captured.units.end() || target->scalar != owner->initial->scalar
		|| target->stopped != owner->initial->stopped || target->healingReduction != owner->initial->healingReduction)
		throw std::logic_error("Actual decoded client target differs from quiescent simulation target");
	owner->clientBefore = std::move(captured);
}
void afterClientCapture(const CBattleInfoCallback & source, uint32_t active, int32_t serial, ESerializationVersion version)
{
	auto owner = observationOwner(); const auto captured = observeClient(source, active, serial, version);
	std::lock_guard lock(owner->mutex);
	if(owner->cancelled || !owner->clientBefore || owner->clientCaptureVerified || !sameClient(*owner->clientBefore, captured))
		throw std::logic_error("Actual client source/held cache/counters changed during owned preparation");
	owner->clientCaptureVerified = true;
}
void afterClientDecision(const CBattleInfoCallback & source, uint32_t active, int32_t serial, ESerializationVersion version)
{
	auto owner = observationOwner(); const auto captured = observeClient(source, active, serial, version);
	std::lock_guard lock(owner->mutex);
	if(owner->cancelled || !owner->clientBefore || !owner->clientCaptureVerified || owner->clientDecisionVerified
		|| !sameClient(*owner->clientBefore, captured))
		throw std::logic_error("Actual client source/held cache/counters changed during owned analysis");
	owner->clientDecisionVerified = true;
}

}
