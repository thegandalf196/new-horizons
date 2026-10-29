/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "StackSpellStatusPresentation.h"
#include "../../lib/CStack.h"
namespace newHorizonsStackSpellPresentation
{
newHorizonsBattleStatus::BattleFormStatus battleFormStatus(const CStack & stack)
{
	if(!stack.hasBattleForm())
		return {};
	const auto currentCreature = stack.battleFormCreature();
	const auto originalCreature = stack.battleFormOriginalCreature();
	return newHorizonsBattleStatus::makeBattleFormStatus(true,
		currentCreature.getNum(), originalCreature.getNum(), stack.getBattleFormRoundsRemaining(),
		stack.health.getCreatureHealthAvailable(),
		currentCreature.toCreature()->getNamePluralTranslated(),
		originalCreature.toCreature()->getNamePluralTranslated(), stack.isBattleFormRestorationPending());
}
}
