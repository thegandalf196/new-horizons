/*
 * AttackPreviewTargets.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../../lib/battle/IBattleInfoCallback.h"
#include "../../lib/battle/HeroCommand.h"
#include <memory>
#include <optional>

class CBattleInfoCallback;
class HypotheticBattle;

namespace attackPreview
{
struct Targets
{
	battle::Units resolved;
	battle::Units requested;
	battle::Units counter;
};

/// Commit the same captured Protect interception in model and legacy previews.
void consumeProtect(const std::shared_ptr<HypotheticBattle> & model, const CBattleInfoCallback & source,
	BattleSide side, const battle::Unit * requested, const battle::Unit * actual, const std::optional<HeroOrderState> & captured,
	uint8_t & legacyConsumed);

/// Primary recipients may be redirected by Protect; splash follows the selected
/// contact. A remote primary counter does not acquire adjacent splash targets.
Targets targets(const CBattleInfoCallback & battle, const battle::Unit * attacker,
	const battle::Unit * requested, const battle::Unit * resolved,
	BattleHex from, BattleHex target, bool shooting, bool longReach);
}
