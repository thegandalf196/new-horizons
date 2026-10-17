/*
 * SecondWindOpportunity.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../../lib/battle/BattleSide.h"
#include <cstdint>
#include <memory>

class CBattleInfoCallback;
class Environment;

namespace heroOrderValue
{
/// A genuine immediate Second Wind activation, in a child of the SAME visible
/// source. Absence means this bounded forecast is incomplete: keep the old
/// damage prior, never interpret missing context as an illegal activation.
/// The caller keeps environment alive while using the returned read-only view.
std::shared_ptr<const CBattleInfoCallback> projectSecondWindActivation(const CBattleInfoCallback & battle,
	const Environment * environment, const std::shared_ptr<CBattleInfoCallback> & source,
	BattleSide side, uint32_t recipientId);

/// Physical action opportunity only. Does not estimate damage, mutate the
/// recipient, spend an action, or choose among spell/Order alternatives.
bool hasSecondWindAttackOpportunity(const CBattleInfoCallback & activation,
	uint32_t recipientId, uint32_t enemyId);
}
