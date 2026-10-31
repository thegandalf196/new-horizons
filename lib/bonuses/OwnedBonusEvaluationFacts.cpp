/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedBonusEvaluationFacts.h"
#include <stdexcept>
void OwnedBonusEvaluationFacts::validate() const
{
	const bool army = nodeType == BonusNodeType::HERO || nodeType == BonusNodeType::ARMY
		|| nodeType == BonusNodeType::TOWN;
	const bool stack = nodeType == BonusNodeType::STACK_BATTLE || nodeType == BonusNodeType::STACK_INSTANCE
		|| nodeType == BonusNodeType::COMMANDER;
	if((nodeType == BonusNodeType::HERO && !heroLevel) || (army && !armySlots)
		|| (stack && (!stackCount || !stableStackLevel)))
		throw std::invalid_argument("Missing applicable owned bonus evaluation facts");
	if(acceptance && acceptance->state.hasForm && !acceptance->state.currentForm.hasValue())
		throw std::invalid_argument("Invalid copied battle form acceptance facts");
}
