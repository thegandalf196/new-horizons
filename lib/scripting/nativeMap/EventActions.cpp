/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "EventRuntime.h"
#include "EventCodes.h"
#include "../../gameState/CGameState.h"
#include "../../mapping/CMap.h"
#include "../../callback/IGameEventCallback.h"
#include "../../modding/ModScope.h"

namespace scripting::nativeMap
{
bool Runtime::action(IGameEventCallback & server, Continuation & context, const JsonNode & node)
{
	const auto currentPlayer = player(context);
	switch(static_cast<HotaScriptActions>(node["op"].Integer()))
	{
		case HotaScriptActions::MODIFY_VARIABLE:
		{
			const auto name = variable(node);
			const auto mode = node["mode"].Integer();
			const double previous = mode < 2 ? state.getMap().getScriptVariables().get(ModScope::scopeMap(), name).Float() : 0;
			const double value = expression(server, context, node["value"]);
			setVariable(server, name, mode == 0 ? previous + value : mode == 1 ? previous - value : value);
			break;
		}
		case HotaScriptActions::SET_VARIABLE_CONDITIONAL:
		{
			const bool yes = condition(server, context, node["condition"]);
			setVariable(server, variable(node), expression(server, context, node[yes ? "yes" : "no"]));
			break;
		}
		case HotaScriptActions::CONDITIONAL:
			push(context, node[condition(server, context, node["condition"]) ? "yes" : "no"], currentPlayer);
			break;
		case HotaScriptActions::CONDITIONAL_CHAIN:
			for(const auto & branch : node["branches"].Vector())
				if(condition(server, context, branch["condition"])) { push(context, branch["body"], currentPlayer); break; }
			break;
		case HotaScriptActions::LOOP_FOR:
		{
			const double first = expression(server, context, node["from"]);
			const double last = expression(server, context, node["to"]);
			if(first <= last)
			{
				const auto name = variable(node);
				setVariable(server, name, first);
				context.frames.push_back({&node["body"], 0, currentPlayer, name, first + 1, last});
			}
			break;
		}
		case HotaScriptActions::EXECUTE_EVENT: call(context, node["handler"].String()); break;
		case HotaScriptActions::SHOW_MESSAGE:
			message(server, currentPlayer, node["text"].String(), components(server, context, node["images"])); break;
		case HotaScriptActions::SHOW_QUESTION:
			context.pending = &node;
			dialog(server, context.originalPlayer, node["text"].String(), true, components(server, context, node["images"]));
			return false;
		case HotaScriptActions::SHOW_REWARDS_MESSAGE:
			context.pending = &node;
			dialog(server, currentPlayer, node["text"].String(), false, {});
			return false;
		case HotaScriptActions::START_COMBAT:
			context.pending = &node;
			combat(server, context, node);
			return false;
		case HotaScriptActions::QUEST_ACTION: case HotaScriptActions::SET_QUEST_HINT:
		case HotaScriptActions::REMOVE_CURRENT_OBJECT_OR_FINISH_QUEST: case HotaScriptActions::DISABLE_EVENT:
			quest(server, context, node); break;
		default: reward(server, context, node); break;
	}
	return true;
}
}
