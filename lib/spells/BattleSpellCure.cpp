/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleSpellMechanics.h"
#include "CurePhysicalAffliction.h"
#include "NewHorizonsMagic.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/Unit.h"
namespace spells
{
bool BattleSpellMechanics::canCastCure(Problem & problem, const CGHeroInstance * castingHero) const
{
	if(mode != Mode::HERO || !castingHero)
		return adaptGenericProblem(problem);

	const auto & rules = battle()->getBattle()->getMagicRules();
	for(const auto * unit : battle()->battleGetAllUnits(false))
	{
		if(!curePhysical::livingTarget(unit)
			|| !ownerMatches(unit, true) || !isReceptive(unit))
			continue;

		if(curePhysical::present(rules, *unit, "frozen")
			|| curePhysical::present(rules, *unit, "graveDust")
			|| !newHorizonsMagic::cureAfflictions(rules, unit).empty()
			|| unit->getSurvivingMissingHealth() > 0)
			return true;
	}
	return adaptProblem(ESpellCastProblem::NO_APPROPRIATE_TARGET, problem);
}
}
