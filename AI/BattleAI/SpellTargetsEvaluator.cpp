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
#include "AttackPossibility.h"
#include "../../lib/spells/Problem.h"
#include "../../lib/spells/NewHorizonsMagic.h"
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

bool isCanonicalLandMine(const Mechanics * spellMechanics)
{
	return spellMechanics
		&& spellMechanics->usesNewHorizonsMagic()
		&& newHorizonsMagic::isLandMine(spellMechanics->getSpellId());
}

bool isCanonicalFireWall(const Mechanics * spellMechanics)
{
	return spellMechanics
		&& spellMechanics->usesNewHorizonsMagic()
		&& newHorizonsMagic::isFireWall(spellMechanics->getSpellId());
}

bool canonicalLandMineHexIsEmpty(const CBattleInfoCallback & battle,
	const AccessibilityInfo & accessibility, const BattleHex & hex)
{
	// Keep this predicate in lockstep with the authoritative action validator.
	// In particular, ACCESSIBLE is intentionally stricter than merely
	// passable: a gate, wall, side column, moat, or destructible wall is not a
	// legal canonical mine tile.
	if(!hex.isAvailable()
		|| accessibility[hex.toInt()] != EAccessibility::ACCESSIBLE
		|| battle.battleGetUnitByPos(hex, true)
		|| !battle.battleGetAllObstaclesOnPos(hex, false).empty())
		return false;

	if(!battle.hasFortifications())
		return true;

	const auto wallPart = battle.battleHexToWallPart(hex);
	if(wallPart == EWallPart::INVALID)
		return true;
	if(wallPart == EWallPart::INDESTRUCTIBLE_PART
		|| wallPart == EWallPart::INDESTRUCTIBLE_PART_OF_GATE
		|| wallPart == EWallPart::BOTTOM_TOWER
		|| wallPart == EWallPart::UPPER_TOWER)
		return false;

	const auto wallState = battle.battleGetWallState(wallPart);
	return wallState == EWallState::NONE || wallState == EWallState::DESTROYED;
}

struct LandMineHexScore
{
	BattleHex hex;
	int64_t score = 0;
	std::vector<uint32_t> adjacentEnemies;
};

bool isGroundHostile(const Mechanics * spellMechanics, const battle::Unit * unit)
{
	if(!unit || !unit->alive() || unit->isGhost() || !unit->isValidTarget() || unit->isTurret())
		return false;
	if(unit->hasBonusOfType(BonusType::FLYING))
		return false;
	return spellMechanics->battle()->battleGetOwner(unit) != spellMechanics->getCasterColor();
}

bool isGroundAlly(const Mechanics * spellMechanics, const battle::Unit * unit)
{
	if(!unit || !unit->alive() || unit->isGhost() || !unit->isValidTarget() || unit->isTurret())
		return false;
	if(unit->hasBonusOfType(BonusType::FLYING))
		return false;
	return spellMechanics->battle()->battleGetOwner(unit) == spellMechanics->getCasterColor();
}

uint64_t landMineDamagePotential(const Mechanics * spellMechanics, const battle::Unit * enemy)
{
	const auto adjustedDamage = std::max<int64_t>(0, spellMechanics->adjustEffectValue(enemy));
	return std::min<uint64_t>(static_cast<uint64_t>(adjustedDamage), enemy->getAvailableHealth());
}

bool isLandMineAffectable(const Mechanics * spellMechanics, const battle::Unit * enemy)
{
	if(!isGroundHostile(spellMechanics, enemy)
		|| !spellMechanics->isReceptive(enemy)
		|| enemy->hasImmunity(spellMechanics->getSpellId())
		|| enemy->hasAbsoluteImmunity(spellMechanics->getSpellId())
		|| enemy->isInvincible())
		return false;

	const int resistance = std::clamp(enemy->magicResistance(), 0, 100);
	return resistance < 100 && landMineDamagePotential(spellMechanics, enemy) > 0;
}

std::vector<const battle::Unit *> landMineEnemies(const Mechanics * spellMechanics)
{
	std::vector<const battle::Unit *> result;
	for(const auto * unit : spellMechanics->battle()->battleGetAllUnits(false))
		if(isLandMineAffectable(spellMechanics, unit))
			result.push_back(unit);
	return result;
}

