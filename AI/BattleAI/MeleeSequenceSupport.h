/*
 * MeleeSequenceSupport.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "OriginalMeleeSequence.h"

namespace meleeSequenceSupport
{
std::optional<heroOrderValue::MeleeSequenceStop> unavailablePolicies(const CBattleInfoCallback & battle);
/// Closed supported deterministic policy scope, not gameplay legality/pruning.
std::optional<heroOrderValue::MeleeSequenceStop> unavailable(HypotheticBattle & battle,
	const battle::Unit & actor, const battle::Unit & victim, BattleHex selectedTargetHex);
}
