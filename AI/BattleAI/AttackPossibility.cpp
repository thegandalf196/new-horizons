/*
 * AttackPossibility.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "AttackPossibility.h"
#include "NewHorizonsHexOfPain.h"
#include "../../lib/CStack.h" // TODO: remove
#include "../../lib/CSkillHandler.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
                              // Eventually only IBattleInfoCallback and battle::Unit should be used, 
                              // CUnitState should be private and CStack should be removed completely
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/ObstacleCasterProxy.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/battle/NewHorizonsBattlecraft.h"
#include "../../lib/battle/NewHorizonsArchery.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsBulwark.h"
#include "../../lib/spells/NewHorizonsSorcery.h"
#include "../../lib/spells/NewHorizonsMagic.h"

#include "../../lib/GameLibrary.h"

#include <vcmi/spells/Service.h>
#include <vcmi/spells/Spell.h>


namespace
{
bool hasRangedMarkEffect(const battle::Unit * unit, const char * source)
{
	if(!unit)
		return false;
	const auto triggers = unit->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER);
	for(const auto & bonus : *triggers)
		if(bonus->source == BonusSource::SPELL_EFFECT && bonus->sid.toString() == source)
			return true;
	return false;
}

int bulwarkPreemptivePercent(const battle::Unit * defender, const CBattleInfoCallback & state)
{
	if(!defender || !defender->defended()
		|| !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(defender))
		return 0;
	const auto * hero = state.battleGetOwnerHero(defender);
	const int rank = newHorizonsBulwark::rank(hero);
	const auto unitState = defender->acquireState();
	if(rank <= 0 || !unitState || unitState->bulwarkPreemptiveUsed)
		return 0;
	return newHorizonsBulwark::preemptivePercent(rank, newHorizonsBulwark::hasBogAmbush(hero));
}

int bulwarkReflectionBasisPoints(const battle::Unit * defender, const CBattleInfoCallback & state,
	bool ranged)
{
	if(!defender || !defender->defended()
		|| !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(defender))
		return 0;
	const auto * hero = state.battleGetOwnerHero(defender);
	return newHorizonsBulwark::reflectionBasisPoints(newHorizonsBulwark::rank(hero), ranged,
		newHorizonsBulwark::hasThickHide(hero), newHorizonsBulwark::hasVengefulMire(hero));
}

int32_t noQuarterMoraleActivations(const CBattleInfoCallback & battle, uint32_t targetUnitId)
{
	return battle.getBattle()->getActiveStackID() == static_cast<int32_t>(targetUnitId) ? 2 : 1;
}

int64_t projectedPhysicalPoisonDamage(const battle::CUnitState * state)
{
	if(!state || state->physicalPoisonBaseDamage <= 0 || state->physicalPoisonActivationsRemaining <= 0)
		return 0;
	const int64_t base = state->physicalPoisonBaseDamage;
	const int remaining = state->physicalPoisonActivationsRemaining;
	if(remaining >= 3)
		return base * 4 + base / 2;
	if(remaining == 2)
		return base + base / 2 + base * 2;
	return base * 2;
}

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
}

void DamageCache::cacheDamage(const battle::Unit * attacker, const battle::Unit * defender, std::shared_ptr<CBattleInfoCallback> hb)
{
	if(hasRangedMarkEffect(defender, newHorizonsSorcery::ARCANE_BREACH_EFFECT))
		rangedMarkTargets.insert(defender->unitId());
	auto damage = hb->battleExpectedLuckDamage(BattleAttackInfo(attacker, defender, 0, hb->battleCanShoot(attacker, defender->getPosition())));

	damageCache[attacker->unitId()][defender->unitId()] = static_cast<float>(damage) / attacker->getCount();
}

void DamageCache::buildObstacleDamageCache(std::shared_ptr<HypotheticBattle> hb, BattleSide side)
{
	for(const auto & obst : hb->battleGetAllObstacles(side))
	{
		auto spellObstacle = dynamic_cast<const SpellCreatedObstacle *>(obst.get());

		if(!spellObstacle || !obst->triggersEffects())
			continue;

		auto triggerAbility = LIBRARY->spells()->getById(obst->getTrigger());
		auto triggerIsNegative = triggerAbility->isNegative() || triggerAbility->isDamage();

		if(!triggerIsNegative)
			continue;

		std::unique_ptr<spells::BattleCast> cast = nullptr;
		std::unique_ptr<spells::ObstacleCasterProxy> caster = nullptr;
		if(spellObstacle->obstacleType == SpellCreatedObstacle::EObstacleType::SPELL_CREATED)
		{
			const auto perspective = hb->battleGetMySide();
			const bool casterKnown = perspective == BattleSide::ALL_KNOWING || perspective == spellObstacle->casterSide;
			const auto * hero = casterKnown ? hb->battleGetFightingHero(spellObstacle->casterSide) : nullptr;
			caster = std::make_unique<spells::ObstacleCasterProxy>(hb->getSidePlayer(spellObstacle->casterSide), hero, *spellObstacle);
			cast = std::make_unique<spells::BattleCast>(spells::BattleCast(hb.get(), caster.get(), spells::Mode::PASSIVE, obst->getTrigger().toSpell()));
		}

		auto affectedHexes = obst->getAffectedTiles();
		auto stacks = hb->battleGetUnitsIf([](const battle::Unit * u) -> bool {
			return u->alive() && !u->isTurret() && u->getPosition().isValid();
		});

		auto inner = std::make_shared<HypotheticBattle>(hb->env, hb);

		for(auto stack : stacks)
		{
			auto updated = inner->getForUpdate(stack->unitId());

			spells::Target target;
			target.push_back(spells::Destination(updated.get()));

			if(cast)
				cast->castEval(inner->getServerCallback(), target);

			auto damageDealt = stack->getAvailableHealth() - updated->getAvailableHealth();

			for(const auto & hex : affectedHexes)
			{
				obstacleDamage[hex][stack->unitId()] = damageDealt;
			}
		}
	}
}

void DamageCache::buildDamageCache(std::shared_ptr<HypotheticBattle> hb, BattleSide side)
{
	if(parent == nullptr)
	{
		buildObstacleDamageCache(hb, side);
	}

	auto stacks = hb->battleGetUnitsIf([=](const battle::Unit * u) -> bool
		{
			return u->isValidTarget();
		});

	battle::Units ourUnits;
	battle::Units enemyUnits;

	for(auto stack : stacks)
	{
		if(stack->unitSide() == side)
			ourUnits.push_back(stack);
		else
			enemyUnits.push_back(stack);
	}

	for(auto ourUnit : ourUnits)
	{
		if(!ourUnit->alive())
			continue;

		for(auto enemyUnit : enemyUnits)
		{
			if(enemyUnit->alive())
			{
				cacheDamage(ourUnit, enemyUnit, hb);
				cacheDamage(enemyUnit, ourUnit, hb);
			}
		}
	}
}

bool DamageCache::tracksRangedMarks(uint32_t defenderId) const
{
	for(const auto * cache = this; cache; cache = cache->parent)
		if(cache->rangedMarkTargets.contains(defenderId))
			return true;
	return false;
}

int64_t DamageCache::getDamage(const battle::Unit * attacker, const battle::Unit * defender, std::shared_ptr<CBattleInfoCallback> hb)
{
	if(hasRangedMarkEffect(defender, newHorizonsSorcery::ARCANE_BREACH_EFFECT))
		rangedMarkTargets.insert(defender->unitId());
	const auto raSide = hb->playerToSide(hb->battleGetOwner(attacker));
	const auto * raHero = raSide == BattleSide::ATTACKER || raSide == BattleSide::DEFENDER
		? hb->battleGetFightingHero(raSide) : nullptr;
	const bool hasRelentlessAssault = raHero
		&& raHero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::RELENTLESS_ASSAULT);
	// IDs alone cannot key a target/controller/round-sensitive premium. Preserve
	// original-damage snapshots for comparison, but recompute current v2 damage.
	// Remember marked targets so expiry/Dispel cannot revive a cached premium.
	// Relentless Assault depends on the hero-side streak and target; never let an
	// ID-only cache reuse damage from another hypothetical chain tier.
	if(heroCommands::supportedByRules(hb->getBattle()->getHeroCommandRules(), HeroCommand::FOCUS_FIRE)
		|| newHorizonsBattlecraft::rank(hb->battleGetOwnerHero(attacker)) > 0
		|| hasRelentlessAssault
		|| tracksRangedMarks(defender->unitId()))
	{
		if(!attacker->alive())
			return 0;
		const bool shooting = hb->battleCanShoot(attacker, defender->getPosition());
		BattleAttackInfo attack(attacker, defender, 0, shooting);
		const auto * primaryTarget = hb->battleResolveHeroOrderTarget(attacker, defender, shooting);
		attack.defender = primaryTarget ? primaryTarget : defender;
		attack.protectIntercepted = attack.defender->unitId() != defender->unitId();
		if(hasRelentlessAssault)
		{
			attack.relentlessAssaultDamagePercent = hb->battleGetRelentlessAssaultDamagePercent(
				attacker, attack.defender);
		}
		return hb->battleExpectedLuckDamage(attack);
	}
	bool wasComputedBefore = damageCache[attacker->unitId()].count(defender->unitId());

	if (!wasComputedBefore)
		cacheDamage(attacker, defender, hb);

	return damageCache[attacker->unitId()][defender->unitId()] * attacker->getCount();
}

int64_t DamageCache::getObstacleDamage(const BattleHex & hex, const battle::Unit * defender)
{
	if(parent)
		return parent->getObstacleDamage(hex, defender);

	auto damages = obstacleDamage.find(hex);

	if(damages == obstacleDamage.end())
		return 0;

	auto damage = damages->second.find(defender->unitId());

	return damage == damages->second.end()
		? 0
		: damage->second;
}

int64_t DamageCache::getOriginalDamage(const battle::Unit * attacker, const battle::Unit * defender, std::shared_ptr<CBattleInfoCallback> hb)
{
	if(parent)
	{
		auto attackerDamageMap = parent->damageCache.find(attacker->unitId());

		if(attackerDamageMap != parent->damageCache.end())
		{
			auto targetDamage = attackerDamageMap->second.find(defender->unitId());

			if(targetDamage != attackerDamageMap->second.end())
			{
				return static_cast<int64_t>(targetDamage->second * attacker->getCount());
			}
		}
	}

	return getDamage(attacker, defender, hb);
}

AttackPossibility::AttackPossibility(const BattleHex & from, const BattleHex & dest, const BattleAttackInfo & attack)
	: from(from), dest(dest), attack(attack)
{
	this->attack.attackerPos = from;
	this->attack.defenderPos = dest;
}

float AttackPossibility::damageDiff() const
{
	return defenderDamageReduce - attackerDamageReduce - collateralDamageReduce + shootersBlockedDmg;
}

float AttackPossibility::damageDiff(float positiveEffectMultiplier, float negativeEffectMultiplier) const
{
	return positiveEffectMultiplier * (defenderDamageReduce + shootersBlockedDmg)
		- negativeEffectMultiplier * (attackerDamageReduce + collateralDamageReduce);
}

float AttackPossibility::attackValue() const
{
	return damageDiff();
}

float hpFunction(uint64_t unitHealthStart, uint64_t unitHealthEnd, uint64_t maxHealth)
{
	float ratioStart = static_cast<float>(unitHealthStart) / maxHealth;
	float ratioEnd = static_cast<float>(unitHealthEnd) / maxHealth;
	float base = 0.666666f;

	// reduce from max to 0 must be 1. 
	// 10 hp from end costs bit more than 10 hp from start because our goal is to kill unit, not just hurt it
	// ********** 2 * base - ratioStart *********
	// *                                                              *
	// *        height = ratioStart - ratioEnd         *
	// *                                                                  *
	// ******************** 2 * base - ratioEnd ******
	// S = (a + b) * h / 2
	return (base * (4 - ratioStart - ratioEnd)) * (ratioStart - ratioEnd) / 2 ;
}

/// <summary>
/// How enemy damage will be reduced by this attack
/// Half bounty for kill, half for making damage equal to enemy health
/// Bounty - the killed creature average damage calculated against attacker
/// </summary>
float AttackPossibility::calculateDamageReduce(
	const battle::Unit * attacker,
	const battle::Unit * defender,
	uint64_t damageDealt,
	DamageCache & damageCache,
	std::shared_ptr<CBattleInfoCallback> state)
{
	const float HEALTH_BOUNTY = 0.5;
	const float KILL_BOUNTY = 0.5;

	// FIXME: provide distance info for Jousting bonus
	auto attackerUnitForMeasurement = attacker;

	if(!attackerUnitForMeasurement || attackerUnitForMeasurement->isTurret())
	{
		auto ourUnits = state->battleGetUnitsIf([&](const battle::Unit * u) -> bool
			{
				return state->battleGetOwner(u) != state->battleGetOwner(defender)
					&& !u->isTurret()
					&& !u->isCatapult()
					&& !u->isBallista()
					&& !u->isFirstAidTent()
					&& u->getCount();
			});

		if(ourUnits.empty())
			attackerUnitForMeasurement = defender;
		else
			attackerUnitForMeasurement = ourUnits.front();
	}

	// Phantom Army has one stack-wide Integrity pool, not ordinary per-creature
	// health. Losing Integrity does not reduce the copied stack's offensive
	// count; only exhausting the pool removes the full copied stack.
	if(defender->getPhantomInitialIntegrity() > 0)
	{
		const auto integrity = defender->getPhantomIntegrity();
		if(integrity <= 0 || defender->getCount() <= 0
			|| damageDealt < static_cast<uint64_t>(integrity))
			return 0.0f;

		const auto copiedStackDamage = damageCache.getOriginalDamage(defender, attackerUnitForMeasurement, state);
		return static_cast<float>(copiedStackDamage);
	}

	auto maxHealth = defender->getMaxHealth();
	auto availableHealth = defender->getFirstHPleft() + ((defender->getCount() - 1) * maxHealth);

	vstd::amin(damageDealt, availableHealth);

	auto enemyDamageBeforeAttack = damageCache.getOriginalDamage(defender, attackerUnitForMeasurement, state);
	auto enemiesKilled = damageDealt / maxHealth + (damageDealt % maxHealth >= defender->getFirstHPleft() ? 1 : 0);
	auto damagePerEnemy = enemyDamageBeforeAttack / (double)defender->getCount();
	auto exceedingDamage = (damageDealt % maxHealth);
	float hpValue = (damageDealt / maxHealth);
	
	if(defender->getFirstHPleft() >= exceedingDamage)
	{
		hpValue += hpFunction(defender->getFirstHPleft(), defender->getFirstHPleft() - exceedingDamage, maxHealth);
	}
	else
	{
		hpValue += hpFunction(defender->getFirstHPleft(), 0, maxHealth);
		hpValue += hpFunction(maxHealth, maxHealth + defender->getFirstHPleft() - exceedingDamage, maxHealth);
	}

	return damagePerEnemy * (enemiesKilled * KILL_BOUNTY + hpValue * HEALTH_BOUNTY);
}

int64_t AttackPossibility::evaluateBlockedShootersDmg(
	const BattleAttackInfo & attackInfo,
	BattleHex hex,
	DamageCache & damageCache,
	std::shared_ptr<CBattleInfoCallback> state)
{
	int64_t res = 0;

	if(attackInfo.shooting)
		return 0;

	std::set<uint32_t> checkedUnits;

	auto attacker = attackInfo.attacker;
	const auto & hexes = attacker->getSurroundingHexes(hex);
	for(const BattleHex & tile : hexes)
	{
		auto st = state->battleGetUnitByPos(tile, true);
		if(!st || !state->battleMatchOwner(st, attacker))
			continue;
		if(vstd::contains(checkedUnits, st->unitId()))
			continue;
		if(!state->battleCanShoot(st))
			continue;

		checkedUnits.insert(st->unitId());

		// FIXME: provide distance info for Jousting bonus
		BattleAttackInfo rangeAttackInfo(st, attacker, 0, true);
		rangeAttackInfo.defenderPos = hex;

		BattleAttackInfo meleeAttackInfo(st, attacker, 0, false);
		meleeAttackInfo.defenderPos = hex;

		auto rangeDmg = state->battleExpectedLuckDamage(rangeAttackInfo);
		auto meleeDmg = state->battleExpectedLuckDamage(meleeAttackInfo);
		auto cachedDmg = damageCache.getOriginalDamage(st, attacker, state);

		int64_t gain = rangeDmg - meleeDmg + 1;
		res += gain * cachedDmg / std::max<int64_t>(1, rangeDmg);
	}

	return res;
}

int AttackPossibility::getAttackCount(const battle::Unit & attacker, bool shooting, const CBattleInfoCallback & state)
{
	int result = attacker.getTotalAttacks(shooting);
	// BattleAction uses the unit's battle side, including when estimating an
	// opponent's action. A player-scoped callback must not probe that opponent's
	// private hero object; use only the attacks already exposed by the unit.
	const auto perspective = state.battleGetMySide();
	const bool attackerHeroKnown = perspective == BattleSide::ALL_KNOWING || perspective == attacker.unitSide();
	const auto * hero = attackerHeroKnown ? state.battleGetFightingHero(attacker.unitSide()) : nullptr;
	if(hero)
		result += hero->valOfBonuses(BonusType::HERO_GRANTS_ATTACKS, BonusSubtypeID(attacker.creatureId()));
	if(shooting)
		if(const auto * unitState = dynamic_cast<const battle::CUnitState *>(&attacker);
			unitState && unitState->shots.isLimited())
			result = std::min(result, unitState->shots.available());
	return result;
}

AttackPossibility AttackPossibility::evaluate(
	const BattleAttackInfo & attackInfo,
	BattleHex hex,
	DamageCache & damageCache,
	std::shared_ptr<CBattleInfoCallback> state, bool perfectMoment)
{
	auto attacker = attackInfo.attacker;
	const auto * requestedDefender = attackInfo.defender;
	const auto * redirectedDefender = state->battleResolveHeroOrderTarget(attacker, requestedDefender,
		attackInfo.shooting);
	const auto * defender = redirectedDefender ? redirectedDefender : requestedDefender;
	const std::string cachingStringBlocksRetaliation = "type_BLOCKS_RETALIATION";
	static const auto selectorBlocksRetaliation = Selector::type()(BonusType::BLOCKS_RETALIATION);
	const auto attackerSide = state->playerToSide(state->battleGetOwner(attacker));
	const bool counterAttacksBlocked = attacker->hasBonus(selectorBlocksRetaliation, cachingStringBlocksRetaliation);
	static const auto firstStrikeSelector = Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeAll)
		.Or(Selector::typeSubtype(BonusType::FIRST_STRIKE, BonusCustomSubtype::damageTypeMelee));

	AttackPossibility bestAp(hex, BattleHex::INVALID, attackInfo);

	BattleHexArray defenderHex;
	if(attackInfo.shooting)
		defenderHex.insert(requestedDefender->getPosition());
	else
		defenderHex = state->meleeAttackHexes(attacker, requestedDefender, hex);

	for(const BattleHex & defHex : defenderHex)
	{
		if(defHex == hex) // should be impossible but check anyway
			continue;

		AttackPossibility ap(hex, defHex, attackInfo);
		ap.attack.protectIntercepted = !attackInfo.shooting
			&& defender->unitId() != requestedDefender->unitId();
		ap.perfectMoment = perfectMoment && state->battleCanUsePerfectMoment(attacker)
			&& !attackInfo.retaliation && state->battleMatchOwner(attacker, defender);
		const auto * raPrimaryTarget = state->battleResolveHeroOrderTarget(attacker, requestedDefender,
			attackInfo.shooting);
		const auto * raHero = attackerSide == BattleSide::ATTACKER || attackerSide == BattleSide::DEFENDER
			? state->battleGetFightingHero(attackerSide) : nullptr;
		const bool ordinaryArcheryShooter = newHorizonsArchery::isOrdinaryPhysicalShooter(attacker);
		const auto currentRound = state->battleGetRound();
		const auto currentActivationSerial = static_cast<int32_t>(state->getBattle()->getActivationSerial());
		const auto initialAttackerState = attacker->acquireState();
		const bool projectsDeadeye = attackInfo.shooting && attackInfo.physicalDamage && !attackInfo.retaliation
			&& ordinaryArcheryShooter && raHero && newHorizonsArchery::hasDeadeye(raHero)
			&& initialAttackerState->archeryDeadeyeRound != currentRound;
		const bool projectsCrossfire = ordinaryArcheryShooter && raHero && newHorizonsArchery::hasCrossfire(raHero);
		const bool projectsSuppression = attackInfo.shooting && attackInfo.physicalDamage && !attackInfo.retaliation
			&& ordinaryArcheryShooter && raHero && newHorizonsArchery::hasSuppression(raHero)
			&& initialAttackerState->archerySuppressionActivationSerial != currentActivationSerial;
		const bool projectsRainOfArrows = attackInfo.shooting && attackInfo.physicalDamage && !attackInfo.retaliation
			&& ordinaryArcheryShooter && raHero && newHorizonsArchery::hasRainOfArrows(raHero)
			&& initialAttackerState->archeryRainOfArrowsActivationSerial != currentActivationSerial;
		const auto rainPrimaryFootprint = requestedDefender->getHexes();
		const bool projectsArcheryState = projectsDeadeye || projectsCrossfire || projectsSuppression || projectsRainOfArrows;
		const bool projectsSkirmisher = attackInfo.shooting && hex.isValid()
			&& hex != attacker->getPosition()
			&& attackInfo.archeryRangedDamageMultiplierPercent == newHorizonsArchery::SKIRMISHER_DAMAGE_PERCENT
			&& newHorizonsArchery::canUseSkirmisher(raHero, attacker);
		const bool ordinaryRelentlessAssaultAttack = raHero
			&& raHero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::RELENTLESS_ASSAULT)
			&& !attackInfo.retaliation && !attackInfo.secondaryAttack && !attackInfo.bracePreemptive
			&& attackInfo.preemptiveDamagePercent <= 0 && attackInfo.cleaveDamagePercent <= 0
			&& attackInfo.physicalDamage && attacker->alive() && raPrimaryTarget && raPrimaryTarget->alive()
			&& !attacker->isGhost() && !attacker->isTurret()
			&& !attacker->hasBonusOfType(BonusType::SIEGE_WEAPON)
			&& attacker->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER
			&& !attacker->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK)
			&& !raPrimaryTarget->isGhost() && !raPrimaryTarget->isTurret()
			&& !raPrimaryTarget->hasBonusOfType(BonusType::SIEGE_WEAPON)
			&& raPrimaryTarget->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER
			&& state->battleMatchOwner(attacker, raPrimaryTarget);
		if(ordinaryRelentlessAssaultAttack)
		{
			ap.attack.relentlessAssaultDamagePercent = state->battleGetRelentlessAssaultDamagePercent(attacker, raPrimaryTarget);
		}
		std::shared_ptr<HypotheticBattle> fortunePreview;
		BattleAttackInfo potentialRetaliation(defender, attacker, 0, false);
		potentialRetaliation.retaliation = true;
		const bool projectsNoQuarter = !attackInfo.shooting && attackInfo.physicalDamage
			&& (state->battleCanTriggerNoQuarter(attackInfo)
				|| state->battleCanTriggerNoQuarter(potentialRetaliation));
		const bool projectsCleave = !attackInfo.shooting && !attackInfo.retaliation
			&& !attackInfo.secondaryAttack && !attackInfo.bracePreemptive
			&& attackInfo.preemptiveDamagePercent <= 0 && attackInfo.cleaveDamagePercent <= 0
			&& attackInfo.physicalDamage && state->battleCanTriggerCleave(attacker);
	const bool projectsMarks = attackInfo.shooting
		&& hasRangedMarkEffect(attacker, newHorizonsSorcery::FOCUS_MAGIC_SPELL);
	const bool projectsHexOfPain = newHorizonsHexOfPainAI::hasEffect(attacker)
		|| (!attackInfo.shooting && newHorizonsHexOfPainAI::hasEffect(defender));
		const bool projectsProtect = !attackInfo.shooting
			&& defender->unitId() != requestedDefender->unitId();
		const bool ordinaryAttacker = newHorizonsCombatSkills::isOrdinaryCreatureAttacker(attacker);
		const bool mayReceiveBulwarkReaction = ordinaryAttacker && attackInfo.physicalDamage
			&& !attackInfo.shooting && !attackInfo.retaliation
			&& !attackInfo.bracePreemptive && attackInfo.preemptiveDamagePercent <= 0
			&& (bulwarkPreemptivePercent(defender, *state) > 0
				|| bulwarkPreemptivePercent(requestedDefender, *state) > 0);
		const bool mayReflectBulwarkDamage = attackInfo.physicalDamage && ordinaryAttacker
			&& (bulwarkReflectionBasisPoints(defender, *state, attackInfo.shooting) > 0
				|| bulwarkReflectionBasisPoints(requestedDefender, *state, attackInfo.shooting) > 0);
		const int bulwarkRound = state->battleGetRound();
		const auto * defendedHero = state->battleGetOwnerHero(defender);
		const auto defenderInitialState = defender->acquireState();
		const bool projectsImmovable = attackInfo.physicalDamage && ordinaryAttacker
			&& defender->defended() && newHorizonsCombatSkills::isOrdinaryCreatureAttacker(defender)
			&& newHorizonsBulwark::hasImmovable(defendedHero) && defenderInitialState
			&& defenderInitialState->bulwarkImmovableRound != bulwarkRound;
		const bool projectsSwampRenewal = attackInfo.physicalDamage && ordinaryAttacker
			&& defender->defended() && newHorizonsCombatSkills::isOrdinaryCreatureAttacker(defender)
			&& newHorizonsBulwark::hasSwampRenewal(defendedHero);
		const auto qualifiesForMireGrip = [&state](const battle::Unit * target)
		{
			return target && target->defended()
				&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(target)
				&& newHorizonsBulwark::hasMireGrip(state->battleGetOwnerHero(target));
		};
		const auto canBeHitByMireGripAttack = [&](const battle::Unit * primaryTarget)
		{
			if(!primaryTarget)
				return false;
			if(qualifiesForMireGrip(primaryTarget))
				return true;
			const auto possibleVictims = state->getAttackedBattleUnits(attacker, primaryTarget,
				defHex, false, hex, primaryTarget->getPosition());
			return std::ranges::any_of(possibleVictims, qualifiesForMireGrip);
		};
		const bool mireGripTargetCanBeHit = !attackInfo.shooting
			&& (canBeHitByMireGripAttack(defender)
				|| (projectsProtect && canBeHitByMireGripAttack(requestedDefender)));
		const auto attackerInitialState = attacker->acquireState();
		const bool projectsMireGrip = attackInfo.physicalDamage && !attackInfo.shooting
			&& ordinaryAttacker && mireGripTargetCanBeHit && attackerInitialState
			&& !attackerInitialState->bulwarkMireGripApplied;
		const bool projectsBulwarkEffects = mayReceiveBulwarkReaction || mayReflectBulwarkDamage
			|| projectsImmovable || projectsSwampRenewal || projectsMireGrip;
	if(ap.perfectMoment || projectsMarks || projectsHexOfPain || projectsCleave || projectsProtect || projectsSkirmisher
			|| ordinaryRelentlessAssaultAttack || projectsNoQuarter || projectsArcheryState
			|| projectsBulwarkEffects)
			if(const auto model = std::dynamic_pointer_cast<HypotheticBattle>(state))
				fortunePreview = std::make_shared<HypotheticBattle>(model->env, state);
	if(projectsMarks || projectsHexOfPain || projectsCleave || projectsProtect || projectsSkirmisher
			|| ordinaryRelentlessAssaultAttack || projectsNoQuarter || projectsArcheryState
			|| projectsBulwarkEffects)
			ap.effectPreview = fortunePreview;
	const CBattleInfoCallback & luckState = fortunePreview
		? static_cast<const CBattleInfoCallback &>(*fortunePreview) : *state;
	const auto scoreHexPain = [&](battle::CUnitState * recipient, int64_t damage)
	{
		if(!recipient || damage <= 0)
			return;

		const auto value = calculateDamageReduce(nullptr, recipient,
			static_cast<uint64_t>(damage), damageCache, state);
		if(state->battleMatchOwner(attacker, recipient, true))
			ap.attackerDamageReduce += value;
		else
			ap.defenderDamageReduce += value;
	};
	ap.attackerState = ap.effectPreview
			? std::static_pointer_cast<battle::CUnitState>(ap.effectPreview->getForUpdate(attacker->unitId()))
			: attacker->acquireState();
		ap.shootersBlockedDmg = bestAp.shootersBlockedDmg;

		const int totalAttacks = getAttackCount(*ap.attackerState, attackInfo.shooting, *state);

		if(!attackInfo.shooting || projectsSkirmisher)
			ap.attackerState->setPosition(hex);

		battle::Units defenderUnits;
		battle::Units requestedDefenderUnits;
		battle::Units retaliatedUnits = {attacker};
		battle::Units affectedUnits;

		if (attackInfo.shooting)
			defenderUnits = state->getAttackedBattleUnits(attacker, defender, defHex, true, hex, defender->getPosition());
		else
		{
			defenderUnits = state->getAttackedBattleUnits(attacker, defender, defHex, false, hex, defender->getPosition());
			// Keep the originally requested footprint even when Protect has already
			// spent its allowance (or is broken/unavailable). Later strikes then
			// correctly fall back to the Ward instead of evaluating an empty target set.
			requestedDefenderUnits = state->getAttackedBattleUnits(attacker, requestedDefender, defHex,
				false, hex, requestedDefender->getPosition());
			retaliatedUnits = state->getAttackedBattleUnits(defender, attacker, hex, false, defender->getPosition(), hex);

			// attacker can not melle-attack itself but still can hit that place where it was before moving
			vstd::erase_if(defenderUnits, [attacker](const battle::Unit * u) -> bool { return u->unitId() == attacker->unitId(); });
			vstd::erase_if(requestedDefenderUnits, [attacker](const battle::Unit * u) -> bool { return u->unitId() == attacker->unitId(); });

			if(!vstd::contains_if(retaliatedUnits, [attacker](const battle::Unit * u) -> bool { return u->unitId() == attacker->unitId(); }))
			{
				retaliatedUnits.push_back(attacker);
			}

			auto obstacleDamage = damageCache.getObstacleDamage(hex, attacker);

			if(obstacleDamage > 0)
			{
				ap.attackerDamageReduce += calculateDamageReduce(nullptr, attacker, obstacleDamage, damageCache, state);

				ap.attackerState->damage(obstacleDamage);
				ap.preAttackDamage += obstacleDamage;
			}
		}
		if(projectsProtect && !vstd::contains_if(requestedDefenderUnits, [requestedDefender](const battle::Unit * unit)
			{ return unit->unitId() == requestedDefender->unitId(); }))
			requestedDefenderUnits.push_back(requestedDefender);

		// ensure the defender is also affected
		if(!vstd::contains_if(defenderUnits, [defender](const battle::Unit * u) -> bool { return u->unitId() == defender->unitId(); }))
		{
			defenderUnits.push_back(defender);
		}

		affectedUnits = defenderUnits;
		for(const auto * unit : requestedDefenderUnits)
			if(!vstd::contains_if(affectedUnits, [unit](const battle::Unit * value)
				{ return value->unitId() == unit->unitId(); }))
				affectedUnits.push_back(unit);
		vstd::concatenate(affectedUnits, retaliatedUnits);

#if BATTLE_TRACE_LEVEL>=1
		logAi->trace("Attacked battle units count %d, %d->%d", affectedUnits.size(), hex, defHex);
#endif

		std::map<uint32_t, std::shared_ptr<battle::CUnitState>> defenderStates;

		for(auto u : affectedUnits)
		{
			if(u->unitId() == attacker->unitId())
				continue;

			auto defenderState = ap.effectPreview
				? std::static_pointer_cast<battle::CUnitState>(ap.effectPreview->getForUpdate(u->unitId()))
				: u->acquireState();

			ap.affectedUnits.push_back(defenderState);
			defenderStates[u->unitId()] = defenderState;
		}
		const auto protectSide = requestedDefender->unitSide();
		auto protectOrder = projectsProtect ? state->battleGetHeroOrderState(protectSide) : std::nullopt;
		uint8_t projectedProtectInterceptionsConsumed = protectOrder
			? protectOrder->protectInterceptionsConsumed : 0;
		int64_t projectedRainPrimaryDamage = 0;
		bool projectedSuppressionSpent = false;

		for(int i = 0; i < totalAttacks; i++)
		{
			// Resolve Protect against the projected count before every blow. This
			// gives ordinary Protect one redirect and Shield Master two, while a
			// dead Protector or broken pair immediately falls back to the Ward.
			const battle::Unit * strikeDefender = requestedDefender;
			const battle::Units * strikeDefenderUnits = &defenderUnits;
			if(projectsProtect)
			{
				const auto wardState = defenderStates.find(requestedDefender->unitId());
				const battle::Unit * projectedWard = wardState != defenderStates.end()
					? wardState->second.get() : requestedDefender;
				const auto * resolved = fortunePreview
					? fortunePreview->battleResolveHeroOrderTarget(ap.attackerState.get(), projectedWard, false)
					: (protectOrder && !protectOrder->protectBroken
						&& projectedProtectInterceptionsConsumed < state->battleHeroOrderProtectInterceptionLimit(protectSide)
						&& defenderStates.at(defender->unitId())->alive()
						? defender : requestedDefender);
				if(resolved && resolved->unitId() != requestedDefender->unitId())
				{
					strikeDefender = resolved;
					strikeDefenderUnits = &defenderUnits;
				}
				else
					strikeDefenderUnits = &requestedDefenderUnits;
			}
			const auto strikeDefenderState = defenderStates.find(strikeDefender->unitId());
			if(!ap.attackerState->alive() || strikeDefenderState == defenderStates.end()
				|| !strikeDefenderState->second->alive()
				|| (attackInfo.shooting && !ap.attackerState->canShoot()))
				break;
			if(ordinaryAttacker && attackInfo.physicalDamage && !attackInfo.shooting
				&& !attackInfo.retaliation && !attackInfo.bracePreemptive
				&& attackInfo.preemptiveDamagePercent <= 0)
			{
				const int preemptivePercent = bulwarkPreemptivePercent(
					strikeDefenderState->second.get(), *state);
				if(preemptivePercent > 0)
				{
					// Defend's first-melee reaction resolves before the incoming attack.
					// Mark only the detached target state; choosing or probing this
					// possibility never consumes the live stack's reaction.
					strikeDefenderState->second->bulwarkPreemptiveUsed = true;
					BattleAttackInfo preemptive(strikeDefenderState->second.get(), ap.attackerState.get(), 0, false);
					preemptive.retaliation = true;
					preemptive.preemptiveDamagePercent = preemptivePercent;
					preemptive.attackerPos = strikeDefenderState->second->getPosition();
					preemptive.defenderPos = ap.attackerState->getPosition();
					auto preemptiveDamage = luckState.battleExpectedLuckDamage(preemptive);
					vstd::amin(preemptiveDamage, ap.attackerState->getAvailableHealth());
					ap.attackerDamageReduce += calculateDamageReduce(strikeDefenderState->second.get(),
						ap.attackerState.get(), preemptiveDamage, damageCache, state);
					ap.attackerState->damage(preemptiveDamage);
				}
			}
			if(!ap.attackerState->alive())
				break;
			const int relentlessAssaultDamagePercent = ordinaryRelentlessAssaultAttack
				? luckState.battleGetRelentlessAssaultDamagePercent(
					ap.attackerState.get(), strikeDefenderState->second.get())
				: 0;

			FortuneStrikeProjection strike;
		strike.attackerId = ap.attackerState->unitId();
		strike.defenderId = strikeDefender->unitId();
		strike.shooting = attackInfo.shooting;
		strike.retaliation = attackInfo.retaliation;
		strike.perfectMoment = ap.perfectMoment && i == 0;
		strike.attackIndex = attackInfo.retaliation ? 0 : i;
		strike.protectIntercepted = projectsProtect
				&& strikeDefender->unitId() != requestedDefender->unitId();
			// The authoritative server consumes immediately after resolving the
			// redirected recipient, before reactions or damage. Mirror that timing
			// even if this projected blow later produces no damage events.
			if(strike.protectIntercepted)
			{
				if(fortunePreview)
				{
					auto order = fortunePreview->battleGetHeroOrderState(protectSide);
					if(order && order->command == HeroCommand::PROTECT
						&& order->secondaryTargetUnitId == requestedDefender->unitId()
						&& order->primaryTargetUnitId == strikeDefender->unitId()
						&& order->protectInterceptionsConsumed
							< fortunePreview->battleHeroOrderProtectInterceptionLimit(protectSide)
						&& !order->protectBroken)
					{
						++order->protectInterceptionsConsumed;
						fortunePreview->setHeroOrderState(protectSide, order);
					}
				}
				else if(protectOrder && !protectOrder->protectBroken
					&& projectedProtectInterceptionsConsumed
						< state->battleHeroOrderProtectInterceptionLimit(protectSide))
					++projectedProtectInterceptionsConsumed;
			}
			strike.relentlessAssaultEligible = ordinaryRelentlessAssaultAttack
				&& !strikeDefender->isGhost() && !strikeDefender->isTurret()
				&& !strikeDefender->hasBonusOfType(BonusType::SIEGE_WEAPON)
				&& strikeDefender->unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER
				&& state->battleMatchOwner(attacker, strikeDefender);
			std::optional<FortuneStrikeProjection> retaliation;
			std::optional<FortuneStrikeProjection> cleave;
			int64_t bulwarkPrimaryHealthLoss = 0;
			int bulwarkReflectionRate = 0;
			std::vector<std::pair<std::shared_ptr<battle::CUnitState>, int64_t>> pendingRetaliationDamage;
			std::vector<const battle::Unit *> destroyedEnemyUnits;

			for(auto u : *strikeDefenderUnits)
			{
				auto defenderState = defenderStates.at(u->unitId());
				if(!defenderState->alive())
					continue;

				int64_t damageDealt;
				float defenderDamageReduce;

				auto victimAttack = ap.attack;
				victimAttack.attacker = ap.attackerState.get();
				victimAttack.defender = defenderState.get();
				victimAttack.secondaryAttack = u->unitId() != strikeDefender->unitId();
				victimAttack.relentlessAssaultDamagePercent = relentlessAssaultDamagePercent;
				victimAttack.protectIntercepted = strike.protectIntercepted
					&& u->unitId() == strikeDefender->unitId();
				// The authoritative path spends movement on the first strike and
				// consumes Charge before collateral. Later attacks therefore have no
				// charge distance; collateral retains distance for ordinary jousting,
				// while secondaryAttack prevents it from inheriting the Order.
				if(i > 0)
					victimAttack.chargeDistance = 0;
				if(strike.perfectMoment)
				{
					victimAttack.luckyStrike = !victimAttack.secondaryAttack || state->getBattle()->getLuckRollRules().affectsAllTargets;
					victimAttack.unluckyStrike = false;
				}
				victimAttack.defenderPos = defenderState->getPosition();
				if(strike.perfectMoment)
				{
					// Non-lucky collateral of a forced positive strike is neutral,
					// not another ordinary (possibly negative) Luck roll.
					const auto range = state->calculateDmgRange(victimAttack).damage;
					damageDealt = range.min + (range.max - range.min) / 2;
				}
				else
					damageDealt = luckState.battleExpectedLuckDamage(victimAttack);
				vstd::amin(damageDealt, defenderState->getAvailableHealth());
				const auto * targetHero = state->battleGetOwnerHero(defenderState.get());
				if(damageDealt > 0 && victimAttack.physicalDamage && ordinaryAttacker
					&& defenderState->defended()
					&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(defenderState.get())
					&& newHorizonsBulwark::hasSwampRenewal(targetHero))
				{
					const auto currentDamage = defenderState->bulwarkDefendPhysicalDamage;
					defenderState->bulwarkDefendPhysicalDamage = currentDamage
						> std::numeric_limits<int64_t>::max() - damageDealt
						? std::numeric_limits<int64_t>::max() : currentDamage + damageDealt;
				}
				auto retaliatorState = defenderState->acquireState();
				int64_t projectedHit = damageDealt;
				retaliatorState->damage(projectedHit);

				// Later strikes must score casualties against the current copied health,
				// not repeat the first strike's original-victim bounty.
				defenderDamageReduce = calculateDamageReduce(ap.attackerState.get(), defenderState.get(),
					damageDealt, damageCache, state);

				const bool appliesNoQuarter = fortunePreview
					&& state->battleCanTriggerNoQuarter(victimAttack)
					&& !u->isTimeStopped()
					&& state->battleMatchOwner(ap.attackerState.get(), u)
					&& retaliatorState->alive()
					&& newHorizonsOffense::belowNoQuarterThreshold(
						retaliatorState->getAvailableHealth(), battle::getMaximumHealth(*retaliatorState));
				const int32_t moraleActivations = appliesNoQuarter
					? noQuarterMoraleActivations(*state, u->unitId()) : 0;

				const bool wasAlive = defenderState->alive();
				defenderState->damage(damageDealt);
				if(victimAttack.physicalDamage && ordinaryAttacker && defenderState->defended()
					&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(defenderState.get())
					&& newHorizonsBulwark::hasImmovable(targetHero)
					&& defenderState->bulwarkImmovableRound != bulwarkRound)
					defenderState->bulwarkImmovableRound = bulwarkRound;
				if(!ap.bulwarkMireGripTriggered && ordinaryAttacker
					&& !ap.attackerState->bulwarkMireGripApplied && damageDealt > 0
					&& victimAttack.physicalDamage && !attackInfo.shooting
					&& defenderState->defended()
					&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(defenderState.get())
					&& newHorizonsBulwark::hasMireGrip(targetHero))
					ap.bulwarkMireGripTriggered = true;
				if(u->unitId() == strikeDefender->unitId() && damageDealt > 0
					&& attackInfo.physicalDamage && ordinaryAttacker
					&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(ap.attackerState.get()))
				{
					bulwarkPrimaryHealthLoss = damageDealt;
					bulwarkReflectionRate = bulwarkReflectionBasisPoints(
						defenderState.get(), *state, attackInfo.shooting);
				}
				if(appliesNoQuarter && defenderState->alive())
				{
					fortunePreview->getForUpdate(u->unitId())->applyNoQuarter(moraleActivations);
					strike.noQuarterTargets.emplace_back(u->unitId(), moraleActivations);
				}

				if(i == 0 && !attackInfo.shooting && u->unitId() == strikeDefender->unitId()
					&& retaliatorState->alive() && retaliatorState->ableToRetaliate() && !counterAttacksBlocked
					&& (!state->battleShroudDeniesRetaliation(victimAttack) || defenderState->hasBonus(firstStrikeSelector))
					&& !ap.attackerState->isInvincible() && !state->isLongWeaponAttack(ap.attackerState.get(), defenderState.get()))
				{
					retaliation.emplace();
					retaliation->attackerId = retaliatorState->unitId();
					retaliation->defenderId = attacker->unitId();
					retaliation->retaliation = true;
					retaliation->attackIndex = 0;
					for(auto retaliated : retaliatedUnits)
					{
						if(retaliated->unitId() == attacker->unitId())
							pendingRetaliationDamage.emplace_back(ap.attackerState, 0);
						else
							pendingRetaliationDamage.emplace_back(defenderStates.at(retaliated->unitId()), 0);
					}
				}

				bool isEnemy = state->battleMatchOwner(attacker, u);

				// this includes enemy units as well as attacker units under enemy's mind control
				if(isEnemy)
					ap.defenderDamageReduce += defenderDamageReduce;

				// damaging attacker's units (even those under enemy's mind control) is considered friendly fire
				if(attackerSide == u->unitSide())
					ap.collateralDamageReduce += defenderDamageReduce;

				strike.hits.emplace_back(u->unitId(), damageDealt);
				if(damageDealt > 0 && attackInfo.physicalDamage
					&& newHorizonsArchery::isOrdinaryPhysicalShooter(ap.attackerState.get()))
				{
					const auto shooterSide = state->playerToSide(state->battleGetOwner(ap.attackerState.get()));
					defenderState->archeryRecordCrossfireDamage(shooterSide,
						ap.attackerState->unitId(), currentRound);
				}
				if(projectsRainOfArrows && u->unitId() == requestedDefender->unitId())
					projectedRainPrimaryDamage += damageDealt;
				const bool mayRebirth = !defenderState->isClone()
					&& defenderState->valOfBonuses(BonusType::REBIRTH) > 0
					&& defenderState->canCast() && defenderState->getPhantomInitialIntegrity() == 0;
				if(wasAlive && !defenderState->alive()
					&& !mayRebirth
					&& state->battleMatchOwner(ap.attackerState.get(), u))
					destroyedEnemyUnits.push_back(u);

				if(u->unitId() == strikeDefender->unitId())
				{
					ap.defenderDead = !defenderState->alive();
				}
			}
			// The server resolves every victim of a breath/splash strike before it
			// applies Bulwark's direct reflection. Deferring this hit also keeps the
			// attacker count stable while collateral damage is forecast.
			if(bulwarkPrimaryHealthLoss > 0 && bulwarkReflectionRate > 0
				&& ap.attackerState->alive())
			{
				const int64_t reflectedDamage = newHorizonsBulwark::reflectedDamage(
					bulwarkPrimaryHealthLoss, bulwarkReflectionRate);
				if(reflectedDamage > 0)
				{
					auto actualReflectedDamage = std::min(reflectedDamage,
						ap.attackerState->getAvailableHealth());
					auto * reflectedFrom = defenderStates.at(strikeDefender->unitId()).get();
					ap.attackerDamageReduce += calculateDamageReduce(reflectedFrom,
						ap.attackerState.get(), actualReflectedDamage, damageCache, state);
					ap.attackerState->damage(actualReflectedDamage);
					const auto * bulwarkHero = state->battleGetOwnerHero(reflectedFrom);
					if(!attackInfo.shooting && newHorizonsBulwark::hasToxicSpines(bulwarkHero)
						&& reflectedFrom->bulwarkToxicSpinesRound != currentRound
						&& actualReflectedDamage > 0 && ap.attackerState->alive())
					{
						reflectedFrom->bulwarkToxicSpinesRound = currentRound;
						const auto previousPoisonDamage = projectedPhysicalPoisonDamage(ap.attackerState.get());
						const auto poisonBase = newHorizonsBulwark::toxicSpinesPoisonBase(actualReflectedDamage);
						if(newHorizonsBulwark::applyPhysicalPoison(ap.attackerState.get(), poisonBase,
							static_cast<int32_t>(reflectedFrom->unitId())))
						{
							const auto projectedPoisonDamage = projectedPhysicalPoisonDamage(ap.attackerState.get());
							const auto residualPoisonDamage = std::max<int64_t>(0,
								projectedPoisonDamage - previousPoisonDamage);
							if(residualPoisonDamage > 0)
								ap.attackerDamageReduce += calculateDamageReduce(reflectedFrom,
									ap.attackerState.get(), residualPoisonDamage, damageCache, state);
						}
					}
				}
			}
			if(projectsSuppression && !projectedSuppressionSpent)
			{
				const auto primaryHit = std::ranges::find_if(strike.hits,
					[&strike](const auto & hit)
					{
						return hit.first == strike.defenderId && hit.second > 0;
					});
				const auto firstDamaged = primaryHit != strike.hits.end() ? primaryHit
					: std::ranges::find_if(strike.hits,
						[&strike](const auto & hit)
						{
							return hit.first != strike.defenderId && hit.second > 0;
						});
				if(firstDamaged != strike.hits.end())
				{
					projectedSuppressionSpent = true;
					ap.attackerState->archerySuppressionActivationSerial = currentActivationSerial;
					const auto targetState = defenderStates.find(firstDamaged->first);
					if(targetState != defenderStates.end() && targetState->second->alive())
					{
						const Bonus suppression(BonusDuration::STACK_GETS_TURN, BonusType::STACKS_SPEED,
							BonusSource::OTHER, -1, BonusSourceID());
						fortunePreview->addUnitBonus(firstDamaged->first, {suppression});
					}
				}
			}
			if(projectsDeadeye && ap.attackerState->archeryDeadeyeRound != currentRound)
				ap.attackerState->archeryDeadeyeRound = currentRound;
			if(projectsRainOfArrows && ap.attackerState->archeryRainOfArrowsActivationSerial != currentActivationSerial)
				ap.attackerState->archeryRainOfArrowsActivationSerial = currentActivationSerial;
			// The trigger runs after the attacker commits this blow's updated
			// resource state, before responses and follow-up attacks.
			ap.attackerState->afterAttack(attackInfo.shooting, false, attackInfo.physicalDamage);
			int64_t actualStrikeDamage = 0;
			for(const auto & hit : strike.hits)
				actualStrikeDamage += std::max<int64_t>(0, hit.second);
			projectVampirismHealing(ap.attackerState.get(), actualStrikeDamage,
				ap.vampirismHealingByUnit);
			if(projectsHexOfPain && fortunePreview && !strike.hits.empty())
			{
				const auto preHexState = ap.attackerState->acquireState();
				BattleAttackInfo projectedAttack(ap.attackerState.get(),
					defenderStates.at(strike.defenderId).get(), 0, strike.shooting);
				projectedAttack.retaliation = strike.retaliation;
				const auto painDamage = fortunePreview->projectHexOfPainStrike(
					projectedAttack, strike.hits, strike.attackIndex);
				scoreHexPain(preHexState.get(), painDamage);
			}
			// Counterfire is an immediate, once-per-round answer to physical creature
			// ranged damage. Include it in the exchange value so the AI does not price
			// a shot as if the marked shooter could not return fire.
			if(attackInfo.shooting && attackInfo.physicalDamage && !attackInfo.retaliation
				&& ap.attackerState->alive())
			{
				for(const auto & [hitUnitId, damageDealt] : strike.hits)
				{
					if(damageDealt <= 0)
						continue;
					auto stateIt = defenderStates.find(hitUnitId);
					if(stateIt == defenderStates.end())
						continue;
					auto counterShooter = stateIt->second;
					const auto * counterHero = state->battleGetFightingHero(counterShooter->unitSide());
					if(!newHorizonsArchery::canUseCounterfire(counterHero, counterShooter.get())
						|| counterShooter->archeryCounterfireRound == state->battleGetRound()
						|| !luckState.battleCanShoot(counterShooter.get(), ap.attackerState->getPosition()))
						continue;

					counterShooter->archeryCounterfireRound = state->battleGetRound();
					BattleAttackInfo counterfire(counterShooter.get(), ap.attackerState.get(), 0, true);
					counterfire.archeryRangedDamageMultiplierPercent = newHorizonsArchery::COUNTERFIRE_DAMAGE_PERCENT;
					int64_t counterfireDamage = luckState.battleExpectedLuckDamage(counterfire);
					if(newHorizonsArchery::hasDeadeye(counterHero)
						&& counterShooter->archeryDeadeyeRound != currentRound)
						counterShooter->archeryDeadeyeRound = currentRound;
					vstd::amin(counterfireDamage, ap.attackerState->getAvailableHealth());
					ap.attackerDamageReduce += calculateDamageReduce(counterShooter.get(), ap.attackerState.get(),
						counterfireDamage, damageCache, state);
					ap.attackerState->damage(counterfireDamage);
					projectVampirismHealing(counterShooter.get(), counterfireDamage,
						ap.vampirismHealingByUnit);
					if(counterfireDamage > 0 && attackInfo.physicalDamage
						&& newHorizonsArchery::isOrdinaryPhysicalShooter(counterShooter.get()))
					{
						const auto shooterSide = state->playerToSide(state->battleGetOwner(counterShooter.get()));
						ap.attackerState->archeryRecordCrossfireDamage(shooterSide,
							counterShooter->unitId(), currentRound);
					}
					counterShooter->afterAttack(true, false, true);
				}
			}
			if(fortunePreview && !strike.hits.empty())
			{
				if(strike.relentlessAssaultEligible)
					fortunePreview->recordRelentlessAssaultAttack(attackerSide, strikeDefender->unitId());
			}
			// Authority consumes the physical attack's once-per-activation effects
			// before the Cleave follow-up is resolved. In particular, a waited
			// Battlecraft bonus belongs to the triggering blow only.
			// Recovery is part of the triggering attack and resolves before an
			// automatic Cleave strike can select or damage its target.
			if(!attackInfo.shooting && !strike.hits.empty())
			{
				const auto side = state->playerToSide(state->battleGetOwner(attacker));
				if(side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
				{
					const auto fortune = state->getBattle()->getSylvanLuckState(side);
					const auto rules = state->getBattle()->getLuckRollRules();
					const int luck = luckState.battleGetAttackLuck(attacker, strikeDefender, false);
					const auto chanceIndex = luck > 0 && !rules.goodChance.empty()
						? std::min<size_t>(static_cast<size_t>(luck), rules.goodChance.size()) - 1
						: 0;
					const bool certainlyLucky = strike.perfectMoment || ap.attack.luckyStrike
						|| (luck > 0 && rules.diceSize > 0 && !rules.goodChance.empty()
							&& rules.goodChance[chanceIndex] >= rules.diceSize);
					if(fortune.luckyRecovery && certainlyLucky && ap.attackerState->alive())
					{
						int64_t actualDamage = 0;
						for(const auto & [unitId, damage] : strike.hits)
							if(unitId == strike.defenderId || rules.affectsAllTargets)
								actualDamage += std::max<int64_t>(0, damage);
						auto healing = SylvanLuckState::recoveryAmount(actualDamage);
						ap.attackerState->heal(healing, EHealLevel::HEAL, EHealPower::PERMANENT);
					}
				}
			}

			if(projectsCleave && ap.attackerState->alive() && ap.effectPreview
				&& ap.effectPreview->battleCanTriggerCleave(ap.attackerState.get())
				&& !destroyedEnemyUnits.empty())
			{
				const auto lowestOccupiedHex = [](const battle::Unit * unit)
				{
					int result = GameConstants::BFIELD_SIZE;
					for(const auto hex : unit->getHexes())
						result = std::min<int>(result, hex.toInt());
					return result;
				};
				std::sort(destroyedEnemyUnits.begin(), destroyedEnemyUnits.end(), [&](const battle::Unit * left,
					const battle::Unit * right)
				{
					const int leftHex = lowestOccupiedHex(left);
					const int rightHex = lowestOccupiedHex(right);
					return leftHex != rightHex ? leftHex < rightHex : left->unitId() < right->unitId();
				});
				destroyedEnemyUnits.erase(std::unique(destroyedEnemyUnits.begin(), destroyedEnemyUnits.end(),
					[](const battle::Unit * left, const battle::Unit * right)
					{
						return left->unitId() == right->unitId();
					}), destroyedEnemyUnits.end());

				for(const auto * destroyed : destroyedEnemyUnits)
				{
					const auto * projectedDestroyed = ap.effectPreview->battleGetUnitByID(destroyed->unitId());
					const auto * target = ap.effectPreview->battleSelectCleaveTarget(ap.attackerState.get(), projectedDestroyed);
					if(!target)
						continue;

					auto targetStateIt = defenderStates.find(target->unitId());
					std::shared_ptr<battle::CUnitState> targetState;
					if(targetStateIt != defenderStates.end())
						targetState = targetStateIt->second;
					else
					{
						targetState = ap.effectPreview->getForUpdate(target->unitId());
						defenderStates.emplace(target->unitId(), targetState);
						ap.affectedUnits.push_back(targetState);
					}

					ap.attackerState->cleaveUsedThisActivation = true;
					BattleAttackInfo cleaveAttack(ap.attackerState.get(), targetState.get(), 0, false);
					cleaveAttack.attackerPos = ap.attackerState->getPosition();
					cleaveAttack.defenderPos = targetState->getPosition();
					cleaveAttack.cleaveDamagePercent = newHorizonsOffense::CLEAVE_DAMAGE_PERCENT;
					int64_t cleaveDamage = luckState.battleExpectedLuckDamage(cleaveAttack);
					vstd::amin(cleaveDamage, targetState->getAvailableHealth());
					ap.defenderDamageReduce += calculateDamageReduce(ap.attackerState.get(), targetState.get(),
						cleaveDamage, damageCache, state);

					cleave.emplace();
					cleave->attackerId = ap.attackerState->unitId();
					cleave->defenderId = targetState->unitId();
					cleave->attackIndex = 0;
					cleave->cleaveDamagePercent = newHorizonsOffense::CLEAVE_DAMAGE_PERCENT;
					cleave->hits.emplace_back(targetState->unitId(), cleaveDamage);
					targetState->damage(cleaveDamage);
					if(state->battleCanTriggerNoQuarter(cleaveAttack) && !targetState->isTimeStopped()
						&& state->battleMatchOwner(ap.attackerState.get(), targetState.get())
						&& targetState->alive()
						&& newHorizonsOffense::belowNoQuarterThreshold(
							targetState->getAvailableHealth(), battle::getMaximumHealth(*targetState)))
					{
						const int32_t moraleActivations = noQuarterMoraleActivations(*state, targetState->unitId());
						fortunePreview->getForUpdate(targetState->unitId())->applyNoQuarter(moraleActivations);
						cleave->noQuarterTargets.emplace_back(targetState->unitId(), moraleActivations);
					}
					if(targetState->unitId() == defender->unitId())
						ap.defenderDead = !targetState->alive();
					ap.attackerState->afterAttack(false, false, true);
					projectVampirismHealing(ap.attackerState.get(), cleaveDamage,
						ap.vampirismHealingByUnit);
					if(projectsHexOfPain && fortunePreview)
					{
						const auto preHexState = ap.attackerState->acquireState();
						const auto painDamage = fortunePreview->projectHexOfPainStrike(
							cleaveAttack, cleave->hits, cleave->attackIndex);
						scoreHexPain(preHexState.get(), painDamage);
					}
					break;
				}
			}

			// The outer counterattack occurs only if its original defender survives
			// the primary attack's Cleave follow-up. Drop its damage, score and strike
			// metadata together when Cleave destroys that stack.
			if(retaliation)
			{
				if(!defenderStates.at(retaliation->attackerId)->alive() || !ap.attackerState->alive())
				{
					retaliation.reset();
					pendingRetaliationDamage.clear();
				}
				else
				{
					auto retaliatorState = defenderStates.at(retaliation->attackerId)->acquireState();
					retaliation->hits.clear();
					for(auto & [targetState, rawDamage] : pendingRetaliationDamage)
					{
						if(!targetState->alive())
						{
							rawDamage = 0;
							continue;
						}

						BattleAttackInfo retaliationAttack(retaliatorState.get(), targetState.get(), 0, false);
						retaliationAttack.retaliation = true;
						retaliationAttack.secondaryAttack = targetState->unitId() != attacker->unitId();
						retaliationAttack.attackerPos = ap.attack.defenderPos;
						retaliationAttack.defenderPos = retaliationAttack.secondaryAttack
							? targetState->getPosition() : ap.attack.attackerPos;
						rawDamage = state->battleExpectedLuckDamage(retaliationAttack);
						retaliation->hits.emplace_back(targetState->unitId(), rawDamage);

						const auto actualDamage = std::min(rawDamage, targetState->getAvailableHealth());
						const auto damageReduce = calculateDamageReduce(retaliatorState.get(), targetState.get(),
							actualDamage, damageCache, state);
						if(targetState->unitId() == attacker->unitId())
							ap.attackerDamageReduce += damageReduce;
						else if(retaliatorState->unitSide() == targetState->unitSide())
						{
							if(state->battleMatchOwner(attacker, defender))
								ap.defenderDamageReduce += damageReduce;
							if(attackerSide == defender->unitSide())
								ap.collateralDamageReduce += damageReduce;
						}
						else
							ap.collateralDamageReduce += damageReduce;
					}
				}
			}

			if(ap.effectPreview)
			{
				BattleAttackInfo projectedAttack(ap.attackerState.get(),
					defenderStates.at(defender->unitId()).get(), 0, true);
				ap.effectPreview->projectRangedMarkStrike(projectedAttack, strike.hits);
			}
			int64_t retaliationActualDamage = 0;
			std::vector<std::pair<uint32_t, int64_t>> retaliationActualHits;
			for(auto & [targetState, rawDamage] : pendingRetaliationDamage)
			{
				auto actualDamage = std::min(rawDamage, targetState->getAvailableHealth());
				targetState->damage(actualDamage);
				retaliationActualHits.emplace_back(targetState->unitId(), actualDamage);
				if(retaliation && fortunePreview && targetState->alive())
				{
					auto retaliatorState = defenderStates.at(retaliation->attackerId);
					BattleAttackInfo retaliationAttack(retaliatorState.get(), targetState.get(), 0, false);
					retaliationAttack.retaliation = true;
					retaliationAttack.secondaryAttack = targetState->unitId() != attacker->unitId();
					if(state->battleCanTriggerNoQuarter(retaliationAttack) && !targetState->isTimeStopped()
						&& state->battleMatchOwner(retaliatorState.get(), targetState.get())
						&& newHorizonsOffense::belowNoQuarterThreshold(
							targetState->getAvailableHealth(), battle::getMaximumHealth(*targetState)))
					{
						const int32_t moraleActivations = noQuarterMoraleActivations(*state, targetState->unitId());
						fortunePreview->getForUpdate(targetState->unitId())->applyNoQuarter(moraleActivations);
						retaliation->noQuarterTargets.emplace_back(targetState->unitId(), moraleActivations);
					}
				}
				if(retaliation && (targetState->unitId() == retaliation->defenderId
					|| state->getBattle()->getLuckRollRules().affectsAllTargets))
					retaliationActualDamage += actualDamage;
			}
			if(retaliation && !retaliation->hits.empty())
			{
				auto retaliatorState = defenderStates.at(retaliation->attackerId);
				retaliatorState->afterAttack(attackInfo.shooting, true, attackInfo.physicalDamage);
				int64_t actualRetaliationDamage = 0;
				for(const auto & hit : retaliationActualHits)
					actualRetaliationDamage += std::max<int64_t>(0, hit.second);
				projectVampirismHealing(retaliatorState.get(), actualRetaliationDamage,
					ap.vampirismHealingByUnit);
				if(projectsHexOfPain && fortunePreview)
				{
					const auto preHexState = retaliatorState->acquireState();
					BattleAttackInfo retaliationAttack(retaliatorState.get(), ap.attackerState.get(), 0, false);
					retaliationAttack.retaliation = true;
					const auto painDamage = fortunePreview->projectHexOfPainStrike(
						retaliationAttack, retaliationActualHits, retaliation->attackIndex);
					scoreHexPain(preHexState.get(), painDamage);
				}
			}
			if(retaliation && retaliationActualDamage > 0)
			{
				auto retaliatorState = defenderStates.at(retaliation->attackerId);
				const auto side = state->playerToSide(state->battleGetOwner(defender));
				if(side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
				{
					const auto fortune = state->getBattle()->getSylvanLuckState(side);
					const auto rules = state->getBattle()->getLuckRollRules();
					BattleAttackInfo retaliationAttack(retaliatorState.get(), ap.attackerState.get(), 0, false);
					retaliationAttack.retaliation = true;
					const int luck = state->battleGetAttackLuck(defender, attacker, false);
					const auto chanceIndex = luck > 0 && !rules.goodChance.empty()
						? std::min<size_t>(static_cast<size_t>(luck), rules.goodChance.size()) - 1
						: 0;
					const bool certainlyLucky = retaliationAttack.luckyStrike
						|| (luck > 0 && rules.diceSize > 0 && !rules.goodChance.empty()
							&& rules.goodChance[chanceIndex] >= rules.diceSize);
					if(fortune.luckyRecovery && certainlyLucky && retaliatorState->alive())
					{
						auto healing = SylvanLuckState::recoveryAmount(retaliationActualDamage);
						retaliatorState->heal(healing, EHealLevel::HEAL, EHealPower::PERMANENT);
					}
				}
			}

			if(!strike.hits.empty())
				ap.fortuneStrikes.push_back(std::move(strike));
			if(cleave && !cleave->hits.empty())
				ap.fortuneStrikes.push_back(std::move(*cleave));
			if(retaliation && !retaliation->hits.empty())
				ap.fortuneStrikes.push_back(std::move(*retaliation));
			if(ap.perfectMoment && i == 0 && fortunePreview && !ap.fortuneStrikes.empty())
			{
				// A guaranteed first trigger also ends Serendipity before the
				// second strike. Keep that deterministic history local to this
				// candidate; merely comparing candidates cannot spend a real use.
				auto fortune = fortunePreview->getSylvanLuckState(attackerSide);
				fortune.consumePerfectMoment();
				fortune.recordStrike(attacker->unitId(), true, false);
				fortunePreview->setSylvanLuckState(attackerSide, fortune);
			}
		}
		if(ap.bulwarkMireGripTriggered && ap.attackerState->alive() && fortunePreview)
		{
			ap.attackerState->bulwarkMireGripApplied = true;
			const auto bulwarkSkillId = SecondarySkill::decode(std::string(newHorizonsBulwark::SKILL_ID));
			const Bonus slow(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
				BonusSource::OTHER, -2, BonusSourceID(SecondarySkill(bulwarkSkillId)));
			fortunePreview->addUnitBonus(ap.attackerState->unitId(), {slow});
		}
		if(projectsRainOfArrows && projectedRainPrimaryDamage > 0 && fortunePreview)
		{
			const auto adjacentToPrimary = [&rainPrimaryFootprint](const battle::Unit * candidate)
			{
				for(const auto & candidateHex : candidate->getHexes())
				{
					if(!candidateHex.isValid())
						continue;
					for(const auto & primaryHex : rainPrimaryFootprint)
						if(primaryHex.isValid() && BattleHex::getDistance(candidateHex, primaryHex) == 1)
							return true;
				}
				return false;
			};
			const auto lowestOccupiedHex = [](const battle::Unit * unit)
			{
				int result = GameConstants::BFIELD_SIZE;
				for(const auto & occupied : unit->getHexes())
					if(occupied.isValid())
						result = std::min(result, static_cast<int>(occupied.toInt()));
				return result;
			};
			const battle::Unit * secondary = nullptr;
			for(const auto * candidate : fortunePreview->battleGetUnitsIf(
				[](const battle::Unit * unit) { return unit->alive(); }))
			{
				if(candidate->unitId() == requestedDefender->unitId()
					|| fortunePreview->battleMatchOwner(ap.attackerState.get(), candidate, true)
					|| !adjacentToPrimary(candidate))
					continue;
				if(!secondary || candidate->getAvailableHealth() > secondary->getAvailableHealth()
					|| (candidate->getAvailableHealth() == secondary->getAvailableHealth()
						&& std::pair{lowestOccupiedHex(candidate), candidate->unitId()}
							< std::pair{lowestOccupiedHex(secondary), secondary->unitId()}))
					secondary = candidate;
			}
			const int64_t proposedDamage = projectedRainPrimaryDamage
				* newHorizonsArchery::RAIN_OF_ARROWS_DAMAGE_PERCENT / 100;
			if(secondary && proposedDamage > 0)
			{
				auto secondaryState = fortunePreview->getForUpdate(secondary->unitId());
				int64_t actualDamage = std::min(proposedDamage, secondaryState->getAvailableHealth());
				ap.defenderDamageReduce += calculateDamageReduce(ap.attackerState.get(), secondaryState.get(),
					actualDamage, damageCache, state);
				secondaryState->damage(actualDamage);
				if(!vstd::contains_if(ap.affectedUnits, [secondaryState](const auto & affected)
					{ return affected->unitId() == secondaryState->unitId(); }))
					ap.affectedUnits.push_back(std::move(secondaryState));
			}
		}

#if BATTLE_TRACE_LEVEL>=2
		logAi->trace("BattleAI AP: %s -> %s at %d from %d, affects %d units: d:%lld a:%lld c:%lld s:%lld",
			attackInfo.attacker->unitType()->getJsonKey(),
			attackInfo.defender->unitType()->getJsonKey(),
			ap.dest.toInt(), ap.from.toInt(), (int)ap.affectedUnits.size(),
			ap.defenderDamageReduce, ap.attackerDamageReduce, ap.collateralDamageReduce, ap.shootersBlockedDmg);
#endif

		if(!bestAp.dest.isValid() || ap.attackValue() > bestAp.attackValue())
			bestAp = ap;
	}

	// check how much damage we gain from blocking enemy shooters on this hex
	bestAp.shootersBlockedDmg = evaluateBlockedShootersDmg(attackInfo, hex, damageCache, state);

#if BATTLE_TRACE_LEVEL>=1
	logAi->trace("BattleAI best AP: %s -> %s at %d from %d, affects %d units: d:%lld a:%lld c:%lld s:%lld",
		attackInfo.attacker->unitType()->getJsonKey(),
		attackInfo.defender->unitType()->getJsonKey(),
		bestAp.dest.toInt(), bestAp.from.toInt(), (int)bestAp.affectedUnits.size(),
		bestAp.defenderDamageReduce, bestAp.attackerDamageReduce, bestAp.collateralDamageReduce, bestAp.shootersBlockedDmg);
#endif

	//TODO other damage related to attack (eg. fire shield and other abilities)
	return bestAp;
}