std::vector<const battle::Unit *> landMineAllies(const Mechanics * spellMechanics)
{
	std::vector<const battle::Unit *> result;
	for(const auto * unit : spellMechanics->battle()->battleGetAllUnits(false))
		if(isGroundAlly(spellMechanics, unit))
			result.push_back(unit);
	return result;
}

int distanceToUnit(const BattleHex & hex, const battle::Unit * unit)
{
	int result = std::numeric_limits<int>::max();
	for(const auto & occupied : unit->getHexes())
		if(occupied.isValid())
			result = std::min(result, static_cast<int>(BattleHex::getDistance(hex, occupied)));
	return result;
}

int distanceToFootprint(const spells::Target & target, const battle::Unit * unit)
{
	int result = std::numeric_limits<int>::max();
	for(const auto & destination : target)
	{
		if(destination.unitValue != nullptr || !destination.hexValue.isValid())
			continue;
		result = std::min(result, distanceToUnit(destination.hexValue, unit));
	}
	return result;
}

float fireWallTriggerLikelihood(const spells::Target & target, const battle::Unit * unit)
{
	const int distance = distanceToFootprint(target, unit);
	if(distance == std::numeric_limits<int>::max())
		return 0.0f;

	const int movement = std::max(0, static_cast<int>(unit->getMovementRange(0)));
	if(distance <= 1)
		return 0.75f;
	if(distance <= movement + 1)
		return 0.45f;
	if(distance <= movement + 3)
		return 0.20f;
	return 0.05f;
}

LandMineHexScore scoreLandMineHex(const Mechanics * spellMechanics, const BattleHex & hex,
	const std::vector<const battle::Unit *> & enemies,
	const std::vector<const battle::Unit *> & allies)
{
	LandMineHexScore result;
	result.hex = hex;

	// A mine directly adjacent to a ground enemy is the strongest pressure: it
	// punishes the next step and blocks the most obvious melee approach.  The
	// remaining terms keep mines useful when no adjacent tile is available by
	// favouring reachable-looking ground paths toward our army.
	for(const auto * enemy : enemies)
	{
		const int enemyDistance = distanceToUnit(hex, enemy);
		if(enemyDistance == std::numeric_limits<int>::max())
			continue;
		// Placement must follow the value of the delayed damage, rather than the
		// number of hostile stacks alone.  In particular, immune and fully
		// resistant clusters are absent from `enemies` and cannot pull every mine
		// away from a susceptible stack elsewhere on the battlefield.
		const auto damageValue = landMineDamagePotential(spellMechanics, enemy);
		if(damageValue == 0)
			continue;

		const bool adjacent = enemyDistance == 1;
		const int movement = static_cast<int>(enemy->getMovementRange(0));
		if(adjacent)
			result.score += 100000 * static_cast<int64_t>(damageValue);
		else if(enemyDistance <= movement + 1)
			result.score += (25000 + (movement + 1 - enemyDistance) * 1000)
				* static_cast<int64_t>(damageValue);
		else
			result.score += static_cast<int64_t>(std::max(0, 6000 - enemyDistance * 250))
				* static_cast<int64_t>(damageValue);

		if(adjacent)
			result.adjacentEnemies.push_back(enemy->unitId());

		// Prefer a hex on the geometric shortest corridor from an enemy to one of
		// our ground units.  This is only a tie-breaker behind adjacency/range,
		// but avoids putting every mine in an irrelevant corner of the field.
		int closestAllyDistance = std::numeric_limits<int>::max();
		for(const auto * ally : allies)
			closestAllyDistance = std::min(closestAllyDistance, distanceToUnit(enemy->getPosition(), ally));
		if(closestAllyDistance != std::numeric_limits<int>::max())
		{
			const int candidateToAlly = [&]()
			{
				int distance = std::numeric_limits<int>::max();
				for(const auto * ally : allies)
					distance = std::min(distance, distanceToUnit(hex, ally));
				return distance;
			}();
			if(candidateToAlly < closestAllyDistance)
				result.score += (closestAllyDistance - candidateToAlly) * 100;
		}
	}

	return result;
}

