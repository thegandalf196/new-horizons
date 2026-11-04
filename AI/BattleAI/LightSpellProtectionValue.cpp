/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "LightSpellProtectionValue.h"
#include "AttackPossibility.h"
#include "HypotheticBattle.h"
#include "BattleDamageValue.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsDivineRetribution.h"
#include <cmath>
using battleDamageValue::averageOrderDamage;
struct HolyArmorProtection
{
	int reductionPercent = 0;
	int rounds = 0;

	int exposurePercentRounds() const
	{
		return reductionPercent * rounds;
	}
};

HolyArmorProtection holyArmorProtection(const battle::Unit * unit, SpellID spell)
{
	HolyArmorProtection result;
	if(!unit)
		return result;

	const auto bonuses = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell))
		.And(Selector::type()(BonusType::SPELL_DAMAGE_REDUCTION)));
	if(!bonuses)
		return result;
	for(const auto & bonus : *bonuses)
	{
		if(!bonus || !Bonus::NTurns(bonus.get()) || bonus->turnsRemain <= 0)
			continue;

		const HolyArmorProtection current{
			std::clamp(bonus->val, 0, 100),
			std::clamp(static_cast<int>(bonus->turnsRemain), 0, 2)};
		if(current.exposurePercentRounds() > result.exposurePercentRounds())
			result = current;
	}
	return result;
}

struct GuardianSpiritShield
{
	int64_t hitPoints = 0;
	int roundsRemaining = 0;
};

GuardianSpiritShield guardianSpiritShield(const battle::Unit * unit)
{
	if(!unit)
		return {};

	const auto state = unit->acquireState();
	if(!state)
		return {};

	return {
		std::max<int64_t>(0, state->guardianSpiritHitPoints),
		std::clamp(state->guardianSpiritRoundsRemaining, 0, 2)};
}

/// Estimate physical creature damage that can reach this one target during the
/// shield's remaining exposure. Only visible enemy stacks and direct melee or
/// ranged attacks are considered; spell-like shots, siege weapons, turrets, and
/// hidden enemy-hero spellbooks are deliberately outside Guardian Spirit's scope.
float guardianSpiritPhysicalThreat(const battle::Unit * target,
	const CBattleInfoCallback & battle, int roundsRemaining)
{
	if(!target || !target->alive() || target->isGhost() || target->isTurret()
		|| roundsRemaining <= 0)
		return 0.0f;

	const int rounds = std::clamp(roundsRemaining, 0, 2);
	float damagePerRound = 0.0f;
	for(const auto * attacker : battle.battleGetAllUnits(false))
	{
		if(!attacker || !attacker->alive() || !attacker->isValidTarget()
			|| attacker->isGhost() || attacker->isTurret()
			|| attacker->hasBonusOfType(BonusType::SIEGE_WEAPON)
			|| attacker->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER
			|| !battle.battleMatchOwner(attacker, target))
			continue;

		bool canShootTarget = false;
		if(attacker->canShoot())
			for(const auto & targetHex : target->getHexes())
				if(battle.battleCanShoot(attacker, targetHex))
				{
					canShootTarget = true;
					break;
				}

		bool canMeleeTarget = false;
		if(attacker->isMeleeAttacker())
		{
			canMeleeTarget = !battle.meleeAttackHexes(attacker, target,
				attacker->getPosition()).empty();
			if(!canMeleeTarget)
				for(const auto & position : battle.battleGetAvailableHexes(attacker, false))
					if(!battle.meleeAttackHexes(attacker, target, position).empty())
					{
						canMeleeTarget = true;
						break;
					}
		}

		float bestAttackThreat = 0.0f;
		for(const bool shooting : {false, true})
		{
			if((shooting && !canShootTarget) || (!shooting && !canMeleeTarget))
				continue;

			const BattleAttackInfo attack(attacker, target, 0, shooting);
			if(!attack.physicalDamage)
				continue;

			const float perAttackDamage = averageOrderDamage(battle.battleEstimateDamage(attack));
			if(perAttackDamage <= 0.0f)
				continue;

			const int availableAttacks = std::max(0,
				AttackPossibility::getAttackCount(*attacker, shooting, battle));
			int attacksPerFutureActivation = std::max(1, attacker->getTotalAttacks(shooting));
			if(shooting)
				if(const auto * attackerState = dynamic_cast<const battle::CUnitState *>(attacker);
					attackerState && attackerState->shots.isLimited()
						&& attackerState->shots.available() <= availableAttacks)
					attacksPerFutureActivation = 0;
			const float attackEquivalents = static_cast<float>(availableAttacks)
				+ (rounds > 1 ? 0.5f * static_cast<float>(attacksPerFutureActivation) : 0.0f);
			bestAttackThreat = std::max(bestAttackThreat, perAttackDamage * attackEquivalents);
		}

		// A stack chooses either its shot or melee attack, not both. Keep its
		// strongest relevant mode once, then aggregate independent enemy stacks.
		damagePerRound += bestAttackThreat;
	}

	return damagePerRound;
}

