/*
 * PreemptiveAttackRequest.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../../lib/battle/BattleAttackInfo.h"
#include <optional>

class CBattleInfoCallback;
namespace battle { class CUnitState; }

namespace attackPreview
{
/// Native makeAttack order: Battlecraft, Brace, then the existing Bulwark stage.
enum class PreemptiveStage { BATTLECRAFT, BRACE };

/// Current branch-local actor/recipient and flags, not a damage/utility result.
/// Both unit views and callback must outlive this synchronous request's use.
struct PreemptiveRequest
{
	BattleAttackInfo attack;
	bool reactiveForLastStand = true;
	bool brace = false;
};

/// Called lazily for each stage AFTER previous reactions at current positions.
/// No forecast of movement, copied coefficient, sampled RNG or completed action.
/// attackIndex is the ORIGINAL fixed loop; only index0 retains its movement.
std::optional<PreemptiveRequest> preparePreemptiveStage(PreemptiveStage stage,
	const CBattleInfoCallback & battle, const BattleAttackInfo & incoming,
	bool physicalCreatureAttack,
	battle::CUnitState & movedAttacker, battle::CUnitState & currentDefender,
	int attackIndex, int currentRound);
}
