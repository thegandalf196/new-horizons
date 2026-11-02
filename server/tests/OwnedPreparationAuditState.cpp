/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedPreparationAudit.h"
#include <stdexcept>
namespace ownedPreparationAudit
{
Context::Context() = default;
Context::~Context() = default;
bool Context::claim()
{
	std::lock_guard lock(mutex);
	if(claimed || cancelled) return false;
	claimed = true; simulationThread = std::this_thread::get_id(); return true;
}
void Context::before(Observation value)
{
	std::lock_guard lock(mutex);
	if(!claimed || initial || simulationThread != std::this_thread::get_id())
		throw std::logic_error("Audit before observation must come from its claimed simulation thread");
	initial = std::move(value); condition.notify_all();
}
bool Context::awaitReady(BattleID battle, uint32_t target, int32_t activationSerial)
{
	std::unique_lock lock(mutex);
	condition.wait_until(lock, deadline, [&] { return initial.has_value() || cancelled; });
	return !cancelled && initial && initial->battle == battle && initial->target == target && initial->activationSerial == activationSerial;
}
void Context::analysisComplete()
{
	std::lock_guard lock(mutex);
	if(!initial || cancelled || analyzed || !clientDecisionVerified) throw std::logic_error("Audit analysis completion out of order");
	analyzed = true; condition.notify_all();
}
bool Context::awaitAnalysis()
{
	std::unique_lock lock(mutex);
	return condition.wait_until(lock, deadline, [&] { return analyzed || cancelled; }) && analyzed && !cancelled;
}
void Context::after(Observation value)
{
	std::lock_guard lock(mutex);
	if(!initial || !analyzed || final || cancelled || simulationThread != std::this_thread::get_id()
		|| value.battle != initial->battle || value.target != initial->target || value.activationSerial != initial->activationSerial)
		throw std::logic_error("Audit after observation must complete the same simulation-thread interval");
	final = std::move(value); condition.notify_all();
}
void Context::cancel(const std::string & reason)
{
	std::lock_guard lock(mutex);
	if(final) return; // Ordinary teardown cannot erase completed source observations.
	cancelled = true; if(failure.empty()) failure = reason; condition.notify_all();
}
bool Context::clientSourceVerified() const { std::lock_guard lock(mutex); return clientCaptureVerified && clientDecisionVerified && !cancelled; }
bool Context::finished() const { std::lock_guard lock(mutex); return final.has_value() || cancelled; }
std::string Context::problem() const { std::lock_guard lock(mutex); return failure; }
std::optional<std::pair<Observation, Observation>> Context::observations() const
{
	std::lock_guard lock(mutex);
	if(!initial || !final || cancelled) return std::nullopt;
	return std::pair{*initial, *final};
}
}
