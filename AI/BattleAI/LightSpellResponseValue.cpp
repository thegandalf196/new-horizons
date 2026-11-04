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
struct DivineRetributionProtection
{
	int rawCap = 0;
	int retributionistPercent = 100;
	int roundsRemaining = 0;
	PlayerColor spellCasterOwner = PlayerColor::CANNOT_DETERMINE;
	int32_t payoutRatioBasisPoints = 3000;
};

DivineRetributionProtection divineRetributionProtection(const battle::Unit * unit, SpellID spell)
{
	DivineRetributionProtection result;
	if(!unit)
		return result;

	const auto bonuses = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell))
		.And(Selector::type()(BonusType::DIVINE_RETRIBUTION)));
	if(!bonuses)
		return result;

	for(const auto & bonus : *bonuses)
	{
		if(!bonus || !Bonus::NTurns(bonus.get()) || bonus->turnsRemain <= 0)
			continue;

		int retributionistPercent = 100;
		if(bonus->parameters)
		{
			try
			{
				const auto & parameters = bonus->parameters->toCustom<JsonNode>();
				const auto percent = parameters["retributionistPercent"];
				if(percent.isNumber())
					retributionistPercent = static_cast<int>(percent.Integer());
			}
			catch(const std::runtime_error &)
			{
				// Older or unrelated marker payloads keep the neutral 100% behavior.
			}
		}

		const DivineRetributionProtection current{
			std::max(0, bonus->val),
			std::clamp(retributionistPercent, 100, 120),
			std::clamp(static_cast<int>(bonus->turnsRemain), 0, 2), bonus->spellCasterOwner,
			newHorizonsDivineRetribution::payoutRatioBasisPoints(*bonus)};
		const int64_t currentExposure = static_cast<int64_t>(current.rawCap)
			* current.retributionistPercent * current.roundsRemaining;
		const int64_t resultExposure = static_cast<int64_t>(result.rawCap)
			* result.retributionistPercent * result.roundsRemaining;
		if(currentExposure > resultExposure)
			result = current;
	}
	return result;
}

int divineRetributionDamage(const DivineRetributionProtection & protection,
	long double actualDamage, int roundOffset)
{
	if(protection.roundsRemaining <= roundOffset || protection.rawCap <= 0
		|| !std::isfinite(actualDamage) || actualDamage <= 0.0L)
		return 0;

	// Keep the established fractional mean-damage boundary. The native integer
	// gameplay helper would round before this expectation; only the validated
	// stored coefficient changes here, never current offensive marks.
	const long double payoutRatio = protection.payoutRatioBasisPoints == 3000 ? 0.30L
		: static_cast<long double>(protection.payoutRatioBasisPoints) / 10000.0L;
	const auto cappedBaseDamage = std::min(static_cast<long double>(protection.rawCap),
		std::floor(actualDamage * payoutRatio));
	const auto finalDamage = std::floor(cappedBaseDamage
		* static_cast<long double>(protection.retributionistPercent) / 100.0L);
	if(finalDamage <= 0.0L)
		return 0;
	return static_cast<int>(std::min(finalDamage,
		static_cast<long double>(std::numeric_limits<int>::max())));
}

float divineRetributionDamageValue(const battle::Unit * target, uint64_t damage,
	DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & projectedBattle)
{
	if(!target || damage == 0 || !projectedBattle)
		return 0.0f;

	const auto retainedAttackValue = AttackPossibility::calculateDamageReduce(
		nullptr, target, damage, damageCache, projectedBattle);
	if(retainedAttackValue > 0.0f)
		return retainedAttackValue;

	const auto maxHealth = target->getMaxHealth();
	const auto * creature = target->unitType();
	const auto creatureValue = creature ? creature->getAIValue() : 0;
	if(maxHealth <= 0 || creatureValue <= 0)
		return std::max(0.0f, retainedAttackValue);

	const long double healthValue = static_cast<long double>(damage)
		* static_cast<long double>(creatureValue) / static_cast<long double>(maxHealth);
	if(!std::isfinite(healthValue) || healthValue <= 0.0L)
		return std::max(0.0f, retainedAttackValue);

	return static_cast<float>(std::min(healthValue,
		static_cast<long double>(std::numeric_limits<float>::max())));
}

