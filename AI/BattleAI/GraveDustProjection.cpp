/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "GraveDustProjection.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/battle/NewHorizonsGraveDust.h"
#include "../../lib/battle/GraveDustEffectChanges.h"
#include "../../lib/bonuses/BonusList.h"
void graveDustAI::validateSelectorRemoval(const IBattleInfo & battle, const battle::Unit & unit, const CSelector & requested)
{
	if(unit.isTimeStopped()) return;
	std::vector<Bonus> selected;
	const auto graveDustEffects = unit.getUnstackedBonuses(CSelector(newHorizonsGraveDust::isEffect));
	for(const auto & bonus : *graveDustEffects)
		if(requested(bonus.get())) selected.push_back(*bonus);
	newHorizonsGraveDust::validateMutation(battle, unit, selected, newHorizonsGraveDust::EffectOperation::REMOVE);
}
CSelector graveDustAI::protectedRemovalSelector(const battle::Unit & unit, const CSelector & requested)
{
	if(!unit.isTimeStopped()) return requested;
	return [requested](const Bonus * bonus)
	{
		return requested(bonus) && !newHorizonsGraveDust::isEffect(bonus);
	};
}
