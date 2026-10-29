/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "AttackHealthAftermath.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/Bonus.h"
namespace
{
int32_t vampirismHealBasisPoints(const battle::Unit * unit)
{
	if(!unit)
		return 0;

	const auto triggers = unit->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER);
	int32_t result = 0;
	for(const auto & bonus : *triggers)
	{
		if(!bonus || bonus->source != BonusSource::SPELL_EFFECT
			|| bonus->sid.toString() != newHorizonsMagic::SHADOW_VAMPIRISM_SPELL
			|| bonus->subtype.toString() != newHorizonsMagic::SHADOW_VAMPIRISM_STATUS
			|| !Bonus::NTurns(bonus.get()) || bonus->turnsRemain <= 0)
			continue;

		result = std::max(result, std::clamp(bonus->val, 0,
			newHorizonsMagic::VAMPIRISM_MAX_HEAL_BASIS_POINTS));
	}
	return result;
}

}
void projectVampirismHealing(battle::CUnitState * attacker, int64_t actualDamage,
	std::vector<std::pair<uint32_t, int64_t>> & healingByUnit)
{
	if(!attacker || !attacker->alive() || actualDamage <= 0)
		return;

	const auto basisPoints = vampirismHealBasisPoints(attacker);
	if(basisPoints <= 0)
		return;

	// Split before multiplying so a large multi-target attack cannot overflow.
	const auto healingRequest = (actualDamage / 10'000) * basisPoints
		+ ((actualDamage % 10'000) * basisPoints) / 10'000;
	if(healingRequest <= 0)
		return;

	auto remainingHealing = healingRequest;
	const auto healed = attacker->heal(remainingHealing, EHealLevel::HEAL,
		EHealPower::PERMANENT).healedHealthPoints;
	if(healed > 0)
		healingByUnit.emplace_back(attacker->unitId(), healed);
}
