/*
 * HeroTargetHeuristicProjection.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "HypotheticBattle.h"

#include <cstdint>
#include <optional>
#include <vector>

class PossibleSpellcast;

namespace heroSpellProjection
{
/// Terminal one-unit forecast: reject target/negation before accepting the detached action.
/// Borrow the value after the caller's canonical-spell and positive-value gates; read it after acceptance.
void projectTargetHeuristic(PossibleSpellcast & ps, const float & forecast,
	HypotheticBattle & state, BattleSide side, const float & baseline,
	const HypotheticBattle::ProjectedCounterspellOutcome & counterspell, bool counterspellNegated,
	const std::optional<HypotheticBattle::ProjectedSpellAllowance> & spellAllowance,
	const std::vector<uint32_t> & sharedPurposeSpellRecipients, uint32_t & targetId);
}