float guardianSpiritMitigationValue(const battle::Unit * liveTarget,
	const battle::Unit * projectedTarget, DamageCache & damageCache,
	const std::shared_ptr<HypotheticBattle> & projectedBattle)
{
	if(!liveTarget || !projectedTarget || !liveTarget->alive() || !projectedTarget->alive()
		|| liveTarget->getAvailableHealth() <= 0 || !projectedBattle)
		return 0.0f;

	const auto before = guardianSpiritShield(liveTarget);
	const auto after = guardianSpiritShield(projectedTarget);
	const auto incomingBefore = guardianSpiritPhysicalThreat(projectedTarget, *projectedBattle,
		before.roundsRemaining);
	const auto incomingAfter = guardianSpiritPhysicalThreat(projectedTarget, *projectedBattle,
		after.roundsRemaining);
	const auto absorbedBefore = std::min(static_cast<long double>(before.hitPoints),
		static_cast<long double>(incomingBefore));
	const auto absorbedAfter = std::min(static_cast<long double>(after.hitPoints),
		static_cast<long double>(incomingAfter));
	const auto newlyAbsorbed = absorbedAfter - absorbedBefore;
	if(!std::isfinite(newlyAbsorbed) || newlyAbsorbed <= 0.0L)
		return 0.0f;

	const auto savedDamage = static_cast<uint64_t>(std::floor(newlyAbsorbed));
	if(savedDamage == 0)
		return 0.0f;

	const auto retainedAttackValue = AttackPossibility::calculateDamageReduce(
		nullptr, projectedTarget, savedDamage, damageCache, projectedBattle);
	if(retainedAttackValue > 0.0f)
		return retainedAttackValue;

	const auto maxHealth = liveTarget->getMaxHealth();
	const auto * creature = liveTarget->unitType();
	const auto creatureValue = creature ? creature->getAIValue() : 0;
	if(maxHealth <= 0 || creatureValue <= 0)
		return std::max(0.0f, retainedAttackValue);

	// A shield still preserves a stack that cannot currently attack. Bound its
	// fallback value by the target's ordinary creature-health value.
	const long double healthValue = static_cast<long double>(savedDamage)
		* static_cast<long double>(creatureValue) / static_cast<long double>(maxHealth);
	if(!std::isfinite(healthValue) || healthValue <= 0.0L)
		return std::max(0.0f, retainedAttackValue);

	return static_cast<float>(std::min(healthValue,
		static_cast<long double>(std::numeric_limits<float>::max())));
}

struct HeavenlyGaleProtection
{
	int reductionBasisPoints = 0;
	int roundsRemaining = 0;

	int exposureBasisPoints() const
	{
		return reductionBasisPoints * roundsRemaining;
	}
};

