/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "EventRuntime.h"
#include "EventCodes.h"
#include "../../gameState/CGameState.h"
#include "../../mapping/CMap.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../mapObjects/CGTownInstance.h"
#include "../../CPlayerState.h"
#include "../../StartInfo.h"

namespace scripting::nativeMap
{
bool Runtime::condition(IGameEventCallback & server, const Continuation & context, const JsonNode & node) const
{
	const auto code = static_cast<HotaScriptCondition>(node["op"].Integer());
	if(code == HotaScriptCondition::LESSER || code == HotaScriptCondition::GREATER || code == HotaScriptCondition::EQUAL
		|| code == HotaScriptCondition::GREATER_OR_EQUAL || code == HotaScriptCondition::LESSER_OR_EQUAL || code == HotaScriptCondition::NOT_EQUAL)
	{
		const double left = expression(server, context, node["left"]);
		const double right = expression(server, context, node["right"]);
		switch(code)
		{
			case HotaScriptCondition::LESSER: return left < right;
			case HotaScriptCondition::GREATER: return left > right;
			case HotaScriptCondition::EQUAL: return left == right;
			case HotaScriptCondition::GREATER_OR_EQUAL: return left >= right;
			case HotaScriptCondition::LESSER_OR_EQUAL: return left <= right;
			default: return left != right;
		}
	}
	switch(code)
	{
		case HotaScriptCondition::CONSTANT: return node["value"].Bool();
		case HotaScriptCondition::ALL_OF:
			for(const auto & arg : node["args"].Vector()) if(!condition(server, context, arg)) return false;
			return true;
		case HotaScriptCondition::ANY_OF:
			for(const auto & arg : node["args"].Vector()) if(condition(server, context, arg)) return true;
			return false;
		case HotaScriptCondition::NOT: return !condition(server, context, node["condition"]);
		case HotaScriptCondition::HAS_ARTIFACT: return hero(context).hasArt(ArtifactID::decode(node["entity"].String()));
		case HotaScriptCondition::CURRENT_PLAYER: return player(context).getNum() == node["player"].Integer();
		case HotaScriptCondition::HERO_OWNER:
		{
			const HeroTypeID id(HeroTypeID::decode(node["entity"].String()));
			for(const auto & object : state.getMap().objects)
				if(const auto * target = dynamic_cast<const CGHeroInstance *>(object.get()); target && target->getHeroTypeID() == id)
					return target->getOwner() == resolvePlayer(context, node);
			return false;
		}
		case HotaScriptCondition::HERO_SECONDARY_SKILL:
			return hero(context).getSecSkillLevel(SecondarySkill(SecondarySkill::decode(node["entity"].String()))) >= node["mastery"].Integer();
		case HotaScriptCondition::PLAYER_DEFEATED:
			return state.getPlayerStatus(resolvePlayer(context, node), false) == EPlayerStatus::LOSER;
		case HotaScriptCondition::PLAYER_IS_HUMAN:
		{
			const auto * target = state.getPlayerState(resolvePlayer(context, node), false);
			return target && target->isHuman();
		}
		case HotaScriptCondition::PLAYER_STARTING_FACTION:
		{
			const auto & players = state.getStartInfo()->playerInfos;
			const auto target = players.find(resolvePlayer(context, node));
			return target != players.end() && target->second.castle.hasValue() && target->second.castle == FactionID(FactionID::decode(node["entity"].String()));
		}
		case HotaScriptCondition::TOWN_IS_NEUTRAL: return town(context).getOwner() == PlayerColor::NEUTRAL;
		case HotaScriptCondition::PLAYER_OWNS_TOWN:
		{
			const auto * target = referencedObject(node);
			return target && target->getOwner() == resolvePlayer(context, node);
		}
		case HotaScriptCondition::PLAYER_DEFEATED_HERO: case HotaScriptCondition::PLAYER_DEFEATED_MONSTER:
		{
			const auto * target = referencedObject(node);
			const auto * owner = state.getPlayerState(resolvePlayer(context, node), false);
			return target && owner && owner->destroyedObjects.count(target->id) != 0;
		}
		default: throw std::runtime_error("Unknown native map condition opcode");
	}
}
}
