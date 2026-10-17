/*
 * AttackPreviewTargets.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../../lib/battle/IBattleInfoCallback.h"

class CBattleInfoCallback;

namespace attackPreview
{
struct Targets
{
	battle::Units resolved;
	battle::Units requested;
	battle::Units counter;
};

/// Primary recipients may be redirected by Protect; splash follows the selected
/// contact. A remote primary counter does not acquire adjacent splash targets.
Targets targets(const CBattleInfoCallback & battle, const battle::Unit * attacker,
	const battle::Unit * requested, const battle::Unit * resolved,
	BattleHex from, BattleHex target, bool shooting, bool longReach);
}
