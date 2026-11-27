/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OrdinarySpecialtyDamage.h"
#include "NewHorizonsPassiveSpecialties.h"
#include "NewHorizonsSpecialtyEvents.h"
#include "NewHorizonsAttackControllers.h"
#include "NewHorizonsPrismaticVanguard.h"
#include "BattleAttackInfo.h"
#include "Unit.h"
#include "../entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"

namespace newHorizonsPassiveSpecialties
{
namespace
{
template<typename Value>
Value composeSpecialtyDamage(const CBattleInfoCallback & battle, const BattleAttackInfo & attack, Value percent)
{
	percent = percent * newHorizonsSpecialtyEvents::firstStrikeFinalMultiplier(battle, attack) / 100;
	if(attack.physicalDamage && attack.shooting && !attack.secondaryAttack)
		percent = percent * newHorizonsAttackControllers::ballistaPhysicalDamagePercent(battle, *attack.attacker) / 100;
	percent = percent * (100 + newHorizonsPrismaticVanguard::activeDamagePercent(attack)) / 100;
	if(attack.retaliation && attack.aerialCounterDamagePercent > 0)
		percent = percent * (100 + attack.aerialCounterDamagePercent) / 100;
	return percent;
}
}

double ordinarySpecialtyDamagePercent(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(attack.attacker && attack.attacker->mayHaveBonusType(BonusType::CASUALTY_DAMAGE_PERCENTAGE)
		&& attack.attacker->hasBonusOfType(BonusType::CASUALTY_DAMAGE_PERCENTAGE))
	{
		const int permille = casualtyDamagePermille(attack);
		if(permille % 10 != 0)
			return composeSpecialtyDamage(battle, attack, 100.0 + static_cast<double>(permille) / 10);
		return composeSpecialtyDamage(battle, attack, 100 + permille / 10);
	}
	// Preserve every historical integer truncation, including intermediate
	// specialty multipliers, when the captured attack has no fractional bonus.
	return composeSpecialtyDamage(battle, attack, newHorizonsHeroes::ordinaryCreatureDamageMultiplier(battle, attack));
}
}