float landMineTriggerLikelihood(const BattleHex & hex, const battle::Unit * enemy)
{
	const int distance = distanceToUnit(hex, enemy);
	if(distance == std::numeric_limits<int>::max())
		return 0.0f;

	// A placed mine is a delayed threat, not an immediate attack.  Keep the
	// expected trigger contribution on the same scale as a direct attack and
	// deliberately discount tiles which are not on the enemy's next approach.
	// The exact values are a stable ranking heuristic; the authoritative script
	// still decides whether and when a unit actually enters a mine.
	const int movement = std::max(0, static_cast<int>(enemy->getMovementRange(0)));
	if(distance <= 1)
		return 0.75f;
	if(distance <= movement + 1)
		return 0.45f;
	if(distance <= movement + 3)
		return 0.20f;
	return 0.05f;
}

double maximumWeightLandMineAssignment(const std::vector<std::vector<double>> & enemyValues,
	size_t mineCount)
{
	if(enemyValues.empty() || mineCount == 0)
		return 0.0;

	// Canonical Land Mine casts contain two to four mines.  A bitmask DP is
	// therefore small enough to enumerate every one-enemy/one-mine assignment,
	// while avoiding greedy collisions such as A:[.75,.75], B:[.75,.45].
	const size_t assignmentCount = size_t{1} << mineCount;
	std::vector<double> best(assignmentCount, 0.0);
	for(const auto & enemy : enemyValues)
	{
		std::vector<double> next = best; // This enemy may remain unassigned.
		for(size_t assignment = 0; assignment < assignmentCount; ++assignment)
		{
			for(size_t mine = 0; mine < mineCount; ++mine)
			{
				if(assignment & (size_t{1} << mine))
					continue;
				const size_t withMine = assignment | (size_t{1} << mine);
				next[withMine] = std::max(next[withMine], best[assignment] + enemy[mine]);
			}
		}
		best.swap(next);
	}

	return *std::max_element(best.begin(), best.end());
}

std::vector<LandMineHexScore> legalLandMineHexes(const Mechanics * spellMechanics)
{
	std::vector<LandMineHexScore> result;
	const auto * battle = spellMechanics->battle();
	const auto accessibility = battle->getAccessibility();
	const auto enemies = landMineEnemies(spellMechanics);
	const auto allies = landMineAllies(spellMechanics);
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex hex(index);
		if(!canonicalLandMineHexIsEmpty(*battle, accessibility, hex))
			continue;
		result.push_back(scoreLandMineHex(spellMechanics, hex, enemies, allies));
	}
	return result;
}

bool targetUsesDistinctLegalLandMineHexes(const Mechanics * spellMechanics, const Target & target)
{
	const auto * battle = spellMechanics->battle();
	const auto accessibility = battle->getAccessibility();
	std::set<int> seen;
	for(const auto & destination : target)
	{
		if(destination.unitValue != nullptr
			|| !destination.hexValue.isValid()
			|| !seen.insert(destination.hexValue.toInt()).second
			|| !canonicalLandMineHexIsEmpty(*battle, accessibility, destination.hexValue))
			return false;
	}
	return true;
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
	if(isCanonicalFireWall(spellMechanics))
		return canonicalFireWallTargets(spellMechanics);
	if(isCanonicalLandMine(spellMechanics))
		return canonicalLandMineTargets(spellMechanics);

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

std::vector<Target> SpellTargetEvaluator::canonicalFireWallTargets(const Mechanics * spellMechanics)
{
	std::vector<Target> result;
	const auto * battle = spellMechanics->battle();
	const auto accessibility = battle->getAccessibility();

	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex start(index);
		if(!canonicalLandMineHexIsEmpty(*battle, accessibility, start))
			continue;

		for(const auto direction : BattleHex::hexagonalDirections())
		{
			Target line;
			BattleHex current = start;
			bool legal = true;
			for(int length = 0; length < 3; ++length)
			{
				if(!canonicalLandMineHexIsEmpty(*battle, accessibility, current))
				{
					legal = false;
					break;
				}
				line.emplace_back(current);
				if(length != 2)
					current = current.cloneInDirection(direction, false);
			}
			if(!legal)
				continue;

			detail::ProblemImpl problem;
			if(spellMechanics->canBeCastAt(line, problem))
				result.push_back(std::move(line));
		}
	}

	return result;
}

