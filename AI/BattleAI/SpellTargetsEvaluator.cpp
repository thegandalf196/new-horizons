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
#include "../../lib/CStack.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/spells/Problem.h"
#include "../../lib/CRandomGenerator.h"
#include "SpellTargetsEvaluator.h"
#include <vcmi/spells/Spell.h>

using namespace spells;

namespace
{
// Transfigure Matter is represented as a location-targeted spell by the
// generic mechanics layer, but only physical battlefield obstacles are legal
// aims.  Keep this identity check local to the AI until the curated spell
// receives a public typed helper in the shared spell API.
bool isTransfigureMatter(const Mechanics * spellMechanics)
{
	const auto * spell = spellMechanics ? spellMechanics->getSpell() : nullptr;
	return spell && spell->getJsonKey() == "new-horizons:transfigureMatter";
}

bool isPhysicalObstacle(const CObstacleInstance & obstacle)
{
	// The curated effect deliberately admits ordinary scenery only. Absolute
	// obstacles cover siege/fortification-like scenery and are rejected by the
	// authoritative Transfigure Matter script.
	return obstacle.obstacleType == CObstacleInstance::USUAL;
}

std::vector<Target> physicalObstacleTargets(const Mechanics * spellMechanics)
{
	std::vector<Target> result;
	std::set<BattleHex> seen;

	for(const auto & obstacle : spellMechanics->battle()->battleGetAllObstacles())
	{
		if(!obstacle || !isPhysicalObstacle(*obstacle))
			continue;

		// Use an affected tile so the aim is a real battlefield hex and the
		// authoritative validator can resolve the object by position.
		const auto affectedTiles = obstacle->getAffectedTiles();
		if(affectedTiles.empty())
			continue;

		const auto aimHex = affectedTiles.front();
		if(!aimHex.isValid() || !seen.insert(aimHex).second)
			continue;

		Target target{Destination(aimHex)};
		detail::ProblemImpl problem;
		if(spellMechanics->canBeCastAt(target, problem))
			result.push_back(std::move(target));
	}

	return result;
}
}

std::vector<Target> SpellTargetEvaluator::getViableTargets(const Mechanics * spellMechanics)
{
	std::vector<Target> result;
	std::vector<AimType> targetTypes = spellMechanics->getTargetTypes();
	if(isTransfigureMatter(spellMechanics))
		return physicalObstacleTargets(spellMechanics);

	if(targetTypes == std::vector<AimType>{AimType::CREATURE, AimType::LOCATION})
		return creatureLocationTargets(spellMechanics);
	if(targetTypes == std::vector<AimType>{AimType::CREATURE, AimType::CREATURE})
		return creaturePairTargets(spellMechanics);
	if(targetTypes.size() != 1)
		return result;

	auto targetType = targetTypes.front();

	switch(targetType)
	{
		case AimType::CREATURE:
			return allTargetableCreatures(spellMechanics, true);
		case AimType::LOCATION:
		{
			if(spellMechanics->isNeutralSpell())
				return defaultLocationSpellHeuristics(
					spellMechanics
				); // theoretically anything can be a useful destination, so we balance performance and validity
			else
				return theBestLocationCasts(spellMechanics);
		}
		case AimType::NOTHING:
			return std::vector<Target>(1); //default-constructed target means cast without destination
		default:
			return result;
	}
}

std::vector<Target> SpellTargetEvaluator::creaturePairTargets(const spells::Mechanics * spellMechanics)
{
	std::vector<Target> result;
	// This query includes corpses. Sacrifice chooses a corpse BEFORE a living
	// victim; do not replace it with an alive-only target list or reorder pairs.
	const auto units = spellMechanics->battle()->battleGetAllUnits(false);
	for(const auto * first : units)
	{
		Target target{Destination(first)};
		detail::ProblemImpl prefixProblem;
		if(!spellMechanics->canBeCastAt(target, prefixProblem))
			continue;
		for(const auto * second : units)
		{
			target.resize(1);
			target.emplace_back(second);
			detail::ProblemImpl pairProblem;
			if(spellMechanics->canBeCastAt(target, pairProblem))
				result.push_back(target);
		}
	}
	return result;
}

