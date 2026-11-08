/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "PhysicalStrikeProjection.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/NewHorizonsSpecialtyEvents.h"
#include "../../lib/battle/NewHorizonsAttackControllers.h"
#include "StackWithBonuses.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/ArcaneInspiration.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

namespace
{
std::optional<PhysicalStrikeProjection::Destruction> configuredCasualty(
	const HypotheticBattle & battle, const battle::Unit & unit)
{
	if(!unit.alive()) return std::nullopt;
	const auto solidarity = newHorizonsReactiveDefense::solidarityConfiguredPercent(battle, unit.unitSide()) > 0
		? newHorizonsReactiveDefense::captureDestruction(unit) : std::nullopt;
	if(!solidarity) return std::nullopt;
	return PhysicalStrikeProjection::Destruction{unit.unitId(), solidarity};
}
}
std::optional<PhysicalStrikeProjection::Destruction> PhysicalStrikeProjection::captureForecastDamage(
	const HypotheticBattle & battle, const battle::Unit & unit)
{
	if(!unit.alive())
		return std::nullopt;
	const auto candidate = configuredCasualty(battle, unit);
	// These direct forecasts do not resolve native Rebirth's roll. Known packet
	// results below use their accepted flags/state instead of this uncertainty.
	return candidate && !battle.hasReadyNativeRebirth(&unit) ? candidate : std::nullopt;
}
std::vector<PhysicalStrikeProjection::Destruction> PhysicalStrikeProjection::captureDamagePacket(
	const HypotheticBattle & battle, std::span<const BattleStackAttacked> hits)
{
	std::vector<Destruction> result;
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
std::vector<PhysicalStrikeProjection::Destruction> PhysicalStrikeProjection::captureDamagePacket(
	const HypotheticBattle & battle, std::span<const UnitChanges> changes)
{
	std::vector<Destruction> result;
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
	std::span<const Destruction> candidates)
{
	std::vector<newHorizonsReactiveDefense::Destruction> destroyed;
	for(const auto & candidate : candidates)
		if(const auto * unit = battle.battleGetUnitByID(candidate.unit); unit && !unit->alive())
		{
			if(candidate.solidarity) destroyed.push_back(*candidate.solidarity);
		}
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
	arcaneInspirationAI::StrikeProjection inspiration(&battle);
	const auto reprisal = inspiration.prepare(attack, amount);
	int64_t postShieldDamage = 0;
	target.damage(amount, false, attack.physicalDamage ? battle::DamageProvenance::PHYSICAL_CREATURE
		: battle::DamageProvenance::SPELL, &postShieldDamage);
	inspiration.record(reprisal, postShieldDamage);
	if(candidate) completeDamagePacket(battle, std::span(&*candidate, 1));
	if(attack.attacker) inspiration.finish(attack.attacker->unitId());
	return amount;
}

bool PhysicalStrikeProjection::needsProjection(const CBattleInfoCallback & battle,
	const battle::Unit * actor, const battle::Unit * target)
{
	using namespace newHorizonsReactiveDefense;
	return newHorizonsSpecialtyEvents::needsStrikeEvents(battle, actor, target)
		|| newHorizonsSpecialtyEvents::needsStrikeEvents(battle, target, actor)
		|| solidarityConfiguredPercent(battle, BattleSide::ATTACKER) > 0
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
	if(branch && attack.attacker && !hit.secondaryAttack)
	{
		auto plan = newHorizonsFactionSpellDamage::exposedWeakness(*branch, hit);
		if(!plan.add.empty()) exposures.emplace_back(attack.attacker->unitId(), std::move(plan));
	}
	if(branch && attack.attacker && !hit.secondaryAttack)
		for(auto & plan : newHorizonsSpecialtyEvents::acceptedStrike(*branch, hit))
			specialtyEvents.emplace_back(attack.attacker->unitId(), std::move(plan));
	const auto reprisal = inspiration.prepare(hit, amount);
	int64_t postShieldDamage = 0;
	sylvan.damage(world, attack, target, amount, provenance, &postShieldDamage);
	inspiration.record(reprisal, postShieldDamage);
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
	if(!branch) return;

	// Earned receipts survive zero damage, lethal hits and subsequent Stasis.
	// They are consumed before the ordinary counterstrike, including on a corpse.
	for(const auto & [actor, plan] : specialtyEvents)
		if(actor == attackerId)
		{
			auto target = branch->getForUpdate(plan.target);
			target->removeUnitBonus(plan.remove);
			target->addUnitBonus(plan.add);
		}
	std::erase_if(specialtyEvents, [attackerId](const auto & pending) { return pending.first == attackerId; });
	std::vector<Destruction> packet;
	for(const auto & death : deaths)
	{
		if(death.actor != attackerId)
			continue;
		packet.push_back(death.victim);
	}
	completeDamagePacket(*branch, packet);
	std::erase_if(deaths, [attackerId](const Death & death) { return death.actor == attackerId; });
	// Lethal indirect aftermath precedes the surviving primary's exposure.
	for(const auto & [actor, plan] : exposures)
		if(actor == attackerId)
		{
			auto target = branch->getForUpdate(plan.target);
			if(!target || !target->alive()) continue;
			target->removeUnitBonus(plan.remove);
			target->addUnitBonus(plan.add);
		}
	std::erase_if(exposures, [attackerId](const auto & pending) { return pending.first == attackerId; });
	inspiration.finish(attackerId); // Native Reprisal follows primary strike aftermath.
}

void PhysicalStrikeProjection::restoreEffects(HypotheticBattle & destination,
	const HypotheticBattle & captured)
{
	const CSelector effects([](const Bonus * bonus)
	{
		return newHorizonsSpecialtyEvents::isReceipt(bonus)
			|| newHorizonsAttackControllers::isCripple(bonus)
			|| arcaneInspiration::isEffect(bonus)
			|| newHorizonsReactiveDefense::isSolidarityProtection(bonus)
			|| newHorizonsReactiveDefense::isIronHideSpent(bonus)
			|| newHorizonsFactionSpellDamage::isExposedWeakness(bonus, BattleSide::ATTACKER)
			|| newHorizonsFactionSpellDamage::isExposedWeakness(bonus, BattleSide::DEFENDER);
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

bool PhysicalStrikeProjection::needsExposureProjection(const CBattleInfoCallback & battle)
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(const auto * hero = battle.battleGetFightingHero(side); hero
			&& hero->hasActivePerk("new-horizons:shroudOfMalassa", "new-horizons:shroudOfMalassa.exposedWeakness"))
			return true;
	return false;
}
