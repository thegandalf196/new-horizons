/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsGraveDustStrike.h"
#include "GraveDustApplication.h"
#include "BattleAttackInfo.h"
#include "CBattleInfoCallback.h"
#include "NewHorizonsCombatSkills.h"
#include "Unit.h"
#include "../CCreatureHandler.h"
#include "../entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include <stdexcept>
namespace newHorizonsGraveDustStrike
{
namespace
{
int coefficient(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(!attack.attacker || !attack.defender || !attack.shooting || attack.secondaryAttack
		|| attack.bracePreemptive || attack.preemptiveDamagePercent > 0 || attack.cleaveDamagePercent > 0
		|| !attack.attacker->alive() || !attack.defender->alive()
		|| attack.attacker->isGhost() || attack.defender->isGhost()
		|| attack.attacker->isTimeStopped() || attack.defender->isTimeStopped()
		|| !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(attack.attacker)
		|| battle.battleGetOwner(attack.attacker) == battle.battleGetOwner(attack.defender)
		|| !battle.battleMatchActionController(attack.attacker, attack.defender)) return 0;
	const int value = newHorizonsHeroes::ordinaryCreatureSpecialtyValue(battle, *attack.attacker, "core:moandor");
	if(value <= 0) return 0; // Absent/explicit-zero/unknown policy: no family/body query.
	const auto family = CreatureID(CreatureID::decode("core:lich"));
	if(!family.hasValue() || !family.toCreature()) throw std::runtime_error("Grave Dust Lich family is not registered");
	return attack.attacker->unitType() && (attack.attacker->creatureId() == family
		|| family.toCreature()->isMyDirectOrIndirectUpgrade(attack.attacker->unitType())) ? value : 0;
}
}
bool eligible(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	return coefficient(battle, attack) > 0;
}
newHorizonsOrdinaryStrike::EffectPlan plan(const CBattleInfoCallback & battle,
	const BattleAttackInfo & attack, const int64_t actualBodyHealthLoss, const bool bodyReplaced)
{
	if(actualBodyHealthLoss <= 0 || bodyReplaced) return {};
	const int value = coefficient(battle, attack);
	if(value <= 0) return {};
	auto result = newHorizonsGraveDust::prepareApplication(*attack.defender, value, battle.battleGetOwner(attack.attacker));
	for(auto & record : result.add)
		record.description.appendRawString("Grave Dust: received eligible healing is reduced by " + std::to_string(value)
			+ " basis points until the next accepted Creature Action completes, including Wait");
	return result;
}
}
