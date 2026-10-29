/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "BattleHexArray.h"
#include "CObstacleInstance.h"
#include <optional>
class CBattleInfoCallback;
namespace battle { class Unit; }
namespace newHorizonsLivingInferno
{
/// Flight has only committed start/end positions. Integer cube interpolation
/// projects that straight trajectory; equal distances choose the lowest hex ID.
DLL_LINKAGE BattleHexArray flightPath(BattleHex from, BattleHex destination);
struct DLL_LINKAGE Trail
{
	int damage = 0;
	int rounds = 1;
	BattleSide side = BattleSide::NONE; // Physical footprint orientation never follows control.
	BattleSide casterSide = BattleSide::NONE; // Existing allegiance-owner hero supplies the policy.
	bool doubleWide = false;
	BattleHexArray visited;
	/// Ground input is the existing committed packet order, never a new path.
	void traverse(BattleHex from, const BattleHexArray & committed, bool flying);
};
DLL_LINKAGE Trail capture(const CBattleInfoCallback & battle, const battle::Unit * source, bool voluntary);
/// One grouped Fire Wall descriptor; no RNG, cast, authoritative mutation or
/// pathfinding. Existing walls are omitted and the final footprint stays clear.
DLL_LINKAGE std::optional<SpellCreatedObstacle> obstacle(const CBattleInfoCallback & battle,
	const Trail & trail, BattleHex finalPosition);
}
