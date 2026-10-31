/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsToxicSting.h"
#include "BattleAttackInfo.h"
#include "CBattleInfoCallback.h"
#include "CUnitState.h"
#include "NewHorizonsBulwark.h"
#include "NewHorizonsCombatSkills.h"
#include "../CCreatureHandler.h"
#include "../entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include <limits>
#include <stdexcept>
namespace newHorizonsToxicSting
{
bool livingTarget(const battle::Unit & target)
{
	return target.isValidTarget(false) && target.alive() && !target.isGhost() && !target.isTimeStopped()
		&& !target.hasBonusOfType(BonusType::UNDEAD) && !target.hasBonusOfType(BonusType::NON_LIVING)
		&& !target.hasBonusOfType(BonusType::MECHANICAL) && !target.hasBonusOfType(BonusType::SIEGE_WEAPON);
}
int chancePercent(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(!attack.attacker || !attack.defender
		|| !attack.attacker->alive() || attack.attacker->isGhost() || attack.attacker->isTimeStopped()
		|| !livingTarget(*attack.defender) || !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(attack.attacker)
		|| attack.shooting || !attack.physicalDamage || attack.secondaryAttack || attack.bracePreemptive
		|| attack.preemptiveDamagePercent > 0 || attack.cleaveDamagePercent > 0
		|| battle.battleGetOwner(attack.attacker) == battle.battleGetOwner(attack.defender))
		return 0;
	const auto family = CreatureID(CreatureID::decode("core:manticore"));
	if(!attack.attacker->unitType() || (attack.attacker->creatureId() != family
		&& !family.toCreature()->isMyDirectOrIndirectUpgrade(attack.attacker->unitType())))
		return 0;
	const int chance = newHorizonsHeroes::ordinaryCreatureSpecialtyValue(battle, *attack.attacker, "core:synca");
	if(chance <= 0)
		return 0;
	return chance;
}
std::optional<Plan> prepare(const CBattleInfoCallback & battle, const BattleAttackInfo & attack,
	int64_t primaryBodyHealthLoss)
{
	if(primaryBodyHealthLoss <= 0)
		return std::nullopt;
	const int chance = chancePercent(battle, attack);
	if(chance <= 0)
		return std::nullopt;
	if(attack.attacker->unitId() > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()))
		throw std::runtime_error("Toxic Sting source exceeds physical Poison provenance range");
	return Plan{attack.attacker->unitId(), attack.defender->unitId(), chance,
		newHorizonsBulwark::toxicSpinesPoisonBase(primaryBodyHealthLoss)};
}
bool apply(const Plan & plan, battle::CUnitState & target)
{
	if(plan.target != target.unitId() || !livingTarget(target))
		return false;
	return newHorizonsBulwark::applyPhysicalPoison(&target, plan.baseDamage, static_cast<int32_t>(plan.source));
}
}
