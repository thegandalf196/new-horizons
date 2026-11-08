/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "HotaScriptConverter.h"
#include "MapReaderH3M.h"
#include "../scripting/nativeMap/EventCodes.h"

JsonNode HotaScriptConverter::loadActions()
{
	reader.readInt32();
	reader.readInt8();
	JsonNode result;
	result.Vector();
	const int count = reader.readInt32();
	for(int i = 0; i < count; ++i) result.Vector().push_back(loadAction(reader.readInt32()));
	return result;
}

JsonNode HotaScriptConverter::loadAction(int raw)
{
	JsonNode node;
	node["op"].Integer() = raw;
	switch(static_cast<HotaScriptActions>(raw))
	{
		case HotaScriptActions::SHOW_MESSAGE: case HotaScriptActions::SHOW_REWARDS_MESSAGE:
		case HotaScriptActions::QUEST_ACTION: case HotaScriptActions::CONDITIONAL:
		case HotaScriptActions::CONDITIONAL_CHAIN: case HotaScriptActions::LOOP_FOR:
		case HotaScriptActions::SET_QUEST_HINT: case HotaScriptActions::SHOW_QUESTION:
		case HotaScriptActions::EXECUTE_EVENT:
			loadControlAction(raw, node); break;
		case HotaScriptActions::REMOVE_CURRENT_OBJECT_OR_FINISH_QUEST:
			node["quest"].Bool() = currentBucket == "questEvents"; [[fallthrough]];
		case HotaScriptActions::DISABLE_EVENT:
			node["town"].Bool() = currentBucket == "townEvents"; break;
		case HotaScriptActions::MODIFY_VARIABLE:
			node["variable"].Integer() = reader.readInt32();
			node["mode"].Integer() = reader.readInt8();
			node["value"] = loadExpressionInternal(); break;
		case HotaScriptActions::SET_VARIABLE_CONDITIONAL:
			node["variable"].Integer() = reader.readInt32();
			node["condition"] = loadCondition();
			node["yes"] = loadExpression(); node["no"] = loadExpression(); break;
		default: loadRewardAction(raw, node); break;
	}
	return node;
}

void HotaScriptConverter::loadControlAction(int raw, JsonNode & node)
{
	switch(static_cast<HotaScriptActions>(raw))
	{
		case HotaScriptActions::SHOW_MESSAGE:
			node["text"].String() = localizedText("message");
			node["images"] = loadImageList(reader.readInt32()); break;
		case HotaScriptActions::SHOW_REWARDS_MESSAGE:
			node["text"].String() = localizedText("rewardMessage");
			node["body"] = loadActions(); break;
		case HotaScriptActions::QUEST_ACTION:
			node["condition"] = loadCondition();
			node["proposal"].String() = localizedText("questProposal");
			node["progression"].String() = localizedText("questProgression");
			node["completion"].String() = localizedText("questCompletion");
			node["hint"].String() = localizedText("questHint");
			node["body"] = loadActions(); reader.readBool();
			node["town"].Bool() = currentBucket == "townEvents"; break;
		case HotaScriptActions::CONDITIONAL:
			node["condition"] = loadCondition();
			node["yes"] = loadActions(); node["no"] = loadActions(); break;
		case HotaScriptActions::CONDITIONAL_CHAIN:
			for(;;)
			{
				JsonNode branch;
				branch["condition"] = loadCondition(); branch["body"] = loadActions();
				node["branches"].Vector().push_back(std::move(branch));
				reader.readBool();
				if(reader.readInt32() == 0) break;
			}
			reader.readInt32(); break;
		case HotaScriptActions::LOOP_FOR:
			node["body"] = loadActions();
			node["from"] = loadExpression(); node["to"] = loadExpression();
			node["variable"].Integer() = reader.readInt32(); break;
		case HotaScriptActions::SET_QUEST_HINT:
			node["text"].String() = localizedText("questHint");
			node["images"] = loadImageList(reader.readInt32());
			node["showInLog"].Bool() = reader.readBool();
			node["town"].Bool() = currentBucket == "townEvents"; break;
		case HotaScriptActions::SHOW_QUESTION:
		{
			const int mode = reader.readInt8();
			node["text"].String() = localizedText("question");
			node["yes"] = loadActions(); node["no"] = loadActions();
			if(mode == 2) node["cancel"] = loadActions();
			const int count = mode == 0 || mode == 3 ? reader.readInt32() : 2;
			node["images"] = loadImageList(count);
			if(mode == 1 || mode == 2) { reader.readBool(); reader.readInt32(); }
			break;
		}
		case HotaScriptActions::EXECUTE_EVENT:
		{
			const int bucket = reader.readInt32();
			const int id = reader.readInt32();
			const std::array<std::string, 4> names{"heroEvents", "playerEvents", "townEvents", "questEvents"};
			if(bucket < 0 || bucket >= static_cast<int>(names.size())) throw unsupported("Unknown event bucket");
			const auto & target = names[bucket];
			const auto category = [](const auto & value) { return value == "townEvents" ? 2 : value == "playerEvents" ? 1 : 0; };
			if(category(target) != category(currentBucket)) throw unsupported("EXECUTE_EVENT takes different parameters");
			node["handler"].String() = eventHandlerName(target, id); break;
		}
		default: throw unsupported("Invalid control event action");
	}
}
