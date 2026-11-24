/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/battle/NewHorizonsWarcast.h"
#include "../../lib/json/JsonNode.h"
#include <map>
#include <string>

namespace nativeBenchmark
{
/// Read-only Warcasting evidence for one encounter: the hero's spells and Orders,
/// Orders issued while one of the hero's beneficial spells is on a friendly stack,
/// and Orders that warcast at least one stack. It never changes the battle.
class WarcastEncounterObserver
{
	BattleSide side;
	std::map<std::string, int64_t> counts;
	bool order = false;
	HeroCommand command = HeroCommand::NONE;

	bool ownBuffOnBoard(const BattleInfo & battle) const
	{
		const auto owner = battle.sideToPlayer(side);
		for(const auto * unit : battle.battleGetAllUnits(false))
			if(unit && unit->alive() && unit->unitSide() == side && newHorizonsWarcast::ownBeneficialSpellCount(*unit, owner, 1) > 0)
				return true;
		return false;
	}

public:
	explicit WarcastEncounterObserver(BattleSide side) : side(side)
	{
		for(const auto * key : {"heroSpells", "orders", "ordersWithOwnBuffOnBoard", "warcastOrders", "warcastRecipients"})
			counts[key] = 0;
	}

	void before(const BattleInfo & battle, const BattleAction & action)
	{
		order = false;
		if(action.side != side)
			return;
		if(action.actionType == EActionType::HERO_SPELL)
			++counts["heroSpells"];
		if(action.actionType == EActionType::HERO_COMMAND)
		{
			order = true;
			command = action.command;
			++counts["orders"];
			if(ownBuffOnBoard(battle))
				++counts["ordersWithOwnBuffOnBoard"];
		}
	}

	void after(const BattleInfo & battle, const BattleAction &)
	{
		if(!order)
			return;
		const auto state = battle.battleGetHeroOrderState(side, command);
		if(state && state->issuedRound == battle.battleGetRound() && !state->warcast.recipients.empty())
		{
			++counts["warcastOrders"];
			counts["warcastRecipients"] += static_cast<int64_t>(state->warcast.recipients.size());
		}
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
