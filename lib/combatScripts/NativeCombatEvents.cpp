/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "NativeCombatEvents.h"
#include "nativeEvents/NativeEventCommon.h"

namespace nativeEvents
{
namespace
{
class Event final : public ICombatEventScript
{
	Handler handler;
	std::vector<CombatEventType> events;
	CombatEventForecastImplementation forecast;
public:
	Event(Handler handler, std::initializer_list<CombatEventType> events, CombatEventForecastImplementation forecast)
		: handler(handler), events(events), forecast(forecast) {}
	bool handlesEvent(const CBattleInfoCallback &, CombatEventType requested) const override
	{
		return std::find(events.begin(), events.end(), requested) != events.end();
	}
	CombatEventForecastImplementation forecastImplementation(const CBattleInfoCallback &) const override { return forecast; }
	void run(ServerCallback * server, const CBattleInfoCallback & battle, CombatEventType requested,
		const battle::Unit * self, const battle::Unit * other, const JsonNode & parameters,
		const CombatEventPayload & payload) const override
	{
		if(!handlesEvent(battle, requested))
			throw std::runtime_error("Native combat event called for an unhandled event");
		handler({*server, battle, self, other, parameters, payload});
	}
};
}

std::shared_ptr<ICombatEventScript> event(Handler handler, std::initializer_list<CombatEventType> events,
	CombatEventForecastImplementation forecast)
{
	return std::make_shared<Event>(handler, events, forecast);
}

std::shared_ptr<ICombatEventScript> create(const std::string & sourcePath)
{
	for(const auto factory : {attackEvent, recoveryEvent, reflectEvent, setupEvent, focusMagicEvent})
		if(auto script = factory(sourcePath))
			return script;
	if(sourcePath == "combat/arcaneBreach" || sourcePath == "combat/plagueStatus"
		|| sourcePath == "combat/soulChainStatus" || sourcePath == "combat/combatScript")
		return event(nullptr, {}); // Inert status identities, never dispatched.
	return nullptr;
}
}
