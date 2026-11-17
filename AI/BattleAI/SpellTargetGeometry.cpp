/*
 * SpellTargetsEvaluator.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "PhysicalCleanseValue.h"
#include "CreatureActivationValue.h"
#include "../../lib/battle/NewHorizonsStun.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/spells/NewHorizonsMetamagicEffects.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/ReachabilityInfo.h"
#include "../../lib/battle/NewHorizonsBulwark.h"
#include "../../lib/battle/BattleForm.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/battle/NewHorizonsConfusionResolution.h"
#include "../../lib/battle/NewHorizonsArchery.h"
#include "../../lib/battle/NewHorizonsShadowGift.h"
#include "../../lib/battle/NewHorizonsSoulChain.h"
#include "AttackPossibility.h"
#include "PotentialTargets.h"
#include "StackWithBonuses.h"
#include "../../lib/spells/Problem.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/effects/BattleForm.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsFireWall.h"
#include "../../lib/spells/NewHorizonsPurify.h"
#include "../../lib/spells/NewHorizonsSorcery.h"
#include "../../lib/spells/NewHorizonsVengefulVines.h"
#include "../../lib/spells/NewHorizonsNaturesWrath.h"
#include "../../lib/spells/NewHorizonsPandemonium.h"
#include "../../lib/spells/NewHorizonsRealityWarp.h"
#include "../../lib/battle/NewHorizonsPlague.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/CRandomGenerator.h"
#include "SpellTargetsEvaluator.h"
#include "SearchBudget.h"
#include "SummonEvaluation.h"
#include "../../lib/spells/NewHorizonsSpellAvailability.h"
#include <vcmi/spells/Spell.h>

#include <set>
#include <tuple>
#include "HypotheticBattle.h"

using namespace spells;

std::vector<Target> SpellTargetEvaluator::creaturePairTargets(const spells::Mechanics * spellMechanics, battleSearch::SearchBudget * budget)
{
	std::vector<Target> result;
	try
	{

		// This query includes corpses. Sacrifice chooses a corpse BEFORE a living
		// victim; do not replace it with an alive-only target list or reorder pairs.
		const auto units = spellMechanics->battle()->battleGetAllUnits(false);
		for(const auto * first : units)
		{
			battleSearch::require(budget, battleSearch::WorkKind::Generation);
			Target target{Destination(first)};
			detail::ProblemImpl prefixProblem;
			if(!spellMechanics->canBeCastAt(target, prefixProblem))
				continue;
			for(const auto * second : units)
			{
				battleSearch::require(budget, battleSearch::WorkKind::Generation);
				target.resize(1);
				target.emplace_back(second);
				detail::ProblemImpl pairProblem;
				if(spellMechanics->canBeCastAt(target, pairProblem))
					result.push_back(target);
			}
		}
		return result;

	}
	catch(const battleSearch::BudgetExhausted &)
	{
		return result;
	}
}

std::vector<Target> SpellTargetEvaluator::creatureLocationTargets(const spells::Mechanics * spellMechanics, battleSearch::SearchBudget * budget)
{
	std::vector<Target> result;
	try
	{

		for(const auto * unit : spellMechanics->battle()->battleGetAllUnits(false))
		{
			battleSearch::require(budget, battleSearch::WorkKind::Generation);
			Target target{Destination(unit)};
			detail::ProblemImpl sourceProblem;
			if(!spellMechanics->canBeCastAt(target, sourceProblem))
				continue;

			// Preserve the exact source unit. The spell validator decides occupancy,
			// double-wide placement, walls/moats and rank restrictions for each pair.
			for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
			{
				battleSearch::require(budget, battleSearch::WorkKind::Generation);
				const BattleHex destination(index);
				if(destination == unit->getPosition())
					continue; // Moving nowhere cannot improve the hypothetical battle.
				target.resize(1);
				target.emplace_back(destination);
				detail::ProblemImpl destinationProblem;
				if(spellMechanics->canBeCastAt(target, destinationProblem))
					result.push_back(target);
			}
		}
		return result;

	}
	catch(const battleSearch::BudgetExhausted &)
	{
		return result;
	}
}

std::vector<Target> SpellTargetEvaluator::defaultLocationSpellHeuristics(const spells::Mechanics * spellMechanics, battleSearch::SearchBudget * budget)
{
	std::vector<Target> result;
	try
	{
		result = allTargetableCreatures(spellMechanics, false, budget);
		auto units = spellMechanics->battle()->battleGetAllUnits(false);
		for(const auto * unit : units) //insert a random surrounding hex
		{
			battleSearch::require(budget, battleSearch::WorkKind::Generation);
			auto surroundingHexes = unit->getSurroundingHexes();
			if(!surroundingHexes.empty())
			{
				const auto randomSurroundingHex = *std::min_element(surroundingHexes.begin(), surroundingHexes.end()); // Evaluation only: no gameplay RNG draw.
				addIfCanBeCast(spellMechanics, randomSurroundingHex, result, budget);
			}
		}
		// OBSTACLE spells (including Remove Obstacle) are normalized to LOCATION
		// by BaseMechanics. Unit-neighbour sampling alone misses distant obstacles.
		std::set<BattleHex> considered;
		for(const auto & target : result)
			{
				battleSearch::require(budget, battleSearch::WorkKind::Generation);
				considered.insert(target.front().hexValue);
			}
		for(const auto & obstacle : spellMechanics->battle()->battleGetAllObstacles())
			{
				battleSearch::require(budget, battleSearch::WorkKind::Generation);
				for(const auto hex : obstacle->getAffectedTiles())
				{
					battleSearch::require(budget, battleSearch::WorkKind::Generation);
					if(considered.insert(hex).second)
					addIfCanBeCast(spellMechanics, hex, result, budget);
				}
			}
		return result;

	}
	catch(const battleSearch::BudgetExhausted &)
	{
		return result;
	}
}

std::vector<Target> SpellTargetEvaluator::allTargetableCreatures(const spells::Mechanics * spellMechanics, bool exactUnit, battleSearch::SearchBudget * budget)
{
	std::vector<Target> result;
	try
	{

		const auto units = spellMechanics->battle()->battleGetAllUnits(false);
		for(const auto * unit : units)
		{
			battleSearch::require(budget, battleSearch::WorkKind::Generation);
			Target target{exactUnit ? Destination(unit) : Destination(unit->getPosition())};
			detail::ProblemImpl problem;
			if(spellMechanics->canBeCastAt(target, problem))
				result.push_back(target);
		}
		return result;

	}
	catch(const battleSearch::BudgetExhausted &)
	{
		return result;
	}
}

std::vector<Target> SpellTargetEvaluator::theBestLocationCasts(const spells::Mechanics * spellMechanics, battleSearch::SearchBudget * budget)
{
	std::vector<Target> result;
	std::map<BattleHex, std::set<const CStack *>> allCasts;
	try
	{

		std::map<BattleHex, std::set<const CStack *>> bestCasts;
		for(int i = 0; i < GameConstants::BFIELD_SIZE; i++)
		{
			battleSearch::require(budget, battleSearch::WorkKind::Generation);
			BattleHex dest(i);
			if(canBeCastAt(spellMechanics, dest, budget))
			{
				Target target;
				target.emplace_back(dest);
				auto temp = spellMechanics->getAffectedStacks(target);
				std::set<const CStack *> affectedStacks(temp.begin(), temp.end());
				allCasts[dest] = affectedStacks;
			}
		}

		for(const auto & cast : allCasts)
		{
			battleSearch::require(budget, battleSearch::WorkKind::Generation);
			std::set<BattleHex> worseCasts;
			if(isCastHarmful(spellMechanics, cast.second, budget))
				continue;

			bool isBestCast = true;
			for(const auto & bestCast : bestCasts)
			{
				battleSearch::require(budget, battleSearch::WorkKind::Generation);
				Compare compare = compareAffectedStacks(spellMechanics, cast.second, bestCast.second, budget);

				if(compare == Compare::WORSE || compare == Compare::EQUAL)
				{
					isBestCast = false;
					break;
				}

				if(compare == Compare::BETTER)
				{
					worseCasts.insert(bestCast.first);
				}
			}

			if(isBestCast)
			{
				bestCasts.insert(cast);
				for(BattleHex worseCast : worseCasts)
					{
						battleSearch::require(budget, battleSearch::WorkKind::Generation);
						bestCasts.erase(worseCast);
					}
			}
		}

		for(const auto & cast : bestCasts)
		{
			battleSearch::require(budget, battleSearch::WorkKind::Generation);
			Destination des(cast.first);
			result.push_back({des});
		}
		return result;

	}
	catch(const battleSearch::BudgetExhausted &)
	{
		if(result.empty())
			for(const auto & entry : allCasts) result.push_back({Destination(entry.first)});
		return result;
	}
}

bool SpellTargetEvaluator::isCastHarmful(const spells::Mechanics * spellMechanics, const std::set<const CStack *> & affectedStacks, battleSearch::SearchBudget * budget)
{

	bool isAffectedAlly = false;
	bool isAffectedEnemy = false;

	for(const CStack * affectedUnit : affectedStacks)
	{
		battleSearch::require(budget, battleSearch::WorkKind::Generation);
		// Hypnotize changes control without changing the unit's original side.
		if(spellMechanics->battle()->battleGetOwner(affectedUnit) == spellMechanics->getCasterColor())
			isAffectedAlly = true;
		else
			isAffectedEnemy = true;
	}

	return (spellMechanics->isPositiveSpell() && !isAffectedAlly) || (spellMechanics->isNegativeSpell() && !isAffectedEnemy);
}

SpellTargetEvaluator::Compare SpellTargetEvaluator::compareAffectedStacks(
	const spells::Mechanics * spellMechanics, const std::set<const CStack *> & newCast, const std::set<const CStack *> & oldCast, battleSearch::SearchBudget * budget)
{
	if(newCast == oldCast)
		return Compare::EQUAL;

	auto getAlliedUnits = [&spellMechanics, &budget](const std::set<const CStack *> & allUnits) -> std::set<const CStack *>
	{
		std::set<const CStack *> alliedUnits;
		for(auto stack : allUnits)
		{
			battleSearch::require(budget, battleSearch::WorkKind::Generation);
			if(spellMechanics->battle()->battleGetOwner(stack) == spellMechanics->getCasterColor())
				alliedUnits.insert(stack);
		}
		return alliedUnits;
	};

	auto getEnemyUnits = [&spellMechanics, &budget](const std::set<const CStack *> & allUnits) -> std::set<const CStack *>
	{
		std::set<const CStack *> enemyUnits;
		for(auto stack : allUnits)
		{
			battleSearch::require(budget, battleSearch::WorkKind::Generation);
			if(spellMechanics->battle()->battleGetOwner(stack) != spellMechanics->getCasterColor())
				enemyUnits.insert(stack);
		}
		return enemyUnits;
	};

	Compare alliedSubsetComparison = compareAffectedStacksSubset(spellMechanics, getAlliedUnits(newCast), getAlliedUnits(oldCast));
	Compare enemySubsetComparison = compareAffectedStacksSubset(spellMechanics, getEnemyUnits(newCast), getEnemyUnits(oldCast));

	if(spellMechanics->isPositiveSpell())
		enemySubsetComparison = reverse(enemySubsetComparison);
	else if(spellMechanics->isNegativeSpell())
		alliedSubsetComparison = reverse(alliedSubsetComparison);

	std::set<Compare> comparisonResults = {alliedSubsetComparison, enemySubsetComparison};
	std::set<std::set<Compare>> possibleBetterResults = {
		{Compare::BETTER, Compare::BETTER},
        {Compare::BETTER, Compare::EQUAL }
	};
	std::set<std::set<Compare>> possibleWorstResults = {
		{Compare::WORSE, Compare::WORSE},
        {Compare::WORSE, Compare::EQUAL}
	};

	if(possibleBetterResults.find(comparisonResults) != possibleBetterResults.end())
		return Compare::BETTER;
	if(possibleWorstResults.find(comparisonResults) != possibleWorstResults.end())
		return Compare::WORSE;

	return Compare::DIFFERENT;
}

SpellTargetEvaluator::Compare SpellTargetEvaluator::compareAffectedStacksSubset(
    const spells::Mechanics * spellMechanics, const std::set<const CStack *> & newSubset, const std::set<const CStack *> & oldSubset)
{
	if(newSubset.size() == oldSubset.size())
		return newSubset == oldSubset ? Compare::EQUAL : Compare::DIFFERENT;

	if(oldSubset.size() > newSubset.size())
		return reverse(compareAffectedStacksSubset(spellMechanics, oldSubset, newSubset));

	const std::set<const CStack *> & biggerSet = newSubset;
	const std::set<const CStack *> & smallerSet = oldSubset;

	if(std::includes(biggerSet.begin(), biggerSet.end(), smallerSet.begin(), smallerSet.end()))
		return Compare::BETTER;
	else
		return Compare::DIFFERENT;
}

SpellTargetEvaluator::Compare SpellTargetEvaluator::reverse(SpellTargetEvaluator::Compare compare)
{
	switch(compare)
	{
		case Compare::BETTER:
			return Compare::WORSE;
		case Compare::WORSE:
			return Compare::BETTER;
		default:
			return compare;
	}
}

bool SpellTargetEvaluator::canBeCastAt(const spells::Mechanics * spellMechanics, BattleHex hex, battleSearch::SearchBudget * budget)
{
	detail::ProblemImpl ignored;
	Destination des(hex);
	return spellMechanics->canBeCastAt({des}, ignored);
}

void SpellTargetEvaluator::addIfCanBeCast(const spells::Mechanics * spellMechanics, BattleHex hex, std::vector<Target> & targets, battleSearch::SearchBudget * budget)
{
	detail::ProblemImpl ignored;
	Destination des(hex);
	if(spellMechanics->canBeCastAt({des}, ignored))
		targets.push_back({des});
}
