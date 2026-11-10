/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CanonicalOrderValue.h"
#include "AttackPossibility.h"
#include "BattleDamageValue.h"
#include "HypotheticBattle.h"
#include "HeroOrderCandidateRules.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/battle/HeroCommandRevision.h"
#include "../../lib/spells/NewHorizonsMagic.h"

namespace heroOrderValue
{
namespace
{
using heroOrderCandidates::isEligibleOrderUnit;
using battleDamageValue::averageOrderDamage;
// Finite stat estimates only: no action, movement, queue or exchange search.
class RevisedOrderEstimate
{
	std::shared_ptr<HypotheticBattle> before;
	std::shared_ptr<HypotheticBattle> after;
	std::shared_ptr<HypotheticBattle> reference;
	DamageCache referenceDamage;
	BattleSide side;

	static void materializeUnits(HypotheticBattle & view)
	{
		// Const retaliation estimates latch state; own every reachable unit.
		const auto units = view.battleGetAllUnits(false);
		for(const auto * unit : units) view.getForUpdate(unit->unitId());
	}

	float damageValue(const battle::Unit * attacker, const battle::Unit * victim, float damage)
	{
		return AttackPossibility::calculateDamageReduce(
			reference->battleGetUnitByID(attacker->unitId()), reference->battleGetUnitByID(victim->unitId()),
			static_cast<uint64_t>(std::max(0.0f, damage)), referenceDamage, reference);
	}

	float damage(const CBattleInfoCallback & view, uint32_t attacker, uint32_t victim) const
	{
		const auto * a = view.battleGetUnitByID(attacker);
		const auto * d = view.battleGetUnitByID(victim);
		return averageOrderDamage(view.battleEstimateDamage(BattleAttackInfo(a, d, 0,
			view.battleCanShoot(a, d->getPosition()))));
	}

	float morale(const CBattleInfoCallback & view, const battle::Unit * unit) const
	{
		if(unit->unaffectedByMorale()) return 0.0f;
		const int value = view.battleGetMorale(unit);
		if(value == 0) return 0.0f;
		const auto & rules = view.getBattle()->getMagicRules();
		const auto chance = newHorizonsMagic::moraleChance(rules, value);
		const auto settings = LIBRARY->engineSettings();
		const auto curve = settings->getVector(value > 0
			? EGameSettings::COMBAT_GOOD_MORALE_CHANCE : EGameSettings::COMBAT_BAD_MORALE_CHANCE);
		if(!chance && curve.empty()) return 0.0f;
		const int threshold = chance.value_or(curve.empty() ? 0
			: curve[std::min<size_t>(curve.size(), std::abs(value)) - 1]);
		const int dice = newHorizonsMagic::moraleDiceSize(rules)
			.value_or(settings->getInteger(EGameSettings::COMBAT_MORALE_DICE_SIZE));
		return dice > 0 ? (value > 0 ? 1.0f : -1.0f)
			* std::clamp(static_cast<float>(threshold) / dice, 0.0f, 1.0f) : 0.0f;
	}

	float vanguard(const CBattleInfoCallback & view, const battle::Unit * unit)
	{
		const auto grant = heroCommands::activeVanguardBonuses(unit);
		const int rounds = grant ? grant->roundsRemaining : 0;
		if(rounds <= 0) return 0.0f;
		const auto * neutral = reference->battleGetUnitByID(unit->unitId());
		float output = 0.0f, outputGain = 0.0f, protection = 0.0f;
		for(const auto * enemy : reference->battleGetAllUnits(false))
		{
			if(!isEligibleOrderUnit(*reference, CBattleInfoEssentials::otherSide(side), enemy)) continue;
			const auto baseDamage = damage(*reference, unit->unitId(), enemy->unitId());
			const auto newDamage = damage(view, unit->unitId(), enemy->unitId());
			const float baseValue = damageValue(neutral, enemy, baseDamage);
			output = std::max(output, damageValue(neutral, enemy, newDamage));
			outputGain = std::max(outputGain, damageValue(neutral, enemy, newDamage) - baseValue);
			protection = std::max(protection,
				damageValue(enemy, neutral, damage(*reference, enemy->unitId(), unit->unitId()))
				- damageValue(enemy, neutral, damage(view, enemy->unitId(), unit->unitId())));
		}
		// Preserve the existing half-activation-per-round duration horizon.
		const float activations = 0.5f * std::max(0, rounds - (unit->willMove(0) ? 0 : 1));
		const float initiative = static_cast<float>(unit->getInitiative() - neutral->getInitiative())
			/ std::max(1, neutral->getInitiative()) * 0.01f;
		const float moraleDelta = morale(view, unit) - morale(*reference, neutral);
		return activations * (outputGain + output * (initiative + moraleDelta)) + 0.5f * rounds * protection;
	}

public:
	RevisedOrderEstimate(std::shared_ptr<CBattleInfoCallback> source,
		std::shared_ptr<HypotheticBattle> projected, BattleSide side)
		: before(std::make_shared<HypotheticBattle>(projected->env, source)), after(std::move(projected)), side(side)
	{
		materializeUnits(*this->before);
		materializeUnits(*this->after);
	}

	float feint()
	{
		reference = before;
		float value = 0.0f;
		for(const auto * own : before->battleGetAllUnits(false))
		{
			if(!isEligibleOrderUnit(*before, side, own) || !own->willMove(0) || !own->canMove() || !own->isMeleeAttacker()) continue;
			float best = 0.0f;
			for(const auto * enemy : before->battleGetAllUnits(false))
			{
				if(!isEligibleOrderUnit(*before, CBattleInfoEssentials::otherSide(side), enemy)) continue;
				DamageEstimation oldCounter, newCounter;
				before->battleEstimateDamage(BattleAttackInfo(own, enemy, 0, false), &oldCounter);
				after->battleEstimateDamage(BattleAttackInfo(after->battleGetUnitByID(own->unitId()),
					after->battleGetUnitByID(enemy->unitId()), 0, false), &newCounter);
				best = std::max(best, damageValue(enemy, own, averageOrderDamage(oldCounter))
					- damageValue(enemy, own, averageOrderDamage(newCounter)));
			}
			value += best;
		}
		return value;
	}

	float vanguard()
	{
		reference = std::make_shared<HypotheticBattle>(after->env, before);
		materializeUnits(*reference);
		for(const auto * unit : before->battleGetAllUnits(false))
			if(isEligibleOrderUnit(*before, side, unit))
				reference->getForUpdate(unit->unitId())->removeUnitBonus(CSelector(heroCommands::isVanguardBonus));
		float value = 0.0f;
		for(const auto * old : before->battleGetAllUnits(false))
		{
			if(!isEligibleOrderUnit(*before, side, old) || !heroCommands::isVanguardCreature(old)) continue;
			const auto * projected = after->battleGetUnitByID(old->unitId());
			value += vanguard(*after, projected) - vanguard(*before, old);
		}
		return value;
	}
};
}

bool usesRevisedArmyHeuristic(const CBattleInfoCallback & battle, HeroCommand command)
{
	return heroCommands::isRevisedRules(battle.getBattle()->getHeroCommandRules())
		&& (command == HeroCommand::FEINT || command == HeroCommand::VANGUARD);
}

float revisedArmyUtility(std::shared_ptr<CBattleInfoCallback> before,
	const std::shared_ptr<HypotheticBattle> & projected, BattleSide side, HeroCommand command)
{
	assert(usesRevisedArmyHeuristic(*before, command));
	RevisedOrderEstimate value(std::move(before), projected, side);
	return command == HeroCommand::FEINT ? value.feint() : value.vanguard();
}
}
