/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "BattleSpellMechanics.h"
#include "CSpell.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/NewHorizonsForestAmbush.h"
namespace spells
{
bool targetsSanctifiedStackDirectly(const Mechanics & mechanics, const Target & target)
{
	if(target.size() != 1 || mechanics.isMassive())
		return false;
	const auto * spell = mechanics.getSpell();
	if(!spell || !(spell->isOffensive() || spell->isNegative() || spell->isDamage()))
		return false;

	const auto targetTypes = mechanics.getTargetTypes();
	if(targetTypes != std::vector<AimType>{AimType::CREATURE})
		return false;
	if(mechanics.getSpellId() == SpellID::CHAIN_LIGHTNING || mechanics.isNewHorizonsStormOfDaggers())
		return false;

	const auto affectedStacks = mechanics.getAffectedStacks(target);
	if(affectedStacks.size() != 1)
		return false;

	const auto * battle = mechanics.battle();
	if(!battle)
		return false;

	const auto & destination = target.front();
	const auto * unit = destination.unitValue;
	if(!unit && destination.hexValue.isValid())
		unit = battle->battleGetUnitByPos(destination.hexValue, true);

	return unit && newHorizonsForestAmbush::protectsDirectTarget(unit) && mechanics.ownerMatches(unit, false);
}
}
