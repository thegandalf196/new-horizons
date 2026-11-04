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
/// An already begun deterministic terminal uses one ordinary castEval when its
/// Warcasting completion needs post-effect continuation, retaining current AI's
/// conditional resistance forecast. No new outcome enumeration or extra action.
bool acceptTerminalEffects(HypotheticBattle & state, BattleSide side, const PossibleSpellcast & candidate,
	uint32_t & targetId, const HypotheticBattle::ProjectedCounterspellOutcome & counterspell,
	bool negated, const HypotheticBattle::ProjectedSpellAllowance & prepared,
	const std::vector<uint32_t> & recipients);

/// Finite terminal routes that intentionally skip generic effect evaluation.
/// Keeps the established Counterspell/precomputed precedence and rejection policy.
bool projectNoEffectOrPrecomputedValue(PossibleSpellcast & ps, HypotheticBattle & state,
	BattleSide side, const float & baseline, const HypotheticBattle::ProjectedCounterspellOutcome & counterspell,
	bool counterspellNegated, const std::optional<HypotheticBattle::ProjectedSpellAllowance> & spellAllowance,
	const std::vector<uint32_t> & recipients, uint32_t & targetId);

/// Existing Nature Poison/Soul Chain terminal forecasts, in their original order.
bool projectDeferredDamageValue(PossibleSpellcast & ps, HypotheticBattle & state,
	BattleSide side, const float & baseline, const HypotheticBattle::ProjectedCounterspellOutcome & counterspell,
	bool counterspellNegated, const std::optional<HypotheticBattle::ProjectedSpellAllowance> & spellAllowance,
	const std::vector<uint32_t> & recipients, uint32_t & targetId);
/// Terminal projection of a precomputed signed expectation; false leaves the candidate untouched.
bool projectPrecomputedValue(PossibleSpellcast & ps, HypotheticBattle & state,
	BattleSide side, const float & baseline, const HypotheticBattle::ProjectedCounterspellOutcome & counterspell,
	bool counterspellNegated, const std::optional<HypotheticBattle::ProjectedSpellAllowance> & spellAllowance,
	const std::vector<uint32_t> & sharedPurposeSpellRecipients, uint32_t & targetId);
}
