/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ProjectedBattleWorldArenaState.h"
#include "StackEffectRemoval.h"
#include "../bonuses/BonusSelector.h"
namespace battle
{
void ProjectedBattleWorldArena::Data::recordSolidarityDeaths(ProjectedBattleWorldArena & owner,
	std::span<const newHorizonsReactiveDefense::Destruction> deaths)
{
	std::vector<newHorizonsReactiveDefense::Destruction> destroyed;
	for(const auto & death : deaths)
		if(const auto * slot = find(death.unit, false); slot && !slot->state->alive())
			destroyed.push_back(death);
	if(destroyed.empty())
		return;
	BattleSideArray<int> coefficients{};
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(std::ranges::any_of(destroyed, [side](const auto & death) { return death.side == side; }))
			// Existing arena contract requires the real native exclusive interval or
			// an explicitly authorized ALL_KNOWING captured owner. This pointer is
			// retained original-side metadata, never a player-policy escape hatch.
			coefficients[side] = newHorizonsReactiveDefense::solidarityCapturedPercent(sideHeroes[side]);
	if(coefficients[BattleSide::ATTACKER] <= 0 && coefficients[BattleSide::DEFENDER] <= 0)
		return;
	for(size_t occurrence = 0; occurrence < slots.size(); ++occurrence)
	{
		const auto & unit = *slots[occurrence]->state;
		const auto side = unit.unitSide();
		if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
			continue;
		if(!std::ranges::any_of(destroyed, [&](const auto & death)
			{ return death.side == side && death.unit != unit.unitId(); }))
			continue;
		const auto plan = newHorizonsReactiveDefense::solidarityProtection(unit, coefficients[side]);
		if(plan.add.empty())
			continue;
		// Only the stable occurrence and value-owned plan cross these graph edits.
		for(const auto & previous : plan.remove)
			owner.expireExportedBonusesAt(occurrence, CSelector([previous](const Bonus * candidate)
			{
				return candidate && matchesStackEffectRemoval(previous, *candidate);
			}));
		for(const auto & record : plan.add)
			owner.addExportedBonusAt(occurrence, std::make_shared<Bonus>(record));
	}
}
}
