/*
 * NewHorizonsProtectLink.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#pragma once

#include <cstdint>
#include <optional>

#include "../../lib/battle/BattleHex.h"
#include "../../lib/battle/BattleSide.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/battle/Unit.h"

namespace newHorizonsProtectLink
{
struct Link
{
	uint32_t protectorUnitId = 0;
	uint32_t wardUnitId = 0;
	BattleHex protectorHead = BattleHex::INVALID;
	BattleHex protectorRear = BattleHex::INVALID;
	BattleHex wardHead = BattleHex::INVALID;
	BattleHex wardRear = BattleHex::INVALID;

	bool operator==(const Link &) const = default;
};

/// Returns the current effective Protect pair without inspecting hero visibility or mutating battle state.
inline std::optional<Link> activeLink(const CBattleInfoCallback & battle, BattleSide side)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return std::nullopt;

	const auto order = battle.battleGetHeroOrderState(side, HeroCommand::PROTECT);
	if(!order)
		return std::nullopt;

	const auto * protector = battle.battleGetUnitByID(order->primaryTargetUnitId);
	const auto * ward = battle.battleGetUnitByID(order->secondaryTargetUnitId);
	if(!protector || !ward
		|| !battle.battleOrderBenefitAppliesTo(*order, side, protector)
		|| !battle.battleOrderBenefitAppliesTo(*order, side, ward))
		return std::nullopt;

	Link result;
	result.protectorUnitId = protector->unitId();
	result.wardUnitId = ward->unitId();
	result.protectorHead = protector->getPosition();
	result.protectorRear = protector->doubleWide() ? protector->occupiedHex() : BattleHex::INVALID;
	result.wardHead = ward->getPosition();
	result.wardRear = ward->doubleWide() ? ward->occupiedHex() : BattleHex::INVALID;
	if(!result.protectorHead.isAvailable() || !result.wardHead.isAvailable()
		|| (protector->doubleWide() && !result.protectorRear.isAvailable())
		|| (ward->doubleWide() && !result.wardRear.isAvailable()))
		return std::nullopt;

	return result;
}
}
