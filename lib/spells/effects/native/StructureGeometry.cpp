/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "StructureCommon.h"

namespace spells::effects::native::structure
{
bool clearOfWalls(const CBattleInfoCallback & battle, BattleHex hex)
{
	if(!battle.hasFortifications()) return true;
	const auto part = battle.battleHexToWallPart(hex);
	if(part == EWallPart::INVALID) return true;
	if(part == EWallPart::INDESTRUCTIBLE_PART || part == EWallPart::INDESTRUCTIBLE_PART_OF_GATE
		|| part == EWallPart::BOTTOM_TOWER || part == EWallPart::UPPER_TOWER) return false;
	return static_cast<int>(battle.battleGetWallState(part)) <= 0;
}

bool hexAvailable(const CBattleInfoCallback & battle, BattleHex hex, bool mustBeClear)
{
	if(!hex.isAvailable()) return false;
	if(!mustBeClear) return true;
	if(battle.battleGetUnitByPos(hex, true)) return false;
	for(const auto & obstacle : battle.battleGetAllObstaclesOnPos(hex, false))
		if(obstacle->obstacleType != CObstacleInstance::MOAT) return false;
	return clearOfWalls(battle, hex);
}

std::array<int, GameConstants::BFIELD_SIZE> distances(BattleHex center)
{
	std::array<int, GameConstants::BFIELD_SIZE> result;
	result.fill(std::numeric_limits<int>::max());
	if(!center.isValid()) return result;
	result[center.toInt()] = 0;
	std::vector<BattleHex> queue{center};
	for(size_t cursor = 0; cursor < queue.size(); ++cursor)
	{
		const auto hex = queue[cursor];
		for(const auto direction : {BattleHex::TOP_LEFT, BattleHex::TOP_RIGHT, BattleHex::RIGHT,
			BattleHex::BOTTOM_RIGHT, BattleHex::BOTTOM_LEFT, BattleHex::LEFT})
		{
			const auto next = hex.cloneInDirection(direction, false);
			if(!next.isValid() || result[next.toInt()] != std::numeric_limits<int>::max()) continue;
			result[next.toInt()] = result[hex.toInt()] + 1;
			queue.push_back(next);
		}
	}
	return result;
}

BattleHexArray area(BattleHex center, int radius)
{
	BattleHexArray result;
	if(!center.isValid()) return result;
	std::bitset<GameConstants::BFIELD_SIZE> seen;
	seen.set(center.toInt());
	std::vector<std::pair<BattleHex, int>> queue{{center, 0}};
	for(size_t cursor = 0; cursor < queue.size(); ++cursor)
	{
		const auto [hex, distance] = queue[cursor];
		result.insert(hex);
		if(distance >= radius) continue;
		for(const auto direction : {BattleHex::TOP_LEFT, BattleHex::TOP_RIGHT, BattleHex::RIGHT,
			BattleHex::BOTTOM_RIGHT, BattleHex::BOTTOM_LEFT, BattleHex::LEFT})
		{
			const auto next = hex.cloneInDirection(direction, false);
			if(!next.isValid() || seen[next.toInt()]) continue;
			seen.set(next.toInt());
			queue.emplace_back(next, distance + 1);
		}
	}
	return result;
}

BattleHex firstHex(const Target & target)
{
	for(const auto & destination : target) if(destination.hexValue.isValid()) return destination.hexValue;
	return BattleHex::INVALID;
}
}
