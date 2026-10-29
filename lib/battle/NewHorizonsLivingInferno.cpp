/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include <algorithm>
#include <limits>
#include "NewHorizonsLivingInferno.h"
#include "CBattleInfoCallback.h"
#include "Unit.h"
#include "../entities/hero/NewHorizonsLivingInferno.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../CCreatureHandler.h"
#include "../spells/CSpell.h"
namespace
{
int floorRatio(int numerator, int denominator)
{
	const int quotient = numerator / denominator;
	return quotient - (numerator % denominator < 0 ? 1 : 0);
}
BattleHex roundedHex(int a, int b, int c, int denominator)
{
	BattleHex result;
	int best = std::numeric_limits<int>::max();
	for(const int x : {floorRatio(a, denominator), floorRatio(a, denominator) + 1})
		for(const int z : {floorRatio(c, denominator), floorRatio(c, denominator) + 1})
		{
			const int y = -x - z;
			if(y < floorRatio(b, denominator) || y > floorRatio(b, denominator) + 1)
				continue;
			const int row = -z;
			const int column = x - row / 2;
			if(row < 0 || row >= GameConstants::BFIELD_HEIGHT || column <= 0 || column >= GameConstants::BFIELD_WIDTH - 1)
				continue;
			const BattleHex candidate(column, row);
			const int dx = x * denominator - a, dy = y * denominator - b, dz = z * denominator - c;
			const int distance = dx * dx + dy * dy + dz * dz;
			if(distance < best || (distance == best && (!result.isValid() || candidate < result)))
			{
				best = distance;
				result = candidate;
			}
		}
	return result;
}
BattleHexArray coveredByFireWalls(const CBattleInfoCallback & battle)
{
	BattleHexArray result;
	for(const auto & existing : battle.battleGetAllObstacles())
		if(const auto * wall = dynamic_cast<const SpellCreatedObstacle *>(existing.get());
			wall && SpellID(wall->ID) == SpellID::FIRE_WALL)
			result.insert(wall->getAffectedTiles());
	return result;
}
}
namespace newHorizonsLivingInferno
{
BattleHexArray flightPath(BattleHex from, BattleHex destination)
{
	BattleHexArray result;
	if(!from.isAvailable() || !destination.isAvailable())
		return result;
	const int steps = BattleHex::getDistance(from, destination);
	if(steps == 0)
		return result;
	const int a = from.getX() + from.getY() / 2, c = -from.getY(), b = -a - c;
	const int da = destination.getX() + destination.getY() / 2 - a;
	const int dc = -destination.getY() - c, db = -da - dc;
	for(int step = 0; step <= steps; ++step)
		result.insert(roundedHex(a * steps + da * step, b * steps + db * step, c * steps + dc * step, steps));
	return result;
}
void Trail::traverse(BattleHex from, const BattleHexArray & committed, bool flying)
{
	if(damage <= 0 || committed.empty())
		return;
	visited.insert(from);
	if(flying)
		visited.insert(flightPath(from, committed.back()));
	else
		visited.insert(committed);
}
Trail capture(const CBattleInfoCallback & battle, const battle::Unit * source, bool voluntary)
{
	Trail result;
	if(!voluntary || !source || !source->alive() || source->isGhost() || source->isTimeStopped())
		return result;
	const auto * creature = source->creatureId().toCreature();
	if(!creature || (creature->getJsonKey() != "core:fireElemental" && creature->getJsonKey() != "core:energyElemental"))
		return result;
	const auto * hero = battle.battleGetOwnerHero(source);
	if(!newHorizonsHeroes::hasLivingInferno(hero))
		return result;
	const auto policy = newHorizonsHeroes::livingInfernoRules(hero->getPrimaryGrowthRules());
	result.damage = static_cast<int>(std::min<int64_t>(std::numeric_limits<int>::max(),
		static_cast<int64_t>(source->getCount()) * policy->damagePerCreature));
	result.rounds = policy->durationRounds;
	result.side = source->unitSide();
	result.casterSide = battle.playerToSide(battle.battleGetOwner(source));
	result.doubleWide = source->doubleWide();
	return result;
}
std::optional<SpellCreatedObstacle> obstacle(const CBattleInfoCallback & battle,
	const Trail & trail, BattleHex finalPosition)
{
	if(trail.damage <= 0 || trail.visited.empty())
		return std::nullopt;
	SpellCreatedObstacle wall;
	const auto alreadyBurning = coveredByFireWalls(battle);
	const auto finalFootprint = battle::Unit::getHexes(finalPosition, trail.doubleWide, trail.side);
	for(const auto position : trail.visited)
		for(const auto hex : battle::Unit::getHexes(position, trail.doubleWide, trail.side))
			if(hex.isAvailable() && !finalFootprint.contains(hex) && !alreadyBurning.contains(hex))
				wall.customSize.insert(hex);
	if(wall.customSize.empty())
		return std::nullopt;
	wall.pos = wall.customSize.front();
	wall.ID = SpellID::FIRE_WALL;
	wall.uniqueID = battle.nextObstacleId();
	wall.turnsRemaining = trail.rounds;
	wall.casterSide = trail.casterSide;
	wall.damageSnapshot = true;
	wall.minimalDamage = trail.damage;
	wall.passable = true;
	wall.renderAtEachHex = true;
	wall.nativeVisible = false;
	wall.trigger = SpellID(SpellID::decode("core:fireWallTrigger"));
	wall.appearAnimation = AnimationPath::builtin("C07SPF60");
	wall.animation = AnimationPath::builtin("C07SPF61");
	wall.removalAnimation = AnimationPath::builtin("C07SPF62");
	return wall;
}
}
