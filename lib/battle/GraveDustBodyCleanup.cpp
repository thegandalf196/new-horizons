/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "GraveDustBodyCleanup.h"
#include "NewHorizonsGraveDust.h"
#include "GraveDustEffectChanges.h"
#include "IBattleState.h"
#include "Unit.h"
#include "../bonuses/CBonusSystemNode.h"
#include "../bonuses/BonusList.h"
#include "../networkPacks/PacksForClientBattle.h"
#include "../networkPacks/SetStackEffect.h"
#include <set>
#include <stdexcept>
namespace newHorizonsGraveDust
{
namespace
{
std::vector<Bonus> bodyRecords(const battle::Unit & unit)
{
	std::vector<Bonus> result;
	if(const auto * native = dynamic_cast<const CBonusSystemNode *>(&unit))
	{
		// Canonical Dust is local and non-propagated. Native absence needs no bonus-graph evaluation.
		for(const auto & record : native->getExportedBonusList())
			if(record && isReservedSource(*record)) result.push_back(*record);
	}
	else
	{
		const auto actual = unit.getUnstackedBonuses(CSelector([](const Bonus * record)
		{
			return record && isReservedSource(*record);
		}));
		if(!actual) throw std::invalid_argument("Invalid Grave Dust provenance");
		for(const auto & record : *actual)
		{
			if(!record) throw std::invalid_argument("Invalid Grave Dust provenance");
			result.push_back(*record);
		}
	}
	if(!result.empty()) validateSourceGroup(result);
	return result;
}
SetStackEffect bodyRemoval(const IBattleInfo & battle, const std::vector<BattleStackAttacked> & hits)
{
	SetStackEffect removal;
	removal.battleID = battle.getBattleID();
	std::set<uint32_t> seen;
	for(const auto & hit : hits)
	{
		if((!hit.killed() && !hit.cloneKilled() && !hit.willRebirth()) || !seen.insert(hit.stackAttacked).second)
			continue;
		const auto units = battle.getUnitsIf([&hit](const battle::Unit * unit)
		{
			return unit && unit->unitId() == hit.stackAttacked;
		});
		if(units.empty()) continue; // Preserve the existing missing-unit transition behavior.
		if(units.size() != 1) throw std::invalid_argument("Ambiguous Grave Dust body replacement");
		auto records = bodyRecords(*units.front());
		if(!records.empty()) removal.toRemove.emplace_back(hit.stackAttacked, std::move(records));
	}
	if(!removal.toRemove.empty()) validateEffectChanges(battle, removal);
	return removal;
}
}
void validateBodyChanges(const IBattleInfo & battle, const std::vector<BattleStackAttacked> & hits)
{
	(void)bodyRemoval(battle, hits);
}
void cleanupBodyChanges(IBattleState & battle, const std::vector<BattleStackAttacked> & hits)
{
	const auto removal = bodyRemoval(battle, hits); // Validate every group before any local removal.
	for(const auto & [id, records] : removal.toRemove) battle.removeUnitBonus(id, records);
}
}
