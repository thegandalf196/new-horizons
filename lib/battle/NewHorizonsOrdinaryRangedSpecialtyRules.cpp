/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsOrdinaryRangedSpecialtyRules.h"
#include "BattleAttackInfo.h"
#include "NewHorizonsArchery.h"
#include "Unit.h"
#include "../CCreatureHandler.h"
#include <stdexcept>
namespace newHorizonsOrdinaryRangedSpecialtyRules
{
bool orcCasualtyShot(const BattleAttackInfo & attack)
{
	if(!attack.shooting || !attack.physicalDamage || !attack.attacker || !attack.defender)
		return false;
	const auto family = CreatureID(CreatureID::decode("core:orc"));
	const auto creature = attack.attacker->creatureId();
	if(creature != family && !family.toCreature()->isMyDirectOrIndirectUpgrade(creature.toCreature()))
		return false;
	if(!newHorizonsArchery::isOrdinaryPhysicalShooter(attack.attacker)
		|| !attack.attacker->alive() || !attack.defender->alive()
		|| attack.attacker->isGhost() || attack.defender->isGhost())
		return false;
	const auto entry = attack.defender->unitBaseAmount();
	const auto current = attack.defender->getCurrentOriginalCreatureCount();
	return entry > 0 && current > 0 && current < entry;
}
int orcCasualtyDamagePercent(const BattleAttackInfo & attack, int capturedPercent)
{
	if(capturedPercent < 0 || capturedPercent > 100)
		throw std::invalid_argument("Invalid captured ranged specialty percentage");
	return capturedPercent > 0 && orcCasualtyShot(attack) ? capturedPercent : 0;
}
}