std::vector<Target> SpellTargetEvaluator::creatureLocationTargets(const spells::Mechanics * spellMechanics)
{
	std::vector<Target> result;
	for(const auto * unit : spellMechanics->battle()->battleGetAllUnits(false))
	{
		Target target{Destination(unit)};
		detail::ProblemImpl sourceProblem;
		if(!spellMechanics->canBeCastAt(target, sourceProblem))
			continue;

		// Preserve the exact source unit. The spell validator decides occupancy,
		// double-wide placement, walls/moats and rank restrictions for each pair.
		for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		{
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

std::vector<Target> SpellTargetEvaluator::defaultLocationSpellHeuristics(const spells::Mechanics * spellMechanics)
{
	std::vector<Target> result = allTargetableCreatures(spellMechanics, false);
	auto units = spellMechanics->battle()->battleGetAllUnits(false);
	for(const auto * unit : units) //insert a random surrounding hex
	{
		auto surroundingHexes = unit->getSurroundingHexes();
		if(!surroundingHexes.empty())
		{
			auto randomSurroundingHex = *RandomGeneratorUtil::nextItem(surroundingHexes, CRandomGenerator::getDefault()); // don't think this method bias matter with such small numbers
			addIfCanBeCast(spellMechanics, randomSurroundingHex, result);
		}
	}
	// OBSTACLE spells (including Remove Obstacle) are normalized to LOCATION
	// by BaseMechanics. Unit-neighbour sampling alone misses distant obstacles.
	std::set<BattleHex> considered;
	for(const auto & target : result)
		considered.insert(target.front().hexValue);
	for(const auto & obstacle : spellMechanics->battle()->battleGetAllObstacles())
		for(const auto hex : obstacle->getAffectedTiles())
			if(considered.insert(hex).second)
				addIfCanBeCast(spellMechanics, hex, result);
	return result;
}

std::vector<Target> SpellTargetEvaluator::allTargetableCreatures(const spells::Mechanics * spellMechanics, bool exactUnit)
{
	std::vector<Target> result;
	const auto units = spellMechanics->battle()->battleGetAllUnits(false);
	for(const auto * unit : units)
	{
		Target target{exactUnit ? Destination(unit) : Destination(unit->getPosition())};
		detail::ProblemImpl problem;
		if(spellMechanics->canBeCastAt(target, problem))
			result.push_back(target);
	}
	return result;
}

std::vector<Target> SpellTargetEvaluator::theBestLocationCasts(const spells::Mechanics * spellMechanics)
{
	std::vector<Target> result;
	std::map<BattleHex, std::set<const CStack *>> allCasts;
	std::map<BattleHex, std::set<const CStack *>> bestCasts;
	for(int i = 0; i < GameConstants::BFIELD_SIZE; i++)
	{
		BattleHex dest(i);
		if(canBeCastAt(spellMechanics, dest))
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
		std::set<BattleHex> worseCasts;
		if(isCastHarmful(spellMechanics, cast.second))
			continue;

		bool isBestCast = true;
		for(const auto & bestCast : bestCasts)
		{
			Compare compare = compareAffectedStacks(spellMechanics, cast.second, bestCast.second);

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
				bestCasts.erase(worseCast);
		}
	}

	for(const auto & cast : bestCasts)
	{
		Destination des(cast.first);
		result.push_back({des});
	}
	return result;
}

bool SpellTargetEvaluator::isCastHarmful(const spells::Mechanics * spellMechanics, const std::set<const CStack *> & affectedStacks)
{

	bool isAffectedAlly = false;
	bool isAffectedEnemy = false;

	for(const CStack * affectedUnit : affectedStacks)
	{
		// Hypnotize changes control without changing the unit's original side.
		if(spellMechanics->battle()->battleGetOwner(affectedUnit) == spellMechanics->getCasterColor())
			isAffectedAlly = true;
		else
			isAffectedEnemy = true;
	}

	return (spellMechanics->isPositiveSpell() && !isAffectedAlly) || (spellMechanics->isNegativeSpell() && !isAffectedEnemy);
}

SpellTargetEvaluator::Compare SpellTargetEvaluator::compareAffectedStacks(
	const spells::Mechanics * spellMechanics, const std::set<const CStack *> & newCast, const std::set<const CStack *> & oldCast)
{
	if(newCast == oldCast)
		return Compare::EQUAL;

	auto getAlliedUnits = [&spellMechanics](const std::set<const CStack *> & allUnits) -> std::set<const CStack *>
	{
		std::set<const CStack *> alliedUnits;
		for(auto stack : allUnits)
		{
			if(spellMechanics->battle()->battleGetOwner(stack) == spellMechanics->getCasterColor())
				alliedUnits.insert(stack);
		}
		return alliedUnits;
	};

	auto getEnemyUnits = [&spellMechanics](const std::set<const CStack *> & allUnits) -> std::set<const CStack *>
	{
		std::set<const CStack *> enemyUnits;
		for(auto stack : allUnits)
		{
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

bool SpellTargetEvaluator::canBeCastAt(const spells::Mechanics * spellMechanics, BattleHex hex)
{
	detail::ProblemImpl ignored;
	Destination des(hex);
	return spellMechanics->canBeCastAt({des}, ignored);
}

void SpellTargetEvaluator::addIfCanBeCast(const spells::Mechanics * spellMechanics, BattleHex hex, std::vector<Target> & targets)
{
	detail::ProblemImpl ignored;
	Destination des(hex);
	if(spellMechanics->canBeCastAt({des}, ignored))
		targets.push_back({des});
}
