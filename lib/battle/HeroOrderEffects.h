/*
 * HeroOrderEffects.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "BattleSide.h"
#include "FocusFireState.h"
#include "../bonuses/Bonus.h"

#include <optional>
#include <utility>
#include <vector>

class CBattleInfoCallback;
class IBattleState;

namespace heroCommands
{
/// Value snapshot captured before action-boundary cleanup or action completion.
/// Carries no payment, RNG draw, continuation or new serialized representation.
struct DLL_LINKAGE PreparedOrderEffects
{
	BattleSide side = BattleSide::NONE;
	HeroOrderState order;
	std::optional<FocusFireState> focusFire;
	bool operator==(const PreparedOrderEffects &) const = default;
};

struct DLL_LINKAGE IssuedOrderGrants
{
	using Batch = std::vector<std::pair<uint32_t, std::vector<Bonus>>>;
	/// Native packet ordering: Enchanted Command, Vengeance, Hold Fast.
	std::vector<Batch> batches;
	size_t holdFastRecipientCount = 0;
};

/// Read-only: delegates legality and captured formula state to the canonical callback.
DLL_LINKAGE std::optional<PreparedOrderEffects> prepareOrderEffects(
	const CBattleInfoCallback & battle, BattleSide side, HeroCommand command,
	const std::vector<uint32_t> & targets);
/// Called after the accepted Order has been installed and Shared Purpose applied.
DLL_LINKAGE IssuedOrderGrants prepareIssuedOrderGrants(const CBattleInfoCallback & battle,
	BattleSide side, const HeroOrderState & order);
DLL_LINKAGE void applyIssuedOrderGrants(IBattleState & state, const IssuedOrderGrants & grants);
}