HeavenlyGaleProtection heavenlyGaleProtection(const battle::Unit * unit, SpellID spell)
{
	HeavenlyGaleProtection result;
	if(!unit)
		return result;

	const auto bonuses = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell))
		.And(Selector::type()(BonusType::HEAVENLY_GALE)));
	if(!bonuses)
		return result;

	for(const auto & bonus : *bonuses)
	{
		if(!bonus || !Bonus::NTurns(bonus.get()) || bonus->turnsRemain <= 0)
			continue;

		const HeavenlyGaleProtection current{
			std::clamp(bonus->val, 0, 8000),
			std::clamp(static_cast<int>(bonus->turnsRemain), 0, 2)};
		if(current.exposureBasisPoints() > result.exposureBasisPoints())
			result = current;
	}
	return result;
}

float heavenlyGaleAttackEquivalents(const battle::Unit * attacker,
	const CBattleInfoCallback & battle, int roundsRemaining)
{
	if(!attacker || roundsRemaining <= 0)
		return 0.0f;

	const int availableAttacks = std::max(0, AttackPossibility::getAttackCount(*attacker, true, battle));
	int attacksPerFutureActivation = std::max(1, attacker->getTotalAttacks(true));
	if(const auto * attackerState = dynamic_cast<const battle::CUnitState *>(attacker);
		attackerState && attackerState->shots.isLimited()
			&& attackerState->shots.available() <= availableAttacks)
		attacksPerFutureActivation = 0;

	return static_cast<float>(availableAttacks)
		+ (roundsRemaining > 1 ? 0.5f * static_cast<float>(attacksPerFutureActivation) : 0.0f);
}

float heavenlyGaleSavedDamageValue(const battle::Unit * target, uint64_t savedDamage,
	DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & projectedBattle)
{
	if(!target || savedDamage == 0 || !projectedBattle)
		return 0.0f;

	const auto retainedAttackValue = AttackPossibility::calculateDamageReduce(
		nullptr, target, savedDamage, damageCache, projectedBattle);
	if(retainedAttackValue > 0.0f)
		return retainedAttackValue;

	const auto maxHealth = target->getMaxHealth();
	const auto * creature = target->unitType();
	const auto creatureValue = creature ? creature->getAIValue() : 0;
	if(maxHealth <= 0 || creatureValue <= 0)
		return std::max(0.0f, retainedAttackValue);

	const long double healthValue = static_cast<long double>(savedDamage)
		* static_cast<long double>(creatureValue) / static_cast<long double>(maxHealth);
	if(!std::isfinite(healthValue) || healthValue <= 0.0L)
		return std::max(0.0f, retainedAttackValue);

	return static_cast<float>(std::min(healthValue,
		static_cast<long double>(std::numeric_limits<float>::max())));
}

