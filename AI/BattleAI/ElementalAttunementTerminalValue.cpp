/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ElementalAttunementTerminalValue.h"
#include "CreatureActivationValue.h"
#include "AttackPossibility.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/UnitCreatureResolver.h"

float elementalAttunementAI::terminalMarginal(const std::shared_ptr<HypotheticBattle> & accepted,
	BattleSide side, uint8_t previousMask, const DamageCache & originalDamage,
	float positiveMultiplier, float negativeMultiplier)
{
	if(!accepted || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
		return 0.0f;
	const auto afterMask = accepted->getElementalAttunementMask(side);
	if((afterMask & ~previousMask) == 0 || accepted->getElementalAttunementProfile(side).rank == 0)
		return 0.0f;
	std::vector<uint32_t> newlyAttuned;
	const auto & profile = accepted->getElementalAttunementProfile(side);
	for(const auto * unit : accepted->battleGetAllUnits(false))
	{
		if(!unit || !unit->alive() || unit->isGhost()
			|| unit->unitSide() != side)
			continue;
		const auto * resolver = unit->unitCreatureResolver();
		const auto before = resolver
			? newHorizonsElementalAttunement::passivesForCreatureKey(profile, previousMask,
				resolver->encode(unit->creatureId().getNum()))
			: newHorizonsElementalAttunement::passivesForCreature(profile, previousMask, unit->creatureId());
		if(!before.attuned && accepted->unitElementalAttunement(unit).attuned)
			newlyAttuned.push_back(unit->unitId());
	}
	if(newlyAttuned.empty())
		return 0.0f;
	auto without = std::make_shared<HypotheticBattle>(accepted->env, accepted);
	without->setElementalAttunementMask(side, previousMask);
	DamageCache withDamage(&originalDamage), withoutDamage(&originalDamage);
	float marginal = 0.0f;
	for(const auto id : newlyAttuned)
	{
		// Same post-cast units/targets, original DPS baseline and nominal one-turn
		// kernel on both sides. This values a passive, not another granted action.
		const auto withValue = creatureActivationValue::expectedTargetActivationValue(
			accepted->battleGetUnitByID(id), withDamage, accepted, true);
		const auto withoutValue = creatureActivationValue::expectedTargetActivationValue(
			without->battleGetUnitByID(id), withoutDamage, without, true);
		const auto controller = accepted->playerToSide(accepted->battleGetOwner(accepted->battleGetUnitByID(id)));
		marginal += (controller == side ? positiveMultiplier : -negativeMultiplier) * (withValue - withoutValue);
	}
	return marginal;
}
