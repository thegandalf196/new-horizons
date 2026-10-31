/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ProjectedInjuryLifecycle.h"
#include "HypotheticBattle.h"
#include "StackWithBonuses.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
void projectedInjuryLifecycle::before(HypotheticBattle & battle, std::span<const BattleStackAttacked> hits)
{
	for(const auto & hit : hits)
		if(const auto unit = battle.getForUpdate(hit.stackAttacked); unit && !unit->isTimeStopped())
			unit->removeUnitBonus(Bonus::UntilTakingIndirectDamage);
}
void projectedInjuryLifecycle::after(HypotheticBattle & battle, std::span<const BattleStackAttacked> hits)
{
	for(const auto & hit : hits)
	{
		if((hit.flags & BattleStackAttacked::GUARDIAN_SPIRIT_EXHAUSTED) == 0) continue;
		const auto unit = battle.getForUpdate(hit.stackAttacked);
		if(!unit) continue;
		const auto markers = unit->getBonuses(Selector::type()(BonusType::GUARDIAN_SPIRIT));
		std::vector<Bonus> remove;
		for(const auto & marker : *markers) if(marker) remove.push_back(*marker);
		if(remove.empty())
		{
			unit->guardianSpiritHitPoints = 0;
			unit->guardianSpiritRoundsRemaining = 0;
		}
		else if(!unit->isTimeStopped()) unit->removeUnitBonus(remove);
	}
}
