/*
 * OrientedSpellPattern.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "OrientedSpellPattern.h"

namespace spells
{
BattleHexArray makeOrientedSpellPath(const BattleHex & origin, BattleHex::EDir direction,
	std::span<const int> relativeDirections)
{
	constexpr auto directions = BattleHex::hexagonalDirections();
	constexpr int directionCount = static_cast<int>(directions.size());
	const auto directionIt = std::find(directions.begin(), directions.end(), direction);

	if(directionIt == directions.end() || !origin.isAvailable())
		return {};

	const int initialDirection = static_cast<int>(std::distance(directions.begin(), directionIt));
	BattleHexArray path;
	path.insert(origin);
	BattleHex current = origin;

	for(const int relativeDirection : relativeDirections)
	{
		const int directionOffset = relativeDirection % directionCount;
		const int nextDirection = (initialDirection + directionOffset + directionCount) % directionCount;
		const BattleHex next = current.cloneInDirection(directions[nextDirection], false);

		if(!next.isAvailable() || path.contains(next))
			return {};

		path.insert(next);
		current = next;
	}

	return path;
}

BattleHex::EDir adjacentSpellDirection(const BattleHex & origin, const BattleHex & endpoint)
{
	return BattleHex::mutualPosition(origin, endpoint);
}
}
