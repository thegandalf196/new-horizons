/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsMagic.h"
#include "CSpell.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/Unit.h"
#include "../entities/hero/NewHorizonsInfernoSpecialties.h"
#include "../entities/hero/NewHorizonsPerkRules.h"
#include "../mapObjects/CGHeroInstance.h"
namespace newHorizonsMagic
{
bool isProtectedAreaCenter(const CBattleInfoCallback & callback, const CGHeroInstance * hero,
	SpellID spell, const battle::Unit & unit, const BattleHex & center, BattleSide casterSide,
	bool preciseCastingOnly)
{
	const auto * state = callback.getBattle();
	if(!state || !rulesActive(state->getMagicRules()) || !hero || !center.isAvailable()
		|| (casterSide != BattleSide::ATTACKER && casterSide != BattleSide::DEFENDER))
		return false;
	if(spell == SpellID::FIREBALL && newHorizonsHeroes::bloodfireAttack(hero) > 0) return false;
	const auto * definition = spell.toSpell();
	if(!definition)
		return false;
	const auto & key = definition->getJsonKey();
	const bool blastSpell = key == "core:fireball" || key == "core:inferno" || key == "core:meteorShower";
	const bool preciseSpell = blastSpell || key == "core:frostRing" || key == "new-horizons:purify";
	const bool controlledBlast = !preciseCastingOnly && blastSpell && hero->hasActivePerk(std::string(HAVOC_MAGIC_SKILL),
		"new-horizons:havocMagic.controlledBlast");
	const bool preciseCasting = preciseSpell && !newHorizonsHeroes::usesResistancePreciseCasting(hero->getPerkState().getRules())
		&& hero->hasActivePerk(std::string(SPELLCRAFT_SKILL), std::string(SPELLCRAFT_PRECISE_CASTING));
	if(!controlledBlast && !preciseCasting)
		return false;
	const auto * centerUnit = callback.battleGetUnitByPos(center, true);
	return centerUnit && centerUnit->unitId() == unit.unitId()
		&& callback.playerToSide(callback.battleGetOwner(centerUnit)) == casterSide;
}

}
