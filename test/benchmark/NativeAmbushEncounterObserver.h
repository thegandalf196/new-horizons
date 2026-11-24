/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/NewHorizonsSylvanAmbush.h"
#include "../../lib/json/JsonNode.h"
#include <map>
#include <string>

namespace nativeBenchmark
{
/// Read-only Sylvan Tactics 5 evidence for one encounter: the ambusher side's
/// action shares, Concealments and Ambush strikes, and every opposing action
/// aimed at a Concealed stack. It never changes the battle.
class AmbushEncounterObserver
{
	BattleSide ambusher;
	std::map<std::string, int64_t> counts;
	uint32_t waiter = 0;
	bool waiting = false;

	static const battle::Unit * aimedUnit(const BattleInfo & battle, const BattleAction & action)
	{
		const auto target = action.getTarget(&battle);
		const size_t index = action.actionType == EActionType::WALK_AND_ATTACK ? 1 : 0;
		if(target.size() <= index)
			return nullptr;
		if(target[index].unitValue)
			return target[index].unitValue;
		return target[index].hexValue.isValid() ? battle.battleGetUnitByPos(target[index].hexValue, true) : nullptr;
	}
	void add(const std::string & key) { ++counts[key]; }

public:
	explicit AmbushEncounterObserver(BattleSide side) : ambusher(side)
	{
		for(const auto * key : {"waits", "defends", "attacks", "moves", "otherActions", "concealments",
			"ambushStrikes", "startleRetaliations", "meleeOnConcealed", "rangedOrSingleSpellOnConcealed",
			"spellsOnConcealedHex", "opponentSpells"})
			counts[key] = 0;
	}

	void before(const BattleInfo & battle, const BattleAction & action)
	{
		const auto * actor = battle.battleGetUnitByID(action.stackNumber);
		const bool own = actor && action.actionType != EActionType::HERO_SPELL ? actor->unitSide() == ambusher : action.side == ambusher;
		const auto * aimed = aimedUnit(battle, action);
		const bool hidden = aimed && newHorizonsSylvanAmbush::concealed(aimed);
		waiting = false;
		if(own && actor && action.actionType != EActionType::HERO_SPELL)
		{
			switch(action.actionType)
			{
				case EActionType::WAIT: add("waits"); waiting = true; waiter = actor->unitId(); break;
				case EActionType::DEFEND: add("defends"); break;
				case EActionType::WALK_AND_ATTACK: case EActionType::SHOOT: add("attacks"); break;
				case EActionType::WALK: add("moves"); break;
				default: add("otherActions"); break;
			}
			if(aimed && (action.actionType == EActionType::WALK_AND_ATTACK || action.actionType == EActionType::SHOOT))
			{
				BattleAttackInfo strike(actor, aimed, 0, action.actionType == EActionType::SHOOT);
				if(newHorizonsSylvanAmbush::ambushStrike(battle, strike))
				{
					add("ambushStrikes");
					if(!strike.shooting && newHorizonsSylvanAmbush::hasPerk(battle.battleGetFightingHero(ambusher), "startle"))
						add("startleRetaliations");
				}
			}
		}
		if(!own && action.actionType == EActionType::HERO_SPELL)
			add("opponentSpells");
		if(own || !hidden)
			return;
		if(action.actionType == EActionType::WALK_AND_ATTACK)
			add("meleeOnConcealed");
		else if(action.actionType == EActionType::SHOOT || action.actionType == EActionType::MONSTER_SPELL)
			add("rangedOrSingleSpellOnConcealed");
		else if(action.actionType == EActionType::HERO_SPELL)
			add("spellsOnConcealedHex"); // Accepted only for area spells: the authority rejects the rest.
	}

	void after(const BattleInfo & battle, const BattleAction & action)
	{
		if(!waiting || action.actionType != EActionType::WAIT)
			return;
		if(const auto * unit = battle.battleGetUnitByID(waiter); unit && newHorizonsSylvanAmbush::concealed(unit))
			add("concealments");
	}

	JsonNode summary() const
	{
		JsonNode result;
		for(const auto & [key, value] : counts)
			result[key].Integer() = value;
		return result;
	}
};
}
