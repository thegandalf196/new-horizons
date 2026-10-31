/*
 * PhysicalAfflictionEffectPreparation.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../CGameHandler.h"

#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/PhysicalAffliction.h"
#include "../../lib/battle/NewHorizonsSerpentsEmbrace.h"
#include "../../lib/battle/GraveDustEffectChanges.h"
#include "../../lib/battle/GraveDustNativePreflight.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/networkPacks/SetStackEffect.h"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <vector>

void CGameHandler::stampPhysicalAfflictionEffects(SetStackEffect & effects)
{
	if(newHorizonsGraveDust::containsEffect(effects))
	{
		const auto * battle = gameState().getBattle(effects.battleID);
		if(!battle) throw std::invalid_argument("Grave Dust effect change requires a battle");
		// Match the replica's initial-batch checks before ordered replay or publication.
		effects.validateExchange();
		effects.validateConfusionMarkers();
		effects.validateGazeApplication(*battle->getBattle());
		for(const auto * changes : {&effects.toAdd, &effects.toUpdate})
			for(const auto & [target, bonuses] : *changes)
				for(const auto & bonus : bonuses)
					newHorizonsSerpentsEmbrace::validateApplication(*battle->getBattle(), target, bonus);
		if(const auto * native = dynamic_cast<const BattleInfo *>(battle))
			newHorizonsGraveDust::validateNativeEffectChanges(*native, effects);
		else
			newHorizonsGraveDust::validateEffectChanges(*battle->getBattle(), effects);
	}
	// Stamp accepted effect applications in the outgoing packet, so replicas
	// and detached forecasts receive the same ordering. Unmarked packets
	// keep their ordinary path; this is not a recurring battle-wide scan.
	std::set<uint32_t> markedUnits;
	const auto collectMarkedUnits = [&markedUnits](const auto & changes)
	{
		for(const auto & [unitId, bonuses] : changes)
			if(std::ranges::any_of(bonuses, [](const Bonus & bonus)
			{
				return bonus.type == BonusType::PHYSICAL_AFFLICTION;
			}))
				markedUnits.insert(unitId);
	};
	collectMarkedUnits(effects.toUpdate);
	collectMarkedUnits(effects.toAdd);
	if(!markedUnits.empty())
	{
		const auto * battle = gameState().getBattle(effects.battleID);
		if(!battle)
			throw std::runtime_error("Physical-affliction application requires a battle");
		for(const auto unitId : markedUnits)
		{
			const auto * unit = battle->battleGetUnitByID(unitId);
			if(!unit)
				throw std::runtime_error("Physical-affliction application requires a unit");
			std::vector<Bonus> removed;
			for(const auto & [removedId, bonuses] : effects.toRemove)
				if(removedId == unitId)
					removed.insert(removed.end(), bonuses.begin(), bonuses.end());
			std::vector<std::vector<Bonus> *> incoming;
			const auto collectIncoming = [&incoming, unitId](auto & changes)
			{
				for(auto & [changedId, bonuses] : changes)
					if(changedId == unitId)
						incoming.push_back(&bonuses);
			};
			collectIncoming(effects.toUpdate);
			collectIncoming(effects.toAdd);
			physicalAfflictions::stampEffectChanges(*unit, removed, incoming);
		}
	}
}
