/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "AcceptedHeroSpellHistory.h"
#include "../battle/BattleInfo.h"
#include "../entities/hero/NewHorizonsInfernoSpecialties.h"
#include "../networkPacks/PacksForClientBattle.h"
#include <limits>
void recordAcceptedHeroSpellHistory(BattleInfo & battle, const BattleSpellCast & pack)
{
	auto & casterSide = battle.getSide(pack.side);
	const auto addManaSpent = [](SideInBattle & side, int32_t amount)
	{
		if(amount <= 0)
			return;
		if(side.acceptedHeroManaSpent > std::numeric_limits<int64_t>::max() - amount)
			side.acceptedHeroManaSpent = std::numeric_limits<int64_t>::max();
		else
			side.acceptedHeroManaSpent += amount;
	};
	addManaSpent(casterSide, pack.paidHeroManaCost);
	if(pack.crossSchoolFormula)
		battle.setCrossSchoolFormulaState(pack.side, pack.crossSchoolFormula->after);
	// This marker is set only after every accepted-cast validation above has
	// succeeded; creature casts and rejected hero requests never consume it.
	if(battle.getRound() >= 0)
		casterSide.wisdomCombatState.recordCompletedCast(pack.spellID, battle.getRound());
	if(pack.activeCast && newHorizonsHeroes::usesKindlingHistory(battle.getSideHero(pack.side))
		&& !pack.automaticHeroCast.automatic())
		casterSide.usedSpellsHistory.push_back(pack.spellID);
	casterSide.heroSpellCastCompleted = true;
	casterSide.recordCompletedHeroSpellLevel(battle.battleGetSpellLevel(pack.spellID));
}
