/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "PhysicalStrikeProjection.h"
#include "HypotheticBattle.h"
#include "StackWithBonuses.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"

namespace
{
std::optional<newHorizonsReactiveDefense::Destruction> configuredCasualty(
	const HypotheticBattle & battle, const battle::Unit & unit)
{
	if(newHorizonsReactiveDefense::solidarityConfiguredPercent(battle, unit.unitSide()) <= 0)
		return std::nullopt;
	return newHorizonsReactiveDefense::captureDestruction(unit);
}
}
std::optional<newHorizonsReactiveDefense::Destruction> PhysicalStrikeProjection::captureForecastDamage(
	const HypotheticBattle & battle, const battle::Unit & unit)
{
	if(!unit.alive())
		return std::nullopt;
	const auto candidate = configuredCasualty(battle, unit);
	// These direct forecasts do not resolve native Rebirth's roll. Known packet
	// results below use their accepted flags/state instead of this uncertainty.
	return candidate && !battle.hasReadyNativeRebirth(&unit) ? candidate : std::nullopt;
}
std::vector<newHorizonsReactiveDefense::Destruction> PhysicalStrikeProjection::captureDamagePacket(
	const HypotheticBattle & battle, std::span<const BattleStackAttacked> hits)
{
	std::vector<newHorizonsReactiveDefense::Destruction> result;
	for(const auto & hit : hits)
	{
		if((!hit.killed() && !hit.cloneKilled()) || hit.willRebirth())
			continue;
		const auto * unit = battle.battleGetUnitByID(hit.stackAttacked);
		if(!unit || !unit->alive()) continue;
		if(const auto candidate = configuredCasualty(battle, *unit)) result.push_back(*candidate);
	}
	return result;
}
std::vector<newHorizonsReactiveDefense::Destruction> PhysicalStrikeProjection::captureDamagePacket(
	const HypotheticBattle & battle, std::span<const UnitChanges> changes)
{
	std::vector<newHorizonsReactiveDefense::Destruction> result;
	for(const auto & change : changes)
	{
		if(change.operation != BattleChanges::EOperation::UPDATE || change.healthDelta >= 0)
			continue;
		const auto * unit = battle.battleGetUnitByID(change.id);
		if(!unit || !unit->alive()) continue;
		if(const auto candidate = configuredCasualty(battle, *unit)) result.push_back(*candidate);
	}
	return result;
}
void PhysicalStrikeProjection::completeDamagePacket(HypotheticBattle & battle,
	std::span<const newHorizonsReactiveDefense::Destruction> candidates)
{
	std::vector<newHorizonsReactiveDefense::Destruction> destroyed;
	for(const auto & candidate : candidates)
		if(const auto * unit = battle.battleGetUnitByID(candidate.unit); unit && !unit->alive())
			destroyed.push_back(candidate);
	for(const auto & plan : newHorizonsReactiveDefense::solidarityAftermath(battle, destroyed))
	{
		auto recipient = battle.getForUpdate(plan.target);
		recipient->removeUnitBonus(plan.remove);
		recipient->addUnitBonus(plan.add);
	}
}
int64_t PhysicalStrikeProjection::applyExpectedReactionDamage(HypotheticBattle & battle,
	const BattleAttackInfo & attack, battle::CUnitState & target)
{
	const auto candidate = captureForecastDamage(battle, target);
	auto amount = battle.battleExpectedLuckDamage(attack);
	vstd::amin(amount, target.getAvailableHealth());
	target.damage(amount);
	if(candidate) completeDamagePacket(battle, std::span(&*candidate, 1));
	return amount;
}

bool PhysicalStrikeProjection::needsProjection(const CBattleInfoCallback & battle,
	const battle::Unit * actor, const battle::Unit * target)
{
	using namespace newHorizonsReactiveDefense;
	return solidarityConfiguredPercent(battle, BattleSide::ATTACKER) > 0
		|| solidarityConfiguredPercent(battle, BattleSide::DEFENDER) > 0
		|| (actor && ironHideConfiguredPercent(battle, *actor) > 0)
		|| (target && ironHideConfiguredPercent(battle, *target) > 0);
}

void PhysicalStrikeProjection::damage(const CBattleInfoCallback & world, const BattleAttackInfo & attack,
	battle::CUnitState & target, int64_t & amount, battle::DamageProvenance provenance)
{
	using namespace newHorizonsReactiveDefense;
	// A selected strike describes its primary while damage visits all victims.
	// Collateral must never spend another stack's first-hit protection.
	auto hit = attack;
	hit.secondaryAttack |= !attack.defender || attack.defender->unitId() != target.unitId();
	hit.defender = &target;
	const auto spending = branch ? ironHideSpending(*branch, hit) : std::nullopt;
	const auto casualty = branch ? captureForecastDamage(*branch, target) : std::nullopt;
	sylvan.damage(world, attack, target, amount, provenance);
	if(!branch)
		return;
	if(spending)
	{
		auto recipient = branch->getForUpdate(target.unitId());
		recipient->removeUnitBonus(CSelector(isIronHideSpent));
		recipient->addUnitBonus({*spending}); // Accepted zero/lethal hits also spend.
	}
	if(casualty && !target.alive() && attack.attacker)
		deaths.push_back({attack.attacker->unitId(), *casualty});
}

void PhysicalStrikeProjection::finish(uint32_t attackerId)
{
	sylvan.finish(attackerId); // Preserve the existing Sylvan completion order.
	if(!branch || deaths.empty())
		return;
	std::vector<newHorizonsReactiveDefense::Destruction> packet;
	for(const auto & death : deaths)
	{
		if(death.actor != attackerId)
			continue;
		packet.push_back(death.victim);
	}
	completeDamagePacket(*branch, packet);
	std::erase_if(deaths, [attackerId](const Death & death) { return death.actor == attackerId; });
}

void PhysicalStrikeProjection::restoreEffects(HypotheticBattle & destination,
	const HypotheticBattle & captured)
{
	const CSelector effects([](const Bonus * bonus)
	{
		return newHorizonsReactiveDefense::isSolidarityProtection(bonus)
			|| newHorizonsReactiveDefense::isIronHideSpent(bonus);
	});
	for(const auto * final : captured.battleGetUnitsIf([](const battle::Unit *) { return true; }))
	{
		const auto * current = destination.battleGetUnitByID(final->unitId());
		if(!current)
			continue;
		const auto saved = final->getUnstackedBonuses(effects);
		if(saved->empty() && !current->hasBonus(effects))
			continue;
		std::vector<Bonus> values;
		for(const auto & bonus : *saved)
			values.push_back(*bonus);
		auto recipient = destination.getForUpdate(final->unitId());
		recipient->removeUnitBonus(effects);
		recipient->addUnitBonus(values);
	}
}
