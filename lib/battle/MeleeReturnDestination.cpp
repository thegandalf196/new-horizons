/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "MeleeReturnDestination.h"
#include "CBattleInfoCallback.h"
#include "Unit.h"
#include "BattleHexArray.h"
namespace battleMovement
{
std::optional<BattleHex> meleeReturnDestination(const CBattleInfoCallback & battle,
	const battle::Unit & actor, BattleHex origin, int movementBudget)
{
	const auto path = battle.getPath(actor.getPosition(), origin, &actor);
	const auto reachability = battle.getReachability(&actor);
	const auto destination = std::find_if(path.first.begin(), path.first.end(), [&](const BattleHex & hex)
	{
		return reachability.isReachable(hex)
			&& reachability.distances[hex.toInt()] <= static_cast<uint32_t>(std::max(0, movementBudget));
	});
	return destination == path.first.end() ? std::nullopt : std::make_optional(*destination);
}
}