/// Estimate the army-wide value from the actual projected Heavenly Gale marker.
/// Each visible ranged attacker contributes only its best threatened ally, so a
/// single shooter's same volley is not counted once for every stack. Siege
/// weapons and towers are included; spell-like projectiles are filtered through
/// BattleAttackInfo::physicalDamage. No opposing hero book is inspected.
float heavenlyGaleMitigationValue(BattleSide side,
	const CBattleInfoCallback & liveBattle, const std::shared_ptr<HypotheticBattle> & projectedBattle,
	SpellID spell, DamageCache & damageCache)
{
	if(!projectedBattle)
		return 0.0f;

	float totalValue = 0.0f;
	const auto visibleUnits = liveBattle.battleGetAllUnits(true);
	for(const auto * attacker : visibleUnits)
	{
		if(!attacker || !attacker->alive() || attacker->isGhost() || attacker->unitSide() == side
			|| !attacker->canShoot())
			continue;

		const auto * projectedAttacker = projectedBattle->battleGetUnitByID(attacker->unitId());
		if(!projectedAttacker || !projectedAttacker->alive())
			continue;

		float bestAttackerValue = 0.0f;
		for(const auto * liveTarget : liveBattle.battleGetAllUnits(false))
		{
			if(!liveTarget || !liveTarget->alive() || !liveTarget->isValidTarget(false)
				|| liveTarget->isGhost() || liveTarget->unitSide() != side)
				continue;

			const auto * projectedTarget = projectedBattle->battleGetUnitByID(liveTarget->unitId());
			if(!projectedTarget || !projectedTarget->alive())
				continue;

			const auto before = heavenlyGaleProtection(liveTarget, spell);
			const auto after = heavenlyGaleProtection(projectedTarget, spell);
			if(after.roundsRemaining <= 0 || after.reductionBasisPoints <= 0
				|| after.exposureBasisPoints() <= before.exposureBasisPoints())
				continue;

			bool canShootTarget = false;
			for(const auto & targetHex : liveTarget->getHexes())
				if(liveBattle.battleCanShoot(attacker, targetHex))
				{
					canShootTarget = true;
					break;
				}
			if(!canShootTarget)
				continue;

			const BattleAttackInfo beforeAttack(attacker, liveTarget, 0, true);
			if(!beforeAttack.physicalDamage)
				continue;

			const BattleAttackInfo afterAttack(projectedAttacker, projectedTarget, 0, true);
			if(!afterAttack.physicalDamage)
				continue;

			const auto beforePerAttackDamage = std::max(0.0f,
				averageOrderDamage(liveBattle.battleEstimateDamage(beforeAttack)));
			const auto afterPerAttackDamage = std::max(0.0f,
				averageOrderDamage(projectedBattle->battleEstimateDamage(afterAttack)));
			const long double beforeDamageScale = std::max<int>(1, 10000 - before.reductionBasisPoints);
			const long double afterDamageScale = std::max<int>(1, 10000 - after.reductionBasisPoints);
			const long double rawPerAttackDamage = std::max(
				static_cast<long double>(beforePerAttackDamage) * 10000.0L / beforeDamageScale,
				static_cast<long double>(afterPerAttackDamage) * 10000.0L / afterDamageScale);
			if(!std::isfinite(rawPerAttackDamage) || rawPerAttackDamage <= 0.0L)
				continue;

			const auto beforeExposure = static_cast<long double>(before.reductionBasisPoints)
				* heavenlyGaleAttackEquivalents(attacker, liveBattle, before.roundsRemaining);
			const auto afterExposure = static_cast<long double>(after.reductionBasisPoints)
				* heavenlyGaleAttackEquivalents(attacker, liveBattle, after.roundsRemaining);
			const auto targetHealth = std::max<int64_t>(0, liveTarget->getAvailableHealth());
			const auto beforePrevented = std::min(static_cast<long double>(targetHealth),
				rawPerAttackDamage * beforeExposure / 10000.0L);
			const auto afterPrevented = std::min(static_cast<long double>(targetHealth),
				rawPerAttackDamage * afterExposure / 10000.0L);
			const auto addedProtection = afterPrevented - beforePrevented;
			if(!std::isfinite(addedProtection) || addedProtection <= 0.0L)
				continue;

			const auto savedDamage = static_cast<uint64_t>(std::floor(addedProtection));
			const auto value = heavenlyGaleSavedDamageValue(projectedTarget, savedDamage,
				damageCache, projectedBattle);
			bestAttackerValue = std::max(bestAttackerValue, value);
		}
		totalValue += bestAttackerValue;
	}

	return std::isfinite(totalValue) ? totalValue : 0.0f;
}

