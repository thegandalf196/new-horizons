/*
 * DamageCache.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/battle/NewHorizonsPrismaticVanguard.h"
#include "SylvanTacticsProjection.h"
#include "DreadCavalryProjection.h"
#include "AttackPossibility.h"
#include "DamageCacheAdmission.h"
#include "../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "DamageCacheTimingScope.h"
#include "DamageCacheMarks.h"
#include "AttackCount.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/battle/NewHorizonsBattlecraft.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/NewHorizonsShroud.h"
#include "../../lib/battle/NewHorizonsShieldAndSword.h"
#include "../../lib/battle/NewHorizonsInfernoStrikeDebuffs.h"
#include "../../lib/battle/NewHorizonsCoordinatedMarks.h"
#include "../../lib/battle/NewHorizonsChargeSpecialties.h"
#include "../../lib/battle/NewHorizonsReactiveDefense.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/ObstacleCasterProxy.h"
#include "../../lib/spells/NewHorizonsSorcery.h"
#include <vcmi/spells/Service.h>
#include <vcmi/spells/Spell.h>

namespace damageCacheDetail
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

bool hasNightProwlerBonus(const battle::Unit * unit)
{
	return unit && unit->hasBonus(CSelector(newHorizonsShroud::isNightProwlerBonus));
}

}
namespace
{
bool hasTemporaryCombatModifier(const battle::Unit * unit)
{
	return unit && (newHorizonsCoordinatedMarks::hasMark(*unit) || unit->hasBonus(CSelector([](const Bonus * bonus)
	{
		return newHorizonsPrismaticVanguard::isEffect(bonus)
			|| newHorizonsShroud::isEvasiveShroudProtection(bonus)
			|| newHorizonsShieldAndSword::isProtection(bonus)
			|| newHorizonsInfernoStrikeDebuffs::isEffect(bonus)
			|| newHorizonsChargeSpecialties::isLanceFormation(bonus)
			|| newHorizonsReactiveDefense::isSolidarityProtection(bonus)
			|| newHorizonsReactiveDefense::isIronHideSpent(bonus);
	})));
}
bool hasShadowAssaultSpentMarker(const battle::Unit * unit, BattleSide attackingSide)
{
	return unit && (attackingSide == BattleSide::ATTACKER || attackingSide == BattleSide::DEFENDER)
		&& unit->hasBonus(CSelector([attackingSide](const Bonus * bonus)
			{ return newHorizonsShroud::isShadowAssaultSpentMarker(bonus, attackingSide); }));
}

}
using damageCacheDiagnostics::DamageCacheOperationScope;
using damageCacheDiagnostics::DamageCachePhaseScope;
using damageCacheDetail::hasRangedMarkEffect;
using damageCacheDetail::hasNightProwlerBonus;
using attackCount::hasPendingRangedFollowUp;

void DamageCache::rememberTemporaryCombatModifiers(const battle::Unit * attacker, const battle::Unit * defender)
{
	for(const auto * unit : {attacker, defender})
		if(hasTemporaryCombatModifier(unit))
			temporaryCombatModifierUnits.insert(unit->unitId());
}

void DamageCache::cacheDamage(const battle::Unit * attacker, const battle::Unit * defender, std::shared_ptr<CBattleInfoCallback> hb)
{
	cacheDamageMeasured(attacker, defender, std::move(hb), nullptr);
}

void DamageCache::cacheDamageMeasured(const battle::Unit * attacker, const battle::Unit * defender,
	std::shared_ptr<CBattleInfoCallback> hb, DamageCacheProbeTiming * timing)
{
	DamageCachePhaseScope preflight(timing, &DamageCacheProbeTiming::preflight);
	if(defender->intrinsicMagicalDamageReductionPercent() > 0)
		intrinsicMdrTargets.insert(defender->unitId());
	if(defender->intrinsicPhysicalDamageReductionPercent() > 0)
		intrinsicPdrTargets.insert(defender->unitId());
	rememberTemporaryCombatModifiers(attacker, defender);
	if(newHorizonsReactiveDefense::ironHideConfiguredPercent(*hb, *defender) > 0)
		temporaryCombatModifierUnits.insert(defender->unitId());
	const auto attackerSide = hb->playerToSide(hb->battleGetOwner(attacker));
	const auto * attackerHero = attackerSide == BattleSide::ATTACKER || attackerSide == BattleSide::DEFENDER
		? hb->battleGetFightingHero(attackerSide) : nullptr;
	if(newHorizonsShroud::hasAmbusher(attackerHero))
		ambusherAttackers.insert(attacker->unitId());
	if((attackerSide == BattleSide::ATTACKER || attackerSide == BattleSide::DEFENDER)
		&& (newHorizonsShroud::hasShadowAssault(attackerHero)
			|| hasShadowAssaultSpentMarker(defender, attackerSide)))
		shadowAssaultTargetSides.emplace(defender->unitId(), attackerSide);
	if(newHorizonsShroud::hasNightProwler(attackerHero) || hasNightProwlerBonus(attacker))
		nightProwlerAttackers.insert(attacker->unitId());

	// A continuation allowance is per-action state, not part of the ID-keyed
	// baseline damage cache. Its shared callback forecast is intentionally
	// recomputed for the independently selected follow-up target.
	if(hasPendingRangedFollowUp(attacker) || dreadCavalryAI::hasActivationState(attacker))
		return;

	if(hasRangedMarkEffect(defender, newHorizonsSorcery::ARCANE_BREACH_EFFECT))
		rangedMarkTargets.insert(defender->unitId());
	const BattleAttackInfo attack(attacker, defender, 0, hb->battleCanShoot(attacker, defender->getPosition()));
	preflight.finish();
	const auto damage = [&]()
	{
		DamageCachePhaseScope dynamic(timing, &DamageCacheProbeTiming::dynamicDamage);
		return hb->battleExpectedLuckDamage(attack);
	}();

	damageCache[attacker->unitId()][defender->unitId()] = static_cast<float>(damage) / attacker->getCount();
}

void DamageCache::buildObstacleDamageCache(std::shared_ptr<HypotheticBattle> hb, BattleSide side)
{
	// The valued army side does not grant its obstacle knowledge to this observer.
	const auto observerSide = hb->battleGetMySide();
	const auto obstaclePerspective = observerSide == BattleSide::ALL_KNOWING ? side : observerSide;

	for(const auto & obst : hb->battleGetAllObstacles(obstaclePerspective))
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

bool DamageCache::tracksRangedMarks(uint32_t defenderId) const
{
	for(const auto * cache = this; cache; cache = cache->parent)
		if(cache->rangedMarkTargets.contains(defenderId))
			return true;
	return false;
}

bool DamageCache::tracksTemporaryCombatModifier(uint32_t defenderId) const
{
	for(const auto * cache = this; cache; cache = cache->parent)
		if(cache->temporaryCombatModifierUnits.contains(defenderId))
			return true;
	return false;
}

bool DamageCache::tracksAmbusher(uint32_t attackerId) const
{
	for(const auto * cache = this; cache; cache = cache->parent)
		if(cache->ambusherAttackers.contains(attackerId))
			return true;
	return false;
}

bool DamageCache::tracksShadowAssault(uint32_t defenderId) const
{
	for(const auto * cache = this; cache; cache = cache->parent)
		for(const auto & targetAndSide : cache->shadowAssaultTargetSides)
			if(targetAndSide.first == defenderId)
				return true;
	return false;
}

bool DamageCache::tracksNightProwler(uint32_t attackerId) const
{
	for(const auto * cache = this; cache; cache = cache->parent)
		if(cache->nightProwlerAttackers.contains(attackerId))
			return true;
	return false;
}

bool DamageCache::tracksFrozen(uint32_t defenderId) const
{
	for(const auto * cache = this; cache; cache = cache->parent)
		if(cache->frozenTargets.contains(defenderId))
			return true;
	return false;
}

int64_t DamageCache::getDamage(const battle::Unit * attacker, const battle::Unit * defender, std::shared_ptr<CBattleInfoCallback> hb)
{
	return getDamage(attacker, defender, std::move(hb), nullptr);
}

int64_t DamageCache::getDamage(const battle::Unit * attacker, const battle::Unit * defender,
	std::shared_ptr<CBattleInfoCallback> hb, DamageCacheProbeTiming * receiver,
	DamageCacheAdmission * mainProbeAdmission)
{
	DamageCacheOperationScope operation(receiver);
	auto * timing = operation.receiver();
	if(timing)
		++timing->getDamageCalls;
	DamageCachePhaseScope preflight(timing, &DamageCacheProbeTiming::preflight);
	if(newHorizonsFrozen::isFrozen(*defender))
		frozenTargets.insert(defender->unitId());
	if(defender->intrinsicMagicalDamageReductionPercent() > 0)
		intrinsicMdrTargets.insert(defender->unitId());
	if(defender->intrinsicPhysicalDamageReductionPercent() > 0)
		intrinsicPdrTargets.insert(defender->unitId());
	if(hasRangedMarkEffect(defender, newHorizonsSorcery::ARCANE_BREACH_EFFECT))
		rangedMarkTargets.insert(defender->unitId());
	rememberTemporaryCombatModifiers(attacker, defender);
	if(newHorizonsReactiveDefense::ironHideConfiguredPercent(*hb, *defender) > 0)
		temporaryCombatModifierUnits.insert(defender->unitId());
	DamageCacheAdmission localAdmission(*attacker, *hb);
	auto & admission = mainProbeAdmission && mainProbeAdmission->matches(*attacker, *hb)
		? *mainProbeAdmission : localAdmission;
	const auto raSide = admission.side();
	const bool hasAmbusher = admission.hasAmbusher();
	if(hasAmbusher)
		ambusherAttackers.insert(attacker->unitId());
	const bool hasShadowAssault = admission.hasShadowAssault();
	if((raSide == BattleSide::ATTACKER || raSide == BattleSide::DEFENDER)
		&& (hasShadowAssault || hasShadowAssaultSpentMarker(defender, raSide)))
		shadowAssaultTargetSides.emplace(defender->unitId(), raSide);
	const bool hasNightProwler = admission.hasNightProwler();
	const bool hasNightProwlerEffect = admission.hasNightProwlerEffect();
	if(hasNightProwler || hasNightProwlerEffect)
		nightProwlerAttackers.insert(attacker->unitId());
	const auto * defenderHero = hb->battleGetOwnerHero(defender);
	const bool hasBastion = defenderHero && defenderHero->hasActivePerk(
		std::string(newHorizonsCombatSkills::ARMORER_SKILL_ID),
		std::string(newHorizonsCombatSkills::BASTION_PERK_ID));
	const bool hasRelentlessAssault = admission.hasRelentlessAssault();
	const bool shooting = hb->battleCanShoot(attacker, defender->getPosition());
	BattleAttackInfo attack(attacker, defender, 0, shooting);
	const auto fortune = raSide == BattleSide::ATTACKER || raSide == BattleSide::DEFENDER
		? hb->getBattle()->getSylvanLuckState(raSide) : SylvanLuckState{};
	const bool hasSecondChance = fortune.secondChance
		&& newHorizonsCombatSkills::isPhysicalCreatureLuckAttack(attacker, attack.physicalDamage);
	const bool hasGamblerPenalty = admission.hasGamblerPenalty();
	const bool hasRangedFollowUp = shooting && hasPendingRangedFollowUp(attacker);
	const bool hasReducedExtraActivation = admission.hasReducedExtraActivation();
	const bool hasBloodragePain = admission.hasBloodragePain();
	// IDs alone cannot key a target/controller/round-sensitive premium. Preserve
	// original-damage snapshots for comparison, but recompute current v2 damage.
	// Remember marked targets so expiry/Dispel cannot revive a cached premium.
	// Relentless Assault depends on the hero-side streak and target, while Chain
	// of Fortune can change side Luck after a strike. Never let an ID-only cache
	// reuse damage across either projected state. The Chain bypass remains active
	// after its once-per-round trigger because a pending gift can still be spent.
	// Bloodrage Pain can change damage after a projected health-threshold crossing.
	// Temporary protection, attack reduction and Defense modifiers are visible
	// unit effects. Remember either participant while a marker is present so
	// expiry or hidden hero policy cannot revive a stale ID-only pair entry.
	// Boneward is controller/form-sensitive environment state, not a bonus-tree node.
	// Remember its prior presence so removing it cannot revive a reduced pair entry.
	if(sylvanTacticsAI::dynamicDamage(*hb, attacker, defender)
		|| newHorizonsHeroes::ordinaryCreatureDamageIsStateful(*hb)
		|| tracksIntrinsicReduction(defender->unitId(), attack.physicalDamage)
		|| admission.supportsFocusFire()
		|| newHorizonsBattlecraft::rank(hb->battleGetOwnerHero(attacker)) > 0
		|| hasRangedFollowUp
		|| dreadCavalryAI::hasActivationState(attacker)
		|| hasReducedExtraActivation
		|| hasRelentlessAssault
		|| hasBastion
		|| hasSecondChance
		|| fortune.gambler
		|| fortune.chainOfFortune
		|| hb->getBattle()->getPerfectFortuneState(BattleSide::ATTACKER).enabled
		|| hb->getBattle()->getPerfectFortuneState(BattleSide::DEFENDER).enabled
		|| hasGamblerPenalty
		|| hasBloodragePain
		|| hasAmbusher
		|| hasShadowAssault
		|| tracksShadowAssault(defender->unitId())
		|| hasNightProwler
		|| hasNightProwlerEffect
		|| tracksNightProwler(attacker->unitId())
		|| tracksAmbusher(attacker->unitId())
		|| tracksTemporaryCombatModifier(attacker->unitId())
		|| tracksTemporaryCombatModifier(defender->unitId())
		|| tracksRangedMarks(defender->unitId())
		|| newHorizonsFrozen::isFrozen(*attacker)
		|| tracksFrozen(defender->unitId()))
	{
		if(timing)
			++timing->dynamicBypasses;
		if(!attacker->alive())
		{
			if(timing)
				++timing->deadReturns;
			return 0;
		}
		const auto * primaryTarget = hb->battleResolveHeroOrderTarget(attacker, defender, shooting);
		attack.defender = primaryTarget ? primaryTarget : defender;
		attack.protectIntercepted = attack.defender->unitId() != defender->unitId();
		if(hasRelentlessAssault)
		{
			attack.relentlessAssaultDamagePercent = hb->battleGetRelentlessAssaultDamagePercent(
				attacker, attack.defender);
		}
		preflight.finish();
		DamageCachePhaseScope dynamic(timing, &DamageCacheProbeTiming::dynamicDamage);
		return hb->battleExpectedLuckDamage(attack);
	}
	bool wasComputedBefore = damageCache[attacker->unitId()].count(defender->unitId());

	if (!wasComputedBefore)
	{
		if(timing)
			++timing->cacheFillCalls;
		preflight.finish();
		cacheDamageMeasured(attacker, defender, hb, timing);
	}
	else if(timing)
	{
		if(parent)
			++timing->localChildCacheHits;
		else
			++timing->rootCacheHits;
	}

	return damageCache[attacker->unitId()][defender->unitId()] * attacker->getCount();
}

int64_t DamageCache::getObstacleDamage(const BattleHex & hex, const battle::Unit * defender) const
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
	return getOriginalDamage(attacker, defender, std::move(hb), nullptr);
}

int64_t DamageCache::getOriginalDamage(const battle::Unit * attacker, const battle::Unit * defender,
	std::shared_ptr<CBattleInfoCallback> hb, DamageCacheProbeTiming * receiver)
{
	DamageCacheOperationScope operation(receiver);
	auto * timing = operation.receiver();
	if(timing)
		++timing->getOriginalDamageCalls;
	DamageCachePhaseScope preflight(timing, &DamageCacheProbeTiming::preflight);
	if(parent)
	{
		auto attackerDamageMap = parent->damageCache.find(attacker->unitId());

		if(attackerDamageMap != parent->damageCache.end())
		{
			auto targetDamage = attackerDamageMap->second.find(defender->unitId());

			if(targetDamage != attackerDamageMap->second.end())
			{
				if(timing)
					++timing->parentOriginalHits;
				return static_cast<int64_t>(targetDamage->second * attacker->getCount());
			}
		}
	}

	if(timing)
	{
		if(parent)
			++timing->parentOriginalFallbacks;
		else
			++timing->rootOriginalFallbacks;
	}
	preflight.finish();
	return getDamage(attacker, defender, hb, timing);
}

bool DamageCache::tracksIntrinsicReduction(uint32_t defenderId, bool physical) const
{
	for(const auto * cache = this; cache; cache = cache->parent)
		if((physical ? cache->intrinsicPdrTargets : cache->intrinsicMdrTargets).contains(defenderId))
			return true;
	return false;
}
