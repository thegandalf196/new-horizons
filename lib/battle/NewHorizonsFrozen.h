/*
 * NewHorizonsFrozen.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "Unit.h"
#include "BattleUnitTurnReason.h"
#include <memory>
#include <optional>
#include <vector>

namespace battle { class CUnitState; }
struct BattleAttackInfo;

namespace newHorizonsFrozen
{
DLL_LINKAGE bool enabled(const JsonNode & rules);
DLL_LINKAGE int chancePercent(const JsonNode & rules);
DLL_LINKAGE int shatterBonusPercent(const JsonNode & rules);
DLL_LINKAGE bool isFreezingTouchAttacker(const battle::Unit & unit, const JsonNode & rules);
DLL_LINKAGE bool qualifiesForShatter(const BattleAttackInfo & attack, const JsonNode & rules);
DLL_LINKAGE Bonus marker(BonusSourceID sourceID, int32_t applicationRound);
DLL_LINKAGE Bonus makeFrozenMarker(BonusSourceID sourceID, int32_t applicationRound);
DLL_LINKAGE std::optional<int32_t> markerApplicationRound(const Bonus & bonus);
/// Validate the whole batch before applying effects; empty means no Frozen.
DLL_LINKAGE std::shared_ptr<battle::CUnitState> prepareApplication(const battle::Unit & unit,
	const std::vector<Bonus> & bonuses);
DLL_LINKAGE bool isFrozen(const battle::Unit & unit);
DLL_LINKAGE bool canApply(const battle::CUnitState & unit, int32_t round);
/// Removal never clears the recipient's application-round stamp.
DLL_LINKAGE std::vector<Bonus> removalPlan(const battle::Unit & unit);
/// Extra activations are blocked but do not thaw. The caller consumes this
/// normal queue slot as a no-op and removes the captured marker afterwards.
DLL_LINKAGE bool forfeitsNormalActivation(const battle::Unit & unit, BattleUnitTurnReason reason);
}