float visibleCreatureSpellThreat(
	const std::vector<const battle::Unit *> & enemyUnits, bool magicalOnly)
{
	float totalThreat = 0.0f;
	for(const auto * enemy : enemyUnits)
	{
		if(!enemy || !enemy->canCast())
			continue;
		float bestSpellDamage = 0.0f;
		const auto spellcasters = enemy->getBonuses(Selector::type()(BonusType::SPELLCASTER));
		if(!spellcasters)
			continue;
		for(const auto & bonus : *spellcasters)
		{
			if(!bonus || bonus->parameters || !bonus->subtype.as<SpellID>().hasValue())
				continue;
			const auto * spell = bonus->subtype.as<SpellID>().toSpell();
			if(!spell || !spell->isCombat()
				|| (!spell->isOffensive() && !spell->isDamage())
				|| spell->isCreatureAbility()
				|| (magicalOnly && !spell->isMagical()))
				continue;
			const auto spellDamage = static_cast<float>(std::max<int64_t>(0, spell->calculateDamage(enemy)));
			bestSpellDamage = std::max(bestSpellDamage, spellDamage);
		}
		totalThreat += bestSpellDamage;
	}
	return totalThreat;
}

float holyArmorMitigationValue(BattleSide side,
	const battle::Unit * liveTarget, const battle::Unit * projectedTarget, SpellID spell,
	int64_t friendlyHealth, float visibleMagicalThreat, DamageCache & damageCache,
	std::shared_ptr<CBattleInfoCallback> projectedBattle)
{
	if(!liveTarget || !projectedTarget || !liveTarget->alive() || !projectedTarget->alive()
		|| liveTarget->unitSide() != side || visibleMagicalThreat <= 0.0f || friendlyHealth <= 0)
		return 0.0f;

	const auto targetHealth = liveTarget->getAvailableHealth();
	if(targetHealth <= 0)
		return 0.0f;

	const auto before = holyArmorProtection(liveTarget, spell);
	const auto after = holyArmorProtection(projectedTarget, spell);
	const int addedExposure = after.exposurePercentRounds() - before.exposurePercentRounds();
	if(addedExposure <= 0)
		return 0.0f;

	// Spread the visible creature-caster volley across allied health as a bounded
	// target-priority estimate. The detached cast supplies the actual Holy Armor
	// reduction and duration; the threat side deliberately uses one best spell
	// per visible caster and does not infer anything from an enemy hero's book.
	const long double targetShare = static_cast<long double>(targetHealth)
		/ static_cast<long double>(friendlyHealth);
	const long double estimatedSavedDamage = std::min(
		static_cast<long double>(targetHealth),
		static_cast<long double>(visibleMagicalThreat) * targetShare
			* static_cast<long double>(addedExposure) / 100.0L);
	if(!std::isfinite(estimatedSavedDamage) || estimatedSavedDamage <= 0.0L)
		return 0.0f;

	const auto savedDamage = static_cast<uint64_t>(std::floor(estimatedSavedDamage));
	if(savedDamage == 0)
		return 0.0f;

	const auto retainedAttackValue = AttackPossibility::calculateDamageReduce(
		nullptr, liveTarget, savedDamage, damageCache, std::move(projectedBattle));
	if(retainedAttackValue > 0.0f)
		return retainedAttackValue;
	const auto maxHealth = liveTarget->getMaxHealth();
	const auto creatureValue = liveTarget->unitType()->getAIValue();
	if(maxHealth <= 0 || creatureValue <= 0)
		return std::max(0.0f, retainedAttackValue);

	// The usual damage-reduction value reflects the attacks the saved stack can
	// still make. That can be zero for a distant or temporarily immobilized unit,
	// even though preserving its health is useful. Fall back to a bounded share
	// of the stack's creature value so defensive spells are not treated as no-ops.
	const long double healthValue = static_cast<long double>(savedDamage)
		* static_cast<long double>(creatureValue) / static_cast<long double>(maxHealth);
	if(!std::isfinite(healthValue) || healthValue <= 0.0L)
		return std::max(0.0f, retainedAttackValue);

	const auto boundedHealthValue = static_cast<float>(std::min(
		healthValue, static_cast<long double>(std::numeric_limits<float>::max())));
	return boundedHealthValue;
}