std::vector<Target> SpellTargetEvaluator::canonicalLandMineTargets(const Mechanics * spellMechanics)
{
	const int required = newHorizonsMagic::landMineHexCount(spellMechanics->getEffectPower());
	auto candidates = legalLandMineHexes(spellMechanics);
	if(candidates.size() < static_cast<size_t>(required))
		return {};

	// Greedy coverage keeps the target vector useful against multiple hostile
	// ground stacks instead of selecting four adjacent hexes around one stack
	// solely because they happen to have the same local score.  Ties are broken
	// by battlefield index, making replay/network output deterministic.
	std::vector<LandMineHexScore> selected;
	std::set<uint32_t> coveredEnemies;
	for(int index = 0; index < required; ++index)
	{
		auto best = candidates.end();
		int64_t bestScore = std::numeric_limits<int64_t>::min();
		for(auto candidate = candidates.begin(); candidate != candidates.end(); ++candidate)
		{
			if(vstd::contains_if(selected, [&](const LandMineHexScore & previous)
			{
				return previous.hex == candidate->hex;
			}))
				continue;

			int64_t score = candidate->score;
			for(const auto enemy : candidate->adjacentEnemies)
				if(!coveredEnemies.contains(enemy))
					score += 5000;

			if(best == candidates.end() || score > bestScore
				|| (score == bestScore && candidate->hex.toInt() < best->hex.toInt()))
			{
				best = candidate;
				bestScore = score;
			}
		}

		if(best == candidates.end())
			return {};
		selected.push_back(*best);
		coveredEnemies.insert(best->adjacentEnemies.begin(), best->adjacentEnemies.end());
	}

	Target result;
	result.reserve(selected.size());
	for(const auto & candidate : selected)
		result.emplace_back(candidate.hex);

	// The strict live-state predicate above mirrors the server's fast rejection
	// path.  The spell script remains the final local check for any content-level
	// applicability rule, and only a complete vector is ever returned.
	detail::ProblemImpl problem;
	if(!targetUsesDistinctLegalLandMineHexes(spellMechanics, result)
		|| !spellMechanics->canBeCastAt(result, problem))
		return {};

	return {std::move(result)};
}

float SpellTargetEvaluator::landMinePlacementValue(const Mechanics * spellMechanics,
	const Target & target, std::shared_ptr<CBattleInfoCallback> battleState)
{
	if(!isCanonicalLandMine(spellMechanics)
		|| target.empty()
		|| static_cast<int>(target.size()) != newHorizonsMagic::landMineHexCount(spellMechanics->getEffectPower())
		|| !targetUsesDistinctLegalLandMineHexes(spellMechanics, target))
		return 0.0f;

	detail::ProblemImpl problem;
	if(!spellMechanics->canBeCastAt(target, problem))
		return 0.0f;

	const auto enemies = landMineEnemies(spellMechanics);
	if(enemies.empty())
		return 0.0f;

	// AttackPossibility is the BattleAI's common damage-reduction currency.  Use
	// the live callback when the caller owns it (BattleEvaluator does), while
	// retaining a non-owning fallback for small targeting-only callers/tests.
	if(!battleState)
	{
		const auto * battle = spellMechanics->battle();
		battleState = std::shared_ptr<CBattleInfoCallback>(
			const_cast<CBattleInfoCallback *>(battle), [](CBattleInfoCallback *) {});
	}
	DamageCache damageCache;
	std::vector<size_t> mineOrder(target.size());
	for(size_t index = 0; index < mineOrder.size(); ++index)
		mineOrder[index] = index;
	std::sort(mineOrder.begin(), mineOrder.end(), [&](size_t left, size_t right)
	{
		return target[left].hexValue.toInt() < target[right].hexValue.toInt();
	});
	std::vector<std::vector<double>> enemyValues;
	enemyValues.reserve(enemies.size());

	for(const auto * enemy : enemies)
	{
		// `landMineEnemies` has already removed permanent/absolute immunity,
		// invincibility, zero damage, and 100% resistance.  Magic resistance below
		// remains a probabilistic trigger discount for the surviving targets.
		const int resistance = std::clamp(enemy->magicResistance(), 0, 100);
		const float resistanceFactor = 1.0f - static_cast<float>(resistance) / 100.0f;
		if(resistanceFactor <= 0.0f)
			continue;

		// Evaluate the target-specific damage before the health cap.  This keeps
		// spell damage reduction, protections and caster bonuses consistent with
		// the direct-damage path, then prevents overkill from making a tiny stack
		// look more valuable than the health it can actually lose.
		const auto damage = landMineDamagePotential(spellMechanics, enemy);
		if(damage == 0)
			continue;

		const auto directScale = AttackPossibility::calculateDamageReduce(
			nullptr, enemy, damage, damageCache, battleState);
		const auto enemyValue = directScale * resistanceFactor;
		std::vector<double> contributions;
		contributions.reserve(mineOrder.size());
		for(const auto index : mineOrder)
		{
			const auto triggerChance = landMineTriggerLikelihood(target[index].hexValue, enemy);
			contributions.push_back(static_cast<double>(enemyValue) * triggerChance);
		}
		enemyValues.push_back(std::move(contributions));
	}

	// Normalize row order as well as mine order so equivalent live snapshots
	// produce the same floating-point result even if their enumeration order
	// differs.
	std::sort(enemyValues.begin(), enemyValues.end(), [](const auto & left, const auto & right)
	{
		return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end());
	});
	return static_cast<float>(maximumWeightLandMineAssignment(enemyValues, mineOrder.size()));
}

