/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "AerialRetaliationProjection.h"
#include "AttackPossibility.h"
#include "BattleExchangeVariant.h"
#include "ParalyzingGazeProjection.h"
#include "../../lib/entities/hero/NewHorizonsAerialSupremacy.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
namespace aerialRetaliationAI
{
bool active(const CBattleInfoCallback & battle, const battle::Unit * actor)
{
	if(!actor) return false;
	const auto griffin = CreatureID(CreatureID::decode("core:griffin"));
	const auto royal = CreatureID(CreatureID::decode("core:royalGriffin"));
	return (actor->creatureId() == griffin || actor->creatureId() == royal)
		&& newHorizonsHeroes::aerialSupremacyEnabled(battle.battleGetOwnerHero(actor));
}
bool firstStrike(const battle::Unit & actor)
{
	static const auto selector = Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeAll)
		.Or(Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeRanged));
	return actor.hasBonus(selector) && !actor.hasBonusOfType(BonusType::NOT_ACTIVE);
}
attackPreview::Targets targets(const CBattleInfoCallback & battle, const battle::Unit * actor,
	const battle::Unit * requested, const battle::Unit * resolved,
	const newHorizonsAerialSupremacy::Excursion & excursion)
{
	// Only contact geometry uses landing. Neither the unit nor damage/aura
	// context changes position; a counter never creates a counter-to-counter.
	auto contact = battle.getAerialAttackedBattleUnits(excursion).first;
	vstd::erase_if(contact, [actor](const auto * unit) { return unit->unitId() == actor->unitId(); });
	attackPreview::Targets result{contact, contact, {}};
	const auto primaryFirst = [](battle::Units & units, const battle::Unit * primary, bool include)
	{
		const auto at = std::ranges::find_if(units,
			[primary](const auto * unit) { return unit->unitId() == primary->unitId(); });
		if(at != units.end()) std::rotate(units.begin(), at, std::next(at));
		else if(include) units.insert(units.begin(), primary);
	};
	primaryFirst(result.resolved, resolved, true);
	primaryFirst(result.requested, requested, resolved->unitId() != requested->unitId());
	return result;
}
void validateContext(const CBattleInfoCallback & battle, const BattleAttackInfo & attack,
	BattleHex from, const std::optional<newHorizonsAerialSupremacy::Excursion> & aerial)
{
	if(!aerial)
		return;
	const auto & excursion = *aerial;
	if(from != excursion.origin)
		throw std::runtime_error("Aerial preview cannot move its actor");
	if(!attack.attacker || !attack.defender || !attack.retaliation || attack.shooting
		|| !attack.physicalDamage || attack.chargeDistance != 0 || attack.secondaryAttack || attack.bracePreemptive
		|| attack.preemptiveDamagePercent > 0 || attack.cleaveDamagePercent > 0
		|| attack.attacker->unitId() != excursion.actor || attack.defender->unitId() != excursion.opponent
		|| attack.attackerPos != excursion.origin || attack.defenderPos != excursion.target
		|| attack.attacker->getPosition() != excursion.origin
		|| battle.battleGetUnitByID(excursion.actor) != attack.attacker
		|| battle.battleGetUnitByID(excursion.opponent) != attack.defender)
		throw std::runtime_error("Invalid Aerial Supremacy preview context");
	newHorizonsAerialSupremacy::validate(battle, excursion);
}
bool replayAdmission(HypotheticBattle & branch, const FortuneStrikeProjection & strike)
{
	if(strike.aerialAdmission)
	{
		newHorizonsAerialSupremacy::validate(branch, *strike.aerialAdmission);
		if(strike.gazeConsumption)
		{
			paralyzingGazeAI::replay(branch, *strike.gazeConsumption);
			if(!strike.gazeConsumption->ignoredByDefiant)
			{
				auto actor = branch.getForUpdate(strike.attackerId);
				if(actor->creatureId() == CreatureID(CreatureID::decode("core:griffin")))
					actor->aerialRetaliationRound = branch.battleGetRound();
			}
		}
		return false; // Typed admission is not a physical strike or activation.
	}
	if(strike.aerialExcursion)
		newHorizonsAerialSupremacy::validate(branch, *strike.aerialExcursion);
	return true;
}
int64_t replaySourceAftermath(HypotheticBattle & branch, const FortuneStrikeProjection & strike)
{
	if(!strike.aerialExcursion) return 0;
	auto source = branch.getForUpdate(strike.attackerId);
	const int64_t before = source->getAvailableHealth();
	if(source->alive() && strike.aerialReflectionDamage > 0)
	{
		const auto rebirthSource = branch.captureElementalRebirthSource(*source);
		const bool nativeRebirth = branch.hasReadyNativeRebirth(source.get());
		auto damage = strike.aerialReflectionDamage;
		source->damage(damage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
		if(!source->alive()) branch.releaseSerpentBindingsForDeath(*source);
		branch.recordBloodrageTransition(source, true);
		if(rebirthSource) branch.projectElementalRebirth(source.get(), *rebirthSource,
			!source->alive(), source->isClone(), nativeRebirth);
	}
	const int64_t lost = std::max<int64_t>(0, before - source->getAvailableHealth());
	if(source->alive() && strike.aerialVampirismHealing > 0)
	{
		auto healing = strike.aerialVampirismHealing;
		source->heal(healing, EHealLevel::HEAL, EHealPower::PERMANENT);
	}
	return lost;
}
void project(AttackPossibility & outer, const std::shared_ptr<HypotheticBattle> & branch,
	uint32_t shooterId, uint32_t griffinId, DamageCache & cache)
{
	if(!branch) return;
	const auto * griffin = branch->battleGetUnitByID(griffinId);
	const auto * shooter = branch->battleGetUnitByID(shooterId);
	const auto * state = dynamic_cast<const battle::CUnitState *>(griffin);
	if(!state || newHorizonsFrozen::isFrozen(*griffin) || state->armorerLastStandEndedActivation)
		return;
	const auto excursion = newHorizonsAerialSupremacy::prepare(*branch, griffin, shooter);
	if(!excursion) return;
	// Admission mutations stay in a child until existing selected replay commits
	// them once into the candidate branch, just like physical hit effects.
	auto admitted = std::make_shared<HypotheticBattle>(branch->env, branch);
	auto gaze = paralyzingGazeAI::consume(*admitted, shooterId, griffinId, false, excursion);
	const auto actor = admitted->getForUpdate(griffinId);
	const auto target = admitted->getForUpdate(shooterId);
	BattleAttackInfo attack(actor.get(), target.get(), 0, false);
	attack.retaliation = true;
	attack.attackerPos = excursion->origin;
	attack.defenderPos = excursion->target;
	AttackPossibility counter(excursion->origin, excursion->target, attack);
	if(!gaze || gaze->ignoredByDefiant)
		counter = AttackPossibility::evaluate(attack, excursion->origin, cache, admitted, false, excursion);
	else
	{
		counter.attackerState = actor;
		counter.affectedUnits.push_back(target);
		counter.effectPreview = admitted;
		FortuneStrikeProjection cancelled;
		cancelled.attackerId = griffinId;
		cancelled.defenderId = shooterId;
		cancelled.retaliation = true;
		cancelled.aerialAdmission = excursion;
		counter.fortuneStrikes.push_back(cancelled);
	}
	for(auto & strike : counter.fortuneStrikes)
		if(strike.aerialExcursion)
			for(const auto & [unit, healing] : counter.vampirismHealingByUnit)
				if(unit == griffinId) strike.aerialVampirismHealing += healing;
	if(!gaze || gaze->ignoredByDefiant)
	{
		FortuneStrikeProjection admission;
		admission.attackerId = griffinId;
		admission.defenderId = shooterId;
		admission.retaliation = true;
		admission.aerialAdmission = excursion;
		admission.gazeConsumption = gaze;
		counter.fortuneStrikes.insert(counter.fortuneStrikes.begin(), admission);
	}
	else counter.fortuneStrikes.front().gazeConsumption = gaze;
	const bool attempted = (gaze && !gaze->ignoredByDefiant)
		|| std::ranges::any_of(counter.fortuneStrikes, [](const auto & strike) { return strike.aerialExcursion.has_value(); });
	if(attempted && actor->creatureId() == CreatureID(CreatureID::decode("core:griffin")))
		counter.attackerState->aerialRetaliationRound = branch->battleGetRound();
	outer.deferredBloodAttempts.insert(outer.deferredBloodAttempts.end(),
		counter.deferredBloodAttempts.begin(), counter.deferredBloodAttempts.end());
	BattleExchangeVariant committed;
	committed.trackAttack(counter, branch, cache);
	outer.attackerDamageReduce += counter.defenderDamageReduce + counter.shootersBlockedDmg;
	outer.defenderDamageReduce += counter.attackerDamageReduce + counter.collateralDamageReduce;
	outer.frozenControlValue -= counter.frozenControlValue;
	outer.vampirismHealingByUnit.insert(outer.vampirismHealingByUnit.end(),
		counter.vampirismHealingByUnit.begin(), counter.vampirismHealingByUnit.end());
	for(const auto & captured : counter.affectedUnits)
		if(captured->unitId() != outer.attackerState->unitId()
			&& !std::ranges::any_of(outer.affectedUnits, [&](const auto & unit)
				{ return unit->unitId() == captured->unitId(); }))
			outer.affectedUnits.push_back(branch->getForUpdate(captured->unitId()));
	outer.fortuneStrikes.insert(outer.fortuneStrikes.end(),
		counter.fortuneStrikes.begin(), counter.fortuneStrikes.end());
}
}
