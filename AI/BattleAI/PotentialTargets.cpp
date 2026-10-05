/*
 * PotentialTargets.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "PotentialTargets.h"
#include "SpellTargetsEvaluator.h"
#include "../../lib/CStack.h"//todo: remove
#include "../../lib/battle/ReachabilityInfo.h"
#include "../../lib/battle/NewHorizonsArchery.h"
#include "../../lib/battle/NewHorizonsBerserk.h"
#include "../../lib/battle/NewHorizonsPuppetMaster.h"
#include "../../lib/mapObjects/CGTownInstance.h"

PotentialTargets::PotentialTargets(
	const battle::Unit * attacker,
	DamageCache & damageCache,
	std::shared_ptr<HypotheticBattle> state)
{
	auto evaluationState = state;
	auto attackerInfo = evaluationState->battleGetUnitByID(attacker->unitId());

	//FIXME: this should part of battleGetAvailableHexes
	const bool puppetControlled = newHorizonsPuppetMaster::hasValidControlMarker(*evaluationState, attackerInfo);
	berserk = attackerInfo->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE) && !puppetControlled;
	const auto forcedActivationBonus = berserk
		? newHorizonsBerserk::forcedActivationSpeedBonus(*evaluationState, attackerInfo)
		: std::optional<Bonus>{};
	if(forcedActivationBonus)
	{
		// Berserk candidate pointers outlive this constructor, so model the
		// forced activation in its own child branch instead of temporarily
		// changing the caller's shared forecast.
		forcedBerserkState = std::make_shared<HypotheticBattle>(evaluationState->env, evaluationState);
		forcedBerserkState->addUnitBonus(attackerInfo->unitId(), {*forcedActivationBonus});
		evaluationState = forcedBerserkState;
		attackerInfo = evaluationState->battleGetUnitByID(attacker->unitId());
	}
	auto reachability = evaluationState->getReachability(attackerInfo);
	auto avHexes = evaluationState->battleGetAvailableHexes(reachability, attackerInfo, false);
	DamageCache forcedDamageCache;
	DamageCache & actionDamageCache = forcedActivationBonus ? forcedDamageCache : damageCache;
	if(berserk)
	{
		forcedBerserkActions = evaluationState->getBerserkForcedActions(attackerInfo);
		if(forcedBerserkActions.empty())
			forcedBerserkActions.push_back({EActionType::NO_ACTION, attackerInfo->getPosition(), nullptr});
	}

	auto aliveUnits = evaluationState->battleGetUnitsIf([=](const battle::Unit * unit)
	{
		return unit->isValidTarget() && unit->unitId() != attackerInfo->unitId();
	});

	for(auto defender : aliveUnits)
	{
		// Sanctuary bars this unit only as a deliberately selected enemy primary.
		// Attacks whose primary is another stack may still include it as collateral.
		const bool sanctuaryEnemy = defender->hasBonusOfType(BonusType::SANCTIFIED)
			&& evaluationState->battleMatchActionController(attackerInfo, defender);
		if(sanctuaryEnemy && !evaluationState->battleCanTargetEmptyHex(attackerInfo))
			continue;

		if(!berserk && !evaluationState->battleMatchActionController(attackerInfo, defender))
			continue;

		auto GenerateAttackInfo = [&](bool shooting, const BattleHex & hex) -> AttackPossibility
		{
			const int distance = hex.isValid()
				? std::max(0, SpellTargetEvaluator::physicalTravelDistance(reachability, hex))
				: 0;
			auto bai = BattleAttackInfo(attackerInfo, defender, distance, shooting);
			if(shooting && hex.isValid() && hex != attackerInfo->getPosition()
				&& newHorizonsArchery::canUseSkirmisher(evaluationState->battleGetFightingHero(attackerInfo->unitSide()), attackerInfo))
				bai.archeryRangedDamageMultiplierPercent = newHorizonsArchery::SKIRMISHER_DAMAGE_PERCENT;

			return AttackPossibility::evaluate(bai, hex, actionDamageCache, evaluationState);
		};

		if(berserk)
		{
			for(const auto & forcedAction : forcedBerserkActions)
			{
				if(!forcedAction.target || forcedAction.target->unitId() != defender->unitId())
					continue;

				if(forcedAction.type == EActionType::WALK_AND_ATTACK || forcedAction.type == EActionType::SHOOT)
				{
					const bool rangeAttack = forcedAction.type == EActionType::SHOOT;
					const BattleHex hex = forcedAction.type == EActionType::WALK_AND_ATTACK
						? forcedAction.position : BattleHex::INVALID;
					possibleAttacks.push_back(GenerateAttackInfo(rangeAttack, hex));
				}
				else if(forcedAction.type == EActionType::WALK)
					unreachableEnemies.push_back(defender);
			}
		}
		else
		{
			const bool canShootFromCurrentPosition = evaluationState->battleCanShootAction(attackerInfo, defender->getPosition());
			if(canShootFromCurrentPosition)
				possibleAttacks.push_back(GenerateAttackInfo(true, BattleHex::INVALID));

			if(!sanctuaryEnemy && newHorizonsArchery::canUseSkirmisher(evaluationState->battleGetFightingHero(attackerInfo->unitSide()), attackerInfo))
			{
				// Score every legal destination so the AI can trade movement, firing line,
				// range, and Counterfire exposure instead of always choosing one nearest hex.
				for(const BattleHex & hex : evaluationState->battleGetSkirmisherAttackFromHexes(attackerInfo,
					defender->getPosition()))
					possibleAttacks.push_back(GenerateAttackInfo(true, hex));
			}

			if(!canShootFromCurrentPosition && !sanctuaryEnemy)
			{
				for(const BattleHex & hex : avHexes)
				{
					if(!evaluationState->isMeleeAttackPossible(attackerInfo, defender, hex))
						continue;

					auto bai = GenerateAttackInfo(false, hex);
					if(!bai.affectedUnits.empty())
						possibleAttacks.push_back(bai);
				}
			}

			if(!vstd::contains_if(possibleAttacks, [=](const AttackPossibility & pa) { return pa.attack.defender->unitId() == defender->unitId(); }))
				unreachableEnemies.push_back(defender);
		}
	}

	std::ranges::sort(possibleAttacks, [](const AttackPossibility & lhs, const AttackPossibility & rhs) -> bool
	{
		return lhs.damageDiff() > rhs.damageDiff();
	});

	if(forcedActivationBonus && forcedBerserkState)
	{
		// The scoped Speed has done its job: it selected reachable forced actions
		// and informed their attack projections. Remove it before callers replay
		// any chosen AttackPossibility into their longer-lived forecast.
		for(auto & attack : possibleAttacks)
			if(attack.effectPreview)
				attack.effectPreview->removeUnitBonus(attackerInfo->unitId(), {*forcedActivationBonus});
		forcedBerserkState->removeUnitBonus(attackerInfo->unitId(), {*forcedActivationBonus});
	}
}

int64_t PotentialTargets::bestActionValue() const
{
	if(possibleAttacks.empty())
		return 0;

	return bestAction().attackValue();
}

float PotentialTargets::expectedBerserkActionValue() const
{
	if(!berserk || forcedBerserkActions.empty())
		return 0.0f;

	float totalValue = 0.0f;
	for(const auto & forcedAction : forcedBerserkActions)
	{
		if(!forcedAction.target
			|| (forcedAction.type != EActionType::WALK_AND_ATTACK && forcedAction.type != EActionType::SHOOT))
			continue;

		const auto attack = std::ranges::find_if(possibleAttacks, [&](const AttackPossibility & candidate)
		{
			return candidate.attack.defender
				&& candidate.attack.defender->unitId() == forcedAction.target->unitId()
				&& candidate.attack.shooting == (forcedAction.type == EActionType::SHOOT)
				&& (forcedAction.type != EActionType::WALK_AND_ATTACK || candidate.from == forcedAction.position);
		});
		if(attack != possibleAttacks.end())
			totalValue += attack->attackValue();
	}

	return totalValue / static_cast<float>(forcedBerserkActions.size());
}

const AttackPossibility & PotentialTargets::bestAction() const
{
	if(possibleAttacks.empty())
		throw std::runtime_error("No best action, since we don't have any actions");

	if(berserk && !forcedBerserkActions.empty())
	{
		const auto & forcedAction = forcedBerserkActions.front();
		const auto attack = std::ranges::find_if(possibleAttacks, [&](const AttackPossibility & candidate)
		{
			return candidate.attack.defender && forcedAction.target
				&& candidate.attack.defender->unitId() == forcedAction.target->unitId()
				&& candidate.attack.shooting == (forcedAction.type == EActionType::SHOOT)
				&& (forcedAction.type != EActionType::WALK_AND_ATTACK || candidate.from == forcedAction.position);
		});
		if(attack != possibleAttacks.end())
			return *attack;
	}

	return possibleAttacks.front();
}
