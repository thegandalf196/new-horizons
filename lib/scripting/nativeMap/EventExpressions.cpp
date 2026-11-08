/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "EventRuntime.h"
#include "EventCodes.h"
#include "../../gameState/CGameState.h"
#include "../../mapping/CMap.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../entities/artifact/CArtifactInstance.h"
#include "../../CCreatureHandler.h"
#include "../../callback/IGameEventCallback.h"
#include "../../StartInfo.h"
#include "../../modding/ModScope.h"
#include <vstd/RNG.h>
#include <cmath>

namespace scripting::nativeMap
{
double Runtime::expression(IGameEventCallback & server, const Continuation & context, const JsonNode & node) const
{
	if(node.isNumber()) return node.Float();
	const auto code = static_cast<HotaScriptExpression>(node["op"].Integer());
	// Separate statements preserve the generated program's left-to-right RNG draws.
	if(code == HotaScriptExpression::ADD || code == HotaScriptExpression::SUBSTRACT
		|| code == HotaScriptExpression::MULTIPLY || code == HotaScriptExpression::DIVIDE
		|| code == HotaScriptExpression::REMAINDER || code == HotaScriptExpression::RANDOM_NUMBER)
	{
		const double left = expression(server, context, node["left"]);
		const double right = expression(server, context, node["right"]);
		switch(code)
		{
			case HotaScriptExpression::ADD: return left + right;
			case HotaScriptExpression::SUBSTRACT: return left - right;
			case HotaScriptExpression::MULTIPLY: return left * right;
			case HotaScriptExpression::DIVIDE: return std::floor(left / right);
			case HotaScriptExpression::REMAINDER: return left - std::floor(left / right) * right;
			default: return server.getRandomGenerator().nextInt(static_cast<int>(left), static_cast<int>(right));
		}
	}
	switch(code)
	{
		case HotaScriptExpression::INTEGER_VALUE: return node["value"].Float();
		case HotaScriptExpression::VARIABLE_VALUE:
			return state.getMap().getScriptVariables().get(ModScope::scopeMap(), variable(node)).Float();
		case HotaScriptExpression::NEGATE: return -expression(server, context, node["value"]);
		case HotaScriptExpression::RESOURCE:
			return state.getResource(resolvePlayer(context, node), GameResID(GameResID::decode(node["entity"].String())));
		case HotaScriptExpression::CURRENT_DIFFICULTY: return state.getStartInfo()->difficulty;
		case HotaScriptExpression::COMPARE_DIFFICULTY: return state.getStartInfo()->difficulty - node["value"].Integer();
		case HotaScriptExpression::CURRENT_DATE: return state.getCalendar().getCurrentDay();
		case HotaScriptExpression::HERO_EXPERIENCE: return static_cast<double>(hero(context).exp);
		case HotaScriptExpression::HERO_LEVEL: return hero(context).level;
		case HotaScriptExpression::HERO_PRIMARY_SKILL: return hero(context).getPrimSkillLevel(PrimarySkill(node["skill"].Integer()));
		case HotaScriptExpression::CREATURE_COUNT_IN_ARMY:
		{
			const auto & target = hero(context);
			const CreatureID id = CreatureID::decode(node["entity"].String());
			double count = 0;
			for(const auto & slot : target.Slots())
				if(const auto * creature = target.getCreature(slot.first); creature && creature->getId() == id) count += target.getStackCount(slot.first);
			return count;
		}
		case HotaScriptExpression::HERO_OWNED_ARTIFACTS:
		{
			const auto & target = hero(context);
			const ArtifactID id = ArtifactID::decode(node["entity"].String());
			double count = 0;
			for(const auto & slot : target.artifactsWorn) if(slot.second.getArt() && slot.second.getArt()->getTypeId() == id) ++count;
			for(const auto & slot : target.artifactsInBackpack) if(slot.getArt() && slot.getArt()->getTypeId() == id) ++count;
			return count;
		}
		default: throw std::runtime_error("Unknown native map expression opcode");
	}
}
}
