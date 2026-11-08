/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "LegacyHotaImport.h"
#include "../../constants/EntityIdentifiers.h"

namespace scripting::nativeMap::legacy
{
namespace
{
JsonNode opcode(int code) { JsonNode result; result["op"].Integer() = code; return result; }
int64_t variable(const JsonNode & value)
{
	if(value["kind"].String() != "index" || value["text"].String() != "Vars" || value["args"].Vector().size() != 1) unsupported("variable target");
	return integer(value["args"].Vector().at(0));
}
const JsonNode & body(const JsonNode & value)
{
	if(value["kind"].String() != "function") unsupported("expected action closure");
	return value["body"];
}
}
std::string text(const JsonNode & value)
{
	requireRecord(value, {"append"});
	requireArray(value["fields"]["append"], 1);
	return literal(value["fields"]["append"]["args"].Vector().at(0));
}
JsonNode images(const JsonNode & value)
{
	if(value["kind"].String() == "call") requireCall(value, "toComponents", 1);
	const auto & table = value["kind"].String() == "call" ? value["args"].Vector().at(0) : value;
	requireArray(table);
	JsonNode result; result.Vector();
	for(const auto & image : table["args"].Vector())
	{
		requireArray(image, 3);
		const auto & args = image["args"].Vector();
		JsonNode item; item["type"].Integer() = integer(args[0]); item["subtype"].Integer() = integer(args[1]); item["value"] = expression(args[2]);
		result.Vector().push_back(std::move(item));
	}
	return result;
}
JsonNode actions(const JsonNode & value, const std::string & bucket)
{
	JsonNode result; result.Vector();
	for(const auto & statement : value.Vector()) result.Vector().push_back(action(statement, bucket));
	return result;
}
JsonNode action(const JsonNode & value, const std::string & bucket)
{
	const auto & kind = value["kind"].String(), name = value["text"].String();
	const auto & args = value["args"].Vector();
	if(kind == "if")
	{
		auto result = opcode(1);
		for(const auto & branch : value["branches"].Vector())
		{
			JsonNode item; item["condition"] = condition(branch["condition"]); item["body"] = actions(branch["body"], bucket);
			result["branches"].Vector().push_back(std::move(item));
		}
		if(!value["otherwise"].isNull())
		{
			JsonNode branch; branch["condition"]["op"].Integer() = 0; branch["condition"]["value"].Bool() = true;
			branch["body"] = actions(value["otherwise"], bucket); result["branches"].Vector().push_back(std::move(branch));
		}
		return result;
	}
	if(kind == "for")
	{
		auto statements = value["body"];
		if(statements.Vector().empty()) unsupported("loop missing variable assignment");
		const auto & first = statements.Vector().front();
		requireCall(first, "server:setMapVariable", 2);
		requireName(first["args"].Vector()[1], "__i");
		auto result = opcode(28); result["variable"].Integer() = variable(first["args"].Vector().at(0));
		statements.Vector().erase(statements.Vector().begin()); result["body"] = actions(statements, bucket);
		result["from"] = expression(value["from"]); result["to"] = expression(value["to"]); return result;
	}
	if(kind != "call") unsupported("action statement");
	validateActionCall(value, bucket);
	if(name == "server:setMapVariable")
	{
		auto result = opcode(3); result["variable"].Integer() = variable(args.at(0)); result["mode"].Integer() = 2; result["value"] = expression(args.at(1)); return result;
	}
	if(name.compare(0, 4, "Map:") == 0) { auto result = opcode(24); result["handler"].String() = name.substr(4); return result; }
	if(name == "server:removeObject" || name == "server:finishQuestOrRemoveObject")
	{
		auto result = opcode(5); result["quest"].Bool() = name == "server:finishQuestOrRemoveObject"; result["town"].Bool() = bucket == "townEvents"; return result;
	}
	if(name == "server:showMessage") { requireCall(args[2], "toComponents", 1); auto result = opcode(29); result["text"].String() = text(args[1]); result["images"] = images(args[2]); return result; }
	if(name == "server:showRewardsMessage") { auto result = opcode(6); result["text"].String() = text(args.at(1)); result["body"] = actions(body(args.at(2)), bucket); return result; }
	if(name == "server:showQuestion")
	{
		requireRecord(args[0], {"text", "images", "onYes", "onNo"}, {"onCancel"});
		const auto & fields = args.at(0)["fields"];
		requireArray(fields["images"]);
		auto result = opcode(12); result["text"].String() = text(fields["text"]); result["images"] = images(fields["images"]);
		result["yes"] = actions(body(fields["onYes"]), bucket); result["no"] = actions(body(fields["onNo"]), bucket);
		if(!fields["onCancel"].isNull()) result["cancel"] = actions(body(fields["onCancel"]), bucket);
		return result;
	}
	if(name == "registerQuest")
	{
		requireRecord(args[4], {"proposal", "progression", "completion", "hint", "condition", "reward"});
		const auto & fields = args.at(4)["fields"];
		auto result = opcode(7); result["town"].Bool() = bucket == "townEvents";
		for(const auto key : {"proposal", "progression", "completion", "hint"}) result[key].String() = text(fields[key]);
		const auto & predicate = body(fields["condition"]).Vector();
		if(predicate.size() != 1 || predicate[0]["kind"].String() != "return") unsupported("quest condition closure");
		result["condition"] = condition(predicate[0]["value"]); result["body"] = actions(body(fields["reward"]), bucket); return result;
	}
	if(name == "setQuestHint")
	{
		requireCall(args[4], "toComponents", 1);
		auto result = opcode(11); result["town"].Bool() = bucket == "townEvents"; result["text"].String() = text(args.at(3));
		result["images"] = images(args.at(4)); result["showInLog"].Bool() = booleanLiteral(args[5]); return result;
	}
	if(name == "grantResources")
	{
		requireArray(args[2], 7);
		auto result = opcode(4); for(const auto & item : args.at(2)["args"].Vector()) result["amounts"].Vector().push_back(expression(item)); return result;
	}
	if(name == "server:startCombat")
	{
		requireArray(args[1], 7);
		auto result = opcode(23);
		for(const auto & slot : args.at(1)["args"].Vector())
		{
			requireArray(slot, 2);
			JsonNode item; item["amount"] = expression(slot["args"].Vector().at(0)); item["entity"].String() = literal(slot["args"].Vector().at(1)); result["army"].Vector().push_back(std::move(item));
		}
		return result;
	}
	const std::map<std::string, int> codes{{"takeArtifact",9},{"grantArtifact",9},{"grantScroll",9},
		{"takeWarMachine",25},{"grantWarMachine",25},{"takeSpellbook",26},{"grantSpellbook",26},
		{"grantCreatures",8},{"takeCreatures",8},{"grantSpell",15},{"giveExperience",16},{"grantPrimarySkill",19},
		{"grantSecondarySkill",20},{"grantLuck",21},{"grantMorale",22},{"grantSpellPoints",17},{"grantMovementPoints",18},
		{"grantCreaturesToHire",14},{"constructBuilding",10}};
	if(name.compare(0, 7, "server:") != 0) unsupported("action call " + name);
	const auto method = name.substr(7); const auto found = codes.find(method);
	if(found == codes.end()) unsupported("action call " + name);
	auto result = opcode(found->second); result["take"].Bool() = method.compare(0, 4, "take") == 0;
	switch(found->second)
	{
		case 9: case 25:
			if(method == "grantScroll") { result["entity"].String() = ""; result["scroll"].String() = entity(args.at(1), "getSpellByName"); }
			else result["entity"].String() = entity(args.at(1), "getArtifactByName");
			break;
		case 26: break;
		case 8: result["entity"].String() = entity(args.at(1), "getCreatureByName"); result["amount"] = expression(args.at(2)); break;
		case 15: result["entity"].String() = entity(args.at(1), "getSpellByName"); break;
		case 20: result["entity"].String() = entity(args.at(1), "getSecondarySkillByName"); result["mastery"].Integer() = integer(args.at(2)); break;
		case 19:
		{
			result["skill"].Integer() = enumLiteral(args[1], "PrimarySkill"); result["amount"] = expression(args[2]); break;
		}
		case 10: result["building"].Integer() = integer(args.at(1)); break;
		case 14: result["level"].Integer() = integer(args.at(1)); result["amount"] = expression(args.at(2)); break;
		case 21: case 22: result["amount"].Integer() = integer(args.at(1)); break;
		default: result["amount"] = expression(args.at(1)); if(found->second == 17 || found->second == 18) result["mode"].Integer() = integer(args.at(2)); break;
	}
	return result;
}
}
