/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "EventRuntime.h"
#include "EventCodes.h"
#include "../../gameState/CGameState.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../mapObjects/CGTownInstance.h"
#include "../../mapObjects/CGPandoraBox.h"
#include "../../mapping/CMap.h"
#include "../../callback/IGameEventCallback.h"
#include "../../modding/ModScope.h"

namespace scripting::nativeMap
{
Runtime::Runtime(CGameState & state, JsonNode program, bool runInit)
	: state(state), program(std::move(program))
{
	// Imported HotA declarations are initialized by CGameState before dispatcher
	// creation. Optional authored native bindings only run for a fresh game.
	if(runInit)
		for(const auto & binding : this->program["bindings"].Vector())
		{
			const auto found = state.getMap().instanceNames.find(binding["object"].String());
			if(found == state.getMap().instanceNames.end()) throw std::runtime_error("Native event binding names a missing object");
			auto * object = dynamic_cast<CGPandoraBox *>(found->second.get());
			if(!object) throw std::runtime_error("Native event binding requires an event/pandora object");
			object->heroVisitScriptHandler = binding["handler"].String();
		}
}

PlayerColor Runtime::player(const Continuation & context) const { return context.frames.back().player; }
PlayerColor Runtime::resolvePlayer(const Continuation & context, const JsonNode & node) const
{
	const auto value = node["player"].Integer();
	return value < 0 ? player(context) : PlayerColor(value);
}
const CGHeroInstance & Runtime::hero(const Continuation & context) const
{
	const auto * result = state.getHero(context.hero);
	if(!result) throw std::runtime_error("Native map event requires a live visiting hero");
	return *result;
}
const CGTownInstance & Runtime::town(const Continuation & context) const
{
	const auto * result = dynamic_cast<const CGTownInstance *>(state.getObj(context.town, false));
	if(!result) throw std::runtime_error("Native map event requires a town");
	return *result;
}
const CGObjectInstance & Runtime::subject(const Continuation & context, const JsonNode & node) const
{
	if(node["town"].Bool()) return town(context);
	const auto * result = state.getObj(context.object, false);
	if(!result) throw std::runtime_error("Native map event requires a live subject object");
	return *result;
}
const CGObjectInstance * Runtime::referencedObject(const JsonNode & node) const
{
	const auto & name = program["objects"][node["object"].String()].String();
	const auto found = state.getMap().instanceNames.find(name);
	return found == state.getMap().instanceNames.end() ? nullptr : found->second.get();
}
std::string Runtime::variable(const JsonNode & node) const
{
	const auto & name = program["variables"][std::to_string(node["variable"].Integer())].String();
	if(name.empty()) throw std::runtime_error("Native map event references an undeclared variable");
	return name;
}
void Runtime::setVariable(IGameEventCallback & server, const std::string & name, double value) const
{
	server.setScriptVariable(ModScope::scopeMap(), name, JsonNode(value));
}
void Runtime::push(Continuation & context, const JsonNode & actions, PlayerColor currentPlayer)
{
	if(!actions.isNull()) context.frames.push_back({&actions, 0, currentPlayer, {}, 0, -1});
}
void Runtime::call(Continuation & context, const std::string & handler)
{
	const auto & event = program["handlers"][handler];
	if(event.isNull()) throw std::runtime_error("Native map event calls missing handler " + handler);
	const auto & bucket = event["bucket"].String();
	const auto currentPlayer = bucket == "townEvents" ? town(context).getOwner()
		: bucket == "playerEvents" ? (context.frames.empty() ? context.originalPlayer : player(context)) : hero(context).getOwner();
	push(context, event["actions"], currentPlayer);
}

std::optional<int> Runtime::begin(IGameEventCallback & server, const std::string & handler, Continuation context)
{
	if(program["handlers"][handler].isNull()) return std::nullopt;
	call(context, handler);
	const int handle = ++nextHandle;
	if(step(server, context)) return std::nullopt;
	continuations.emplace(handle, std::move(context));
	return handle;
}

std::optional<int> Runtime::onObjectVisit(IGameEventCallback & server, const std::string & handler,
	const CGObjectInstance * object, const CGHeroInstance * visitingHero)
{
	Continuation context;
	context.object = object->id; context.hero = visitingHero->id; context.originalPlayer = visitingHero->getOwner();
	return begin(server, handler, std::move(context));
}
std::optional<int> Runtime::onPlayerTurnStart(IGameEventCallback & server, const std::string & handler, PlayerColor currentPlayer)
{
	Continuation context; context.originalPlayer = currentPlayer;
	return begin(server, handler, std::move(context));
}
std::optional<int> Runtime::onTownTurnStart(IGameEventCallback & server, const std::string & handler, const CGTownInstance * currentTown)
{
	Continuation context; context.town = currentTown->id; context.originalPlayer = currentTown->getOwner();
	return begin(server, handler, std::move(context));
}

bool Runtime::resumeCoroutine(IGameEventCallback & server, int handle, std::optional<int> answer)
{
	const auto found = continuations.find(handle);
	if(found == continuations.end()) return true;
	auto & context = found->second;
	try
	{
		const auto * pending = std::exchange(context.pending, nullptr);
		if(pending)
		{
			const auto code = static_cast<HotaScriptActions>((*pending)["op"].Integer());
			if(code == HotaScriptActions::SHOW_QUESTION)
				push(context, (*pending)[answer == 1 ? "yes" : answer == 0 ? "no" : "cancel"], player(context));
			else if(code == HotaScriptActions::SHOW_REWARDS_MESSAGE) push(context, (*pending)["body"], player(context));
		}
		if(!step(server, context)) return false;
		continuations.erase(found);
		return true;
	}
	catch(...) { continuations.erase(found); throw; }
}

bool Runtime::step(IGameEventCallback & server, Continuation & context)
{
	while(!context.frames.empty())
	{
		auto & frame = context.frames.back();
		if(frame.position == frame.actions->Vector().size())
		{
			if(!frame.loopVariable.empty() && frame.loopNext <= frame.loopEnd)
			{
				setVariable(server, frame.loopVariable, frame.loopNext++);
				frame.position = 0;
				if(frame.actions->Vector().empty()) continue;
			}
			else { context.frames.pop_back(); continue; }
		}
		const auto & node = frame.actions->Vector().at(frame.position++);
		if(!action(server, context, node)) return false;
	}
	return true;
}
}
