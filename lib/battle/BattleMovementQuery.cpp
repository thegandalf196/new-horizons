/*
 * BattleMovementQuery.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleMovementQuery.h"
#include "BattleHexArray.h"
#include "Unit.h"
#include <ranges>

namespace
{
struct BattleGeometry
{
	auto neighbours(std::int16_t position) const
	{
		return BattleHex(position).getNeighbouringTiles()
			| std::views::transform([](BattleHex hex) { return hex.toInt(); });
	}

	auto occupied(std::int16_t position, std::uint8_t footprint) const
	{
		const auto side = footprint == 1 ? BattleSide::ATTACKER : BattleSide::DEFENDER;
		return battle::Unit::getHexes(BattleHex(position), footprint != 0, side)
			| std::views::transform([](BattleHex hex) { return hex.toInt(); });
	}

	std::uint32_t distance(std::int16_t from, std::int16_t to) const
	{
		return BattleHex::getDistance(BattleHex(from), BattleHex(to));
	}
};
}

battle::MovementPaths battle::solveMovementQuery(const MovementQuery & query)
{
	return movement::solve(query, BattleGeometry{});
}