float SpellTargetEvaluator::fireWallPlacementValue(const Mechanics * spellMechanics,
	const Target & target, std::shared_ptr<CBattleInfoCallback> battleState)
{
	if(!isCanonicalFireWall(spellMechanics) || target.size() != 3)
		return 0.0f;

	detail::ProblemImpl problem;
	if(!spellMechanics->canBeCastAt(target, problem))
		return 0.0f;

	if(!battleState)
	{
		const auto * battle = spellMechanics->battle();
		battleState = std::shared_ptr<CBattleInfoCallback>(
			const_cast<CBattleInfoCallback *>(battle), [](CBattleInfoCallback *) {});
	}

	const auto enemies = landMineEnemies(spellMechanics);
	const auto allies = landMineAllies(spellMechanics);
	if(enemies.empty())
		return 0.0f;

	DamageCache damageCache;
	float hostileValue = 0.0f;
	for(const auto * enemy : enemies)
	{
		if(!spellMechanics->isReceptive(enemy)
			|| enemy->hasImmunity(spellMechanics->getSpellId())
			|| enemy->hasAbsoluteImmunity(spellMechanics->getSpellId())
			|| enemy->isInvincible())
			continue;

		const int resistance = std::clamp(enemy->magicResistance(), 0, 100);
		const float resistanceFactor = 1.0f - static_cast<float>(resistance) / 100.0f;
		const float triggerChance = fireWallTriggerLikelihood(target, enemy);
		if(resistanceFactor <= 0.0f || triggerChance <= 0.0f)
			continue;

		const auto adjustedDamage = std::max<int64_t>(0, spellMechanics->adjustEffectValue(enemy));
		const auto damage = std::min<uint64_t>(static_cast<uint64_t>(adjustedDamage), enemy->getAvailableHealth());
		if(damage == 0)
			continue;

		const auto directScale = AttackPossibility::calculateDamageReduce(
			nullptr, enemy, damage, damageCache, battleState);
		hostileValue += directScale * triggerChance * resistanceFactor;
	}

	// Friendly units can also walk through a canonical wall.  Penalize an
	// exposed line more strongly than a hostile line is rewarded so the AI
	// chooses a safer orientation whenever one is available, and declines the
	// spell entirely when every useful line would endanger our army.
	float friendlyPenalty = 0.0f;
	for(const auto * ally : allies)
	{
		if(ally->isInvincible() || !spellMechanics->isReceptive(ally))
			continue;
		const float triggerChance = fireWallTriggerLikelihood(target, ally);
		if(triggerChance <= 0.0f)
			continue;

		const auto adjustedDamage = std::max<int64_t>(0, spellMechanics->adjustEffectValue(ally));
		const auto damage = std::min<uint64_t>(static_cast<uint64_t>(adjustedDamage), ally->getAvailableHealth());
		if(damage == 0)
			continue;

		const auto directScale = AttackPossibility::calculateDamageReduce(
			nullptr, ally, damage, damageCache, battleState);
		friendlyPenalty += directScale * triggerChance * 1.5f;
	}

	return std::max(0.0f, hostileValue - friendlyPenalty);
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