/// Estimate the marginal delayed Holy response from visible ordinary creature
/// attackers. Siege weapons, towers, active creature casts and hidden hero spellbooks
/// are deliberately excluded. Each stack contributes its strongest legal
/// melee or ranged attack once per round, with a bounded second-round forecast.
float divineRetributionThreatValue(const battle::Unit * liveTarget,
	const battle::Unit * projectedTarget, BattleSide side,
	const CBattleInfoCallback & liveBattle,
	const std::shared_ptr<HypotheticBattle> & projectedBattle,
	SpellID spell, DamageCache & damageCache)
{
	if(!liveTarget || !projectedTarget || !liveTarget->alive() || !projectedTarget->alive()
		|| liveTarget->getAvailableHealth() <= 0 || !projectedBattle)
		return 0.0f;

	const auto before = divineRetributionProtection(liveTarget, spell);
	const auto after = divineRetributionProtection(projectedTarget, spell);
	if(after.roundsRemaining <= 0 || after.rawCap <= 0)
		return 0.0f;

	float totalValue = 0.0f;
	for(const auto * attacker : liveBattle.battleGetAllUnits(true))
	{
		if(!attacker || !attacker->alive() || !attacker->isValidTarget() || attacker->isGhost() || attacker->isTurret()
			|| attacker->hasBonusOfType(BonusType::SIEGE_WEAPON)
			|| attacker->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER
			|| attacker->unitSide() == side || !liveBattle.battleMatchOwner(attacker, liveTarget))
			continue;

		const auto * projectedAttacker = projectedBattle->battleGetUnitByID(attacker->unitId());
		if(!projectedAttacker || !projectedAttacker->alive())
			continue;

		bool canShootTarget = false;
		if(attacker->canShoot())
			for(const auto & targetHex : liveTarget->getHexes())
				if(liveBattle.battleCanShoot(attacker, targetHex))
				{
					canShootTarget = true;
					break;
				}

		bool canMeleeTarget = false;
		if(attacker->isMeleeAttacker())
		{
			canMeleeTarget = !liveBattle.meleeAttackHexes(attacker, liveTarget,
				attacker->getPosition()).empty();
			if(!canMeleeTarget)
				for(const auto & position : liveBattle.battleGetAvailableHexes(attacker, false))
					if(!liveBattle.meleeAttackHexes(attacker, liveTarget, position).empty())
					{
						canMeleeTarget = true;
						break;
					}
		}

		float attackerValue = 0.0f;
		for(const int roundOffset : {0, 1})
		{
			const float roundWeight = roundOffset == 0 ? 1.0f : 0.5f;
			int afterAttacks = roundOffset == 0
				? std::max(0, AttackPossibility::getAttackCount(*attacker, true, liveBattle))
				: std::max(1, attacker->getTotalAttacks(true));
			if(canShootTarget && roundOffset == 1)
				if(const auto * attackerState = dynamic_cast<const battle::CUnitState *>(attacker);
					attackerState && attackerState->shots.isLimited())
				{
					const int currentShots = std::max(0,
						AttackPossibility::getAttackCount(*attacker, true, liveBattle));
					afterAttacks = std::min(afterAttacks,
						std::max(0, attackerState->shots.available() - currentShots));
				}

			int64_t beforeResponse = 0;
			int64_t afterResponse = 0;
			for(const bool shooting : {false, true})
			{
				if((shooting && !canShootTarget) || (!shooting && !canMeleeTarget))
					continue;

				const BattleAttackInfo attack(attacker, liveTarget, 0, shooting);
				const auto estimate = liveBattle.battleEstimateDamage(attack);
				const auto perAttackDamage = static_cast<long double>(std::max<int64_t>(0,
					estimate.damage.min + estimate.damage.max)) / 2.0L;
				if(perAttackDamage <= 0.0L)
					continue;

				const int attackCount = shooting ? afterAttacks : (roundOffset == 0
					? std::max(0, AttackPossibility::getAttackCount(*attacker, false, liveBattle))
					: std::max(1, attacker->getTotalAttacks(false)));
				if(attackCount <= 0)
					continue;

				const auto actualDamage = std::min(
					static_cast<long double>(liveTarget->getAvailableHealth()),
					perAttackDamage * static_cast<long double>(attackCount));
				beforeResponse = std::max(beforeResponse,
					newHorizonsDivineRetribution::recipientDamage(liveBattle, attacker,
						divineRetributionDamage(before, actualDamage, roundOffset), before.spellCasterOwner));
				afterResponse = std::max(afterResponse,
					newHorizonsDivineRetribution::recipientDamage(*projectedBattle, projectedAttacker,
						divineRetributionDamage(after, actualDamage, roundOffset), after.spellCasterOwner));
			}

			if(afterResponse <= beforeResponse)
				continue;

			attackerValue += divineRetributionDamageValue(projectedAttacker,
				static_cast<uint64_t>(afterResponse - beforeResponse), damageCache, projectedBattle)
				* roundWeight;
		}
		totalValue += attackerValue;
	}

	return std::isfinite(totalValue) ? totalValue : 0.0f;
}
