/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "LegacyHotaImport.h"
#include "../../constants/EntityIdentifiers.h"

namespace scripting::nativeMap::legacy
{
namespace
{
JsonNode opcode(int code) { JsonNode result; result["op"].Integer() = code; return result; }
int playerValue(const JsonNode & value)
{
	requireCall(value, "resolvePlayer", 2);
	const auto & args = value["args"].Vector();
	requireName(args[1], "player");
	return integer(args[0]);
}
int64_t variable(const JsonNode & value)
{
	if(value["kind"].String() != "index" || value["text"].String() != "Vars" || value["args"].Vector().size() != 1) unsupported("variable reference");
	return integer(value["args"].Vector().at(0));
}
}
JsonNode expression(const JsonNode & value)
{
	const auto & kind = value["kind"].String(), name = value["text"].String();
	const auto & args = value["args"].Vector();
	if(kind == "number") return JsonNode(integer(value));
	if(kind == "unary" && name == "-") { auto result = opcode(2); result["value"] = expression(args.at(0)); return result; }
	if(kind == "binary")
	{
		const std::map<std::string, int> codes{{"+",3},{"-",4},{"*",6},{"%",8}};
		const auto code = codes.find(name); if(code == codes.end()) unsupported("arithmetic operator " + name);
		auto result = opcode(code->second); result["left"] = expression(args.at(0)); result["right"] = expression(args.at(1)); return result;
	}
	if(kind != "call") unsupported("numeric expression " + name);
	const std::map<std::string, size_t> calls{{"math.floor",1},{"game:getMapVariable",1},{"game:getResource",2},
		{"hero:creatureCountInArmy",1},{"hero:ownedArtifacts",1},{"game:getDifficulty",0},{"compareDifficulty",2},
		{"game:getCalendar():getCurrentDay",0},{"hero:getExperience",0},{"hero:getLevel",0},{"hero:getPrimarySkill",1},{"server:random",2}};
	const auto found = calls.find(name);
	if(found == calls.end()) unsupported("numeric call " + name);
	requireCall(value, name, found->second);
	if(name == "compareDifficulty") requireName(args[0], "game");
	if(name == "math.floor")
	{
		const auto & division = args.at(0);
		if(division["kind"].String() != "binary" || division["text"].String() != "/") unsupported("integer division");
		auto result = opcode(7); result["left"] = expression(division["args"].Vector().at(0)); result["right"] = expression(division["args"].Vector().at(1)); return result;
	}
	if(name == "game:getMapVariable") { auto result = opcode(1); result["variable"].Integer() = variable(args.at(0)); return result; }
	if(name == "game:getResource")
	{
		auto result = opcode(5); result["player"].Integer() = playerValue(args.at(0)); result["entity"].String() = entity(args.at(1), "getResourceByName"); return result;
	}
	if(name == "hero:creatureCountInArmy" || name == "hero:ownedArtifacts")
	{
		auto result = opcode(name == "hero:creatureCountInArmy" ? 9 : 17);
		result["entity"].String() = entity(args.at(0), name == "hero:creatureCountInArmy" ? "getCreatureByName" : "getArtifactByName"); return result;
	}
	if(name == "game:getDifficulty") return opcode(10);
	if(name == "compareDifficulty") { auto result = opcode(11); result["value"].Integer() = integer(args.at(1)); return result; }
	if(name == "game:getCalendar():getCurrentDay") return opcode(12);
	if(name == "hero:getExperience") return opcode(13);
	if(name == "hero:getLevel") return opcode(14);
	if(name == "hero:getPrimarySkill") { auto result = opcode(15); result["skill"].Integer() = enumLiteral(args.at(0), "PrimarySkill"); return result; }
	if(name == "server:random") { auto result = opcode(16); result["left"] = expression(args.at(0)); result["right"] = expression(args.at(1)); return result; }
	unsupported("numeric call " + name);
}
JsonNode condition(const JsonNode & value)
{
	const auto & kind = value["kind"].String(), name = value["text"].String();
	const auto & args = value["args"].Vector();
	if(kind == "name" && (name == "true" || name == "false")) { auto result = opcode(0); result["value"].Bool() = name == "true"; return result; }
	if(kind == "unary" && name == "not") { auto result = opcode(6); result["condition"] = condition(args.at(0)); return result; }
	if(kind == "binary")
	{
		const auto & left = args.at(0), right = args.at(1);
		if(name == "and" || name == "or")
		{
			auto result = opcode(name == "and" ? 1 : 2);
			result["args"].Vector().push_back(condition(left)); result["args"].Vector().push_back(condition(right)); return result;
		}
		if(name == "==" && left["text"].String() == "player")
		{
			requireName(left, "player"); auto result = opcode(11);
			result["player"].Integer() = right["kind"].String() == "name" ? enumLiteral(right, "PlayerColor") : integer(right);
			return result;
		}
		if(name == "==" && left["text"].String() == "town:getOwner")
		{
			requireCall(left, "town:getOwner", 0);
			if(enumLiteral(right, "PlayerColor") != PlayerColor::NEUTRAL.getNum()) unsupported("neutral-town condition");
			return opcode(21);
		}
		if(name == ">=" && left["text"].String() == "hero:getSecondarySkill")
		{
			requireCall(left, "hero:getSecondarySkill", 1);
			auto result = opcode(16); result["entity"].String() = entity(left["args"].Vector().at(0), "getSecondarySkillByName"); result["mastery"].Integer() = integer(right); return result;
		}
		const std::map<std::string, int> codes{{"<",3},{">",4},{"==",5},{">=",8},{"<=",9},{"~=",10}};
		const auto code = codes.find(name); if(code == codes.end()) unsupported("comparison " + name);
		auto result = opcode(code->second); result["left"] = expression(left); result["right"] = expression(right); return result;
	}
	if(kind != "call") unsupported("condition " + name);
	const std::map<std::string, size_t> calls{{"hero:hasArtifact",1},{"heroOwner",3},{"playerDefeated",2},
		{"game:playerIsHuman",1},{"playerStartingFaction",3},{"playerOwnsTown",3},{"playerDestroyedObject",3}};
	const auto found = calls.find(name);
	if(found == calls.end()) unsupported("condition call " + name);
	requireCall(value, name, found->second);
	if(name != "hero:hasArtifact" && name != "game:playerIsHuman") requireName(args[0], "game");
	if(name == "hero:hasArtifact") { auto result = opcode(7); result["entity"].String() = entity(args.at(0), "getArtifactByName"); return result; }
	if(name == "heroOwner")
	{
		auto result = opcode(12); result["entity"].String() = entity(args.at(1), "getHeroTypeByName"); result["player"].Integer() = playerValue(args.at(2)); return result;
	}
	if(name == "playerDefeated" || name == "game:playerIsHuman")
	{
		auto result = opcode(name == "playerDefeated" ? 17 : 19); result["player"].Integer() = playerValue(args.at(name == "playerDefeated" ? 1 : 0)); return result;
	}
	if(name == "playerStartingFaction")
	{
		auto result = opcode(20); result["player"].Integer() = playerValue(args.at(1)); result["entity"].String() = entity(args.at(2), "getFactionByName"); return result;
	}
	if(name == "playerOwnsTown" || name == "playerDestroyedObject")
	{
		auto result = opcode(name == "playerOwnsTown" ? 18 : 14); result["player"].Integer() = playerValue(args.at(1));
		const auto & object = args.at(2);
		if(object["kind"].String() != "index" || object["text"].String() != "questObjects" || object["args"].Vector().size() != 1) unsupported("object identity");
		const auto key = literal(object["args"].Vector().at(0));
		if(key.size() != 8 || key.find_first_not_of("0123456789abcdef") != std::string::npos) unsupported("object identity key");
		result["object"].String() = std::to_string(std::stoul(key, nullptr, 16)); return result;
	}
	unsupported("condition call " + name);
}
}
