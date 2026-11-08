/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "HotaScriptConverter.h"
#include "MapReaderH3M.h"
#include "../scripting/nativeMap/EventCodes.h"

JsonNode HotaScriptConverter::loadCondition()
{
	reader.readBool();
	return loadConditionInternal();
}

JsonNode HotaScriptConverter::loadConditionInternal()
{
	const auto code = static_cast<HotaScriptCondition>(reader.readInt32());
	JsonNode node;
	node["op"].Integer() = static_cast<int>(code);
	switch(code)
	{
		case HotaScriptCondition::CONSTANT: node["value"].Bool() = reader.readBool(); break;
		case HotaScriptCondition::ALL_OF:
		case HotaScriptCondition::ANY_OF:
		{
			const int count = reader.readInt32();
			for(int i = 0; i < count; ++i) node["args"].Vector().push_back(loadConditionInternal());
			break;
		}
		case HotaScriptCondition::LESSER: case HotaScriptCondition::GREATER:
		case HotaScriptCondition::EQUAL: case HotaScriptCondition::GREATER_OR_EQUAL:
		case HotaScriptCondition::LESSER_OR_EQUAL: case HotaScriptCondition::NOT_EQUAL:
			node["left"] = loadExpression(); node["right"] = loadExpression(); break;
		case HotaScriptCondition::NOT: node["condition"] = loadCondition(); break;
		case HotaScriptCondition::HAS_ARTIFACT:
			node["entity"].String() = ArtifactID::encode(reader.readArtifact32().getNum());
			reader.readSpell32(); break;
		case HotaScriptCondition::CURRENT_PLAYER:
		case HotaScriptCondition::PLAYER_DEFEATED:
		case HotaScriptCondition::PLAYER_IS_HUMAN:
			node["player"].Integer() = reader.readPlayer32().getNum(); break;
		case HotaScriptCondition::HERO_OWNER:
			node["entity"].String() = HeroTypeID::encode(reader.readHero32().getNum());
			node["player"].Integer() = reader.readPlayer32().getNum(); break;
		case HotaScriptCondition::HERO_SECONDARY_SKILL:
			node["entity"].String() = SecondarySkill::encode(reader.readSkill32().getNum());
			node["mastery"].Integer() = reader.readInt32(); break;
		case HotaScriptCondition::TOWN_IS_NEUTRAL: break;
		case HotaScriptCondition::PLAYER_STARTING_FACTION:
			node["player"].Integer() = reader.readPlayer32().getNum();
			node["entity"].String() = FactionID::encode(reader.readFaction32().getNum()); break;
		case HotaScriptCondition::PLAYER_DEFEATED_MONSTER:
		case HotaScriptCondition::PLAYER_DEFEATED_HERO:
		case HotaScriptCondition::PLAYER_OWNS_TOWN:
			node["player"].Integer() = reader.readPlayer32().getNum();
			node["object"].String() = questObjectRef(reader.readUInt32()); break;
		default: throw unsupported("Unknown event condition code " + std::to_string(static_cast<int>(code)));
	}
	return node;
}

JsonNode HotaScriptConverter::loadExpression()
{
	if(reader.readBool()) return loadExpressionInternal();
	JsonNode result;
	result.Integer() = reader.readInt32();
	return result;
}

JsonNode HotaScriptConverter::loadExpressionInternal()
{
	reader.readBool();
	const auto code = static_cast<HotaScriptExpression>(reader.readInt32());
	JsonNode node;
	node["op"].Integer() = static_cast<int>(code);
	switch(code)
	{
		case HotaScriptExpression::INTEGER_VALUE: node["value"].Integer() = reader.readInt32(); break;
		case HotaScriptExpression::VARIABLE_VALUE: node["variable"].Integer() = reader.readInt32(); break;
		case HotaScriptExpression::NEGATE:
			reader.readInt32(); node["value"] = loadExpression(); break;
		case HotaScriptExpression::ADD: case HotaScriptExpression::SUBSTRACT:
		case HotaScriptExpression::MULTIPLY: case HotaScriptExpression::DIVIDE: case HotaScriptExpression::REMAINDER:
			node["left"] = loadExpressionInternal(); node["right"] = loadExpressionInternal(); break;
		case HotaScriptExpression::RESOURCE:
			node["player"].Integer() = reader.readPlayer().getNum();
			node["entity"].String() = GameResID::encode(reader.readGameResID32().getNum()); break;
		case HotaScriptExpression::CREATURE_COUNT_IN_ARMY:
			node["entity"].String() = CreatureID::encode(reader.readCreature32().getNum()); break;
		case HotaScriptExpression::CURRENT_DIFFICULTY: case HotaScriptExpression::CURRENT_DATE:
		case HotaScriptExpression::HERO_EXPERIENCE: case HotaScriptExpression::HERO_LEVEL: break;
		case HotaScriptExpression::COMPARE_DIFFICULTY: node["value"].Integer() = reader.readInt32(); break;
		case HotaScriptExpression::HERO_PRIMARY_SKILL: node["skill"].Integer() = reader.readInt32(); break;
		case HotaScriptExpression::RANDOM_NUMBER:
			node["left"] = loadExpression(); node["right"] = loadExpression(); break;
		case HotaScriptExpression::HERO_OWNED_ARTIFACTS:
			node["entity"].String() = ArtifactID::encode(reader.readArtifact32().getNum());
			reader.readSpell32(); break;
		default: throw unsupported("Unknown event expression code " + std::to_string(static_cast<int>(code)));
	}
	return node;
}
