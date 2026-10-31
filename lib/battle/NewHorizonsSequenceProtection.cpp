/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsSequenceProtection.h"
#include "BattleAttackInfo.h"
#include "CBattleInfoCallback.h"
#include "Unit.h"
#include "../CCreatureHandler.h"
#include "../entities/hero/CHero.h"
#include "../entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../mapObjects/CGHeroInstance.h"
namespace newHorizonsSequenceProtection
{
namespace
{
bool family(const battle::Unit & unit, const char * key)
{
	const auto base = CreatureID(CreatureID::decode(key));
	const auto belongs = [&](CreatureID type)
	{
		return type == base || base.toCreature()->isMyDirectOrIndirectUpgrade(type.toCreature());
	};
	// Ordinary specialty families follow the effective battle form, like the
	// shared melee and ranged helpers; reversion is observed on the next query.
	return belongs(unit.creatureId());
}
bool ordinaryEnemyBlow(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	return attack.attacker && attack.defender && attack.physicalDamage
		&& !attack.retaliation && !attack.bracePreemptive && attack.preemptiveDamagePercent == 0
		&& attack.cleaveDamagePercent == 0 && attack.attacker->alive() && attack.defender->alive()
		&& !attack.attacker->isGhost() && !attack.defender->isGhost()
		&& !attack.attacker->isTurret() && !attack.attacker->isCatapult()
		&& battle.battleGetOwner(attack.attacker) != battle.battleGetOwner(attack.defender);
}
int coefficient(const CBattleInfoCallback & battle, const battle::Unit & unit,
	const char * identity, const char * key)
{
	const auto * hero = newHorizonsHeroes::ordinaryCreatureVisibleOwnerHero(battle, unit);
	if(!hero || !hero->getHeroType() || hero->getHeroType()->getJsonKey() != identity
		|| !newHorizonsHeroes::hasOrdinaryCreatureSpecialty(hero))
		return 0;
	return static_cast<int>(hero->getPrimaryGrowthRules()["additionalSpecialties"]["ordinaryCreatureSpecialties"][key].Integer());
}
}
int cyclopeanBreachPercent(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(!attack.shooting || attack.secondaryAttack || !ordinaryEnemyBlow(battle, attack) || !family(*attack.attacker, "core:cyclop"))
		return 0;
	return coefficient(battle, *attack.attacker, "core:yog", "cyclopeanBreachIgnorePercent");
}
int runicEndurancePercent(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(attack.ordinaryAttackIndex <= 0 || !ordinaryEnemyBlow(battle, attack) || !family(*attack.defender, "core:dwarf"))
		return 0;
	return coefficient(battle, *attack.defender, "core:ufretin", "runicEnduranceReductionPercent");
}
}
