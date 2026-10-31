/*
 * AttackPreviewTargets.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../../lib/battle/IBattleInfoCallback.h"
#include "../../lib/battle/HeroCommand.h"
#include <cstdint>
#include <memory>
#include <vector>
#include <optional>

class CBattleInfoCallback;
class HypotheticBattle;
class AttackPossibility;
class DamageCache;
class PhysicalStrikeProjection;

namespace attackPreview
{
/// Existing Rain of Arrows automatic collateral selection, with the captured
/// primary footprint and unchanged health/hex/identity tie order.
const battle::Unit * rainSecondary(const CBattleInfoCallback & battle,
	const battle::Unit * attacker, const battle::Unit * primary, const BattleHexArray & rainPrimaryFootprint);

/// Synchronous borrowed facts for the already admitted automatic Rain hit.
struct RainCollateralInput
{
	const battle::Unit * primary;
	const BattleHexArray & primaryFootprint;
	int64_t primaryDamage;
	bool physicalDamage;
	bool projectsTactics;
	bool rootMayHaveSerpents;
};
/// Complete the existing selected collateral hit and journal its damage/death.
void completeRainCollateral(AttackPossibility & candidate,
	const std::shared_ptr<CBattleInfoCallback> & state, const std::shared_ptr<HypotheticBattle> & preview,
	DamageCache & cache, PhysicalStrikeProjection & tactics, std::vector<uint32_t> & serpentDeaths,
	const RainCollateralInput & input);

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
