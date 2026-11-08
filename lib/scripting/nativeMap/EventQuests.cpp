/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "EventRuntime.h"
#include "EventCodes.h"
#include "../../mapObjects/Quest.h"
#include "../../gameState/QuestInfo.h"
#include "../../callback/IGameEventCallback.h"

namespace scripting::nativeMap
{
namespace
{
void addToLog(IGameEventCallback & server, const CGObjectInstance & target, PlayerColor player)
{
	const auto * source = dynamic_cast<const IQuestSource *>(&target);
	if(!source) { logGlobal->error("Native addToQuestLog: object '%s' is not a quest source", target.getObjectNameTextID()); return; }
	server.addQuest(player, source->getQuestIdentity());
}
}
void Runtime::quest(IGameEventCallback & server, Continuation & context, const JsonNode & node)
{
	const auto & target = subject(context, node);
	const auto currentPlayer = player(context);
	const auto code = static_cast<HotaScriptActions>(node["op"].Integer());
	if(code == HotaScriptActions::DISABLE_EVENT || code == HotaScriptActions::REMOVE_CURRENT_OBJECT_OR_FINISH_QUEST)
	{
		if(!node["quest"].Bool() || dynamic_cast<const QuestGuard *>(&target)) server.removeObject(&target, target.getOwner());
		else if(dynamic_cast<const QuestGate *>(&target)) logGlobal->warn("Native finishQuest: quest gates are unsupported, as in the previous event implementation");
		else if(dynamic_cast<const SeerHut *>(&target)) server.setObjPropertyValue(target.id, ObjProperty::SEERHUT_COMPLETE, true);
		else throw std::runtime_error("Native finishQuest requires a quest source");
		return;
	}
	if(code == HotaScriptActions::SET_QUEST_HINT)
	{
		// The old hint implementation ignored images, but evaluating their amounts can draw RNG.
		components(server, context, node["images"]);
		server.setQuestHintText(target.id, MetaString::createFromTextID(node["text"].String()));
		if(node["showInLog"].Bool()) addToLog(server, target, currentPlayer);
		return;
	}
	if(condition(server, context, node["condition"]))
	{
		message(server, currentPlayer, node["completion"].String(), {});
		push(context, node["body"], currentPlayer);
	}
	else
	{
		const auto * source = dynamic_cast<const QuestSource *>(&target);
		const auto * active = source ? source->getActiveQuest() : nullptr;
		if(active && active->isKnownTo(currentPlayer)) message(server, currentPlayer, node["progression"].String(), {});
		else
		{
			server.setObjPropertyID(target.id, ObjProperty::SEERHUT_VISITED, currentPlayer);
			server.setQuestHintText(target.id, MetaString::createFromTextID(node["hint"].String()));
			addToLog(server, target, currentPlayer);
			message(server, currentPlayer, node["proposal"].String(), {});
		}
	}
}
}
