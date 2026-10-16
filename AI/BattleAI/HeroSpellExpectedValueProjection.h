/*
 * HeroSpellExpectedValueProjection.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "HypotheticBattle.h"

#include <cstdint>
#include <optional>
#include <vector>

class PossibleSpellcast;

namespace heroSpellProjection
{
/// Terminal projection of a precomputed signed expectation; false leaves the candidate untouched.
bool projectPrecomputedValue(PossibleSpellcast & ps, HypotheticBattle & state,
	BattleSide side, const float & baseline, const HypotheticBattle::ProjectedCounterspellOutcome & counterspell,
	bool counterspellNegated, const std::optional<HypotheticBattle::ProjectedSpellAllowance> & spellAllowance,
	const std::vector<uint32_t> & sharedPurposeSpellRecipients, uint32_t & targetId);
}
