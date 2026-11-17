/*
 * ArcaneOverflowValue.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../../Global.h"
#include <functional>
#include <optional>

namespace battleSearch { class SearchBudget; }
namespace newHorizonsMetamagic { struct OverflowPreview; }

namespace arcaneOverflowAI
{
enum class Outcome { PrimaryResisted, PrimaryOnly, BothLand };

/// Unweighted signed immediate/local utility of ONE accepted original cast.
/// The caller uses the native conditional event path and the existing local
/// outcome kernel. This callback must not select another action/continuation,
/// reuse a branch modified by another outcome, or weight resistance itself.
/// Missing means uncertified/incomplete forecast, never illegal or zero utility.
struct LocalUtility
{
	float immediate = 0;
	float completion = 0;
	LocalUtility(float immediate = 0, float completion = 0) : immediate(immediate), completion(completion) {}
};
using LocalValue = std::function<std::optional<LocalUtility>(Outcome)>;

/// Full local replacement for an attested native Overflow preview. Ordinary
/// candidate admission/pricing and the existing continuation remain caller-owned.
/// Charges all nonzero finite outcome visits before the first native effect.
/// Callback allocations/effects additionally use this same root reservation.
/// No battle/RNG access, damage formula, target search or exception fallback.
DLL_LINKAGE std::optional<LocalUtility> expectedLocalValue(
	const newHorizonsMetamagic::OverflowPreview &, battleSearch::SearchBudget &, const LocalValue &);
/// The already materialized native seed is scored only for positive BothLand
/// mass. Other callbacks may materialize at most the two remaining native worlds.
DLL_LINKAGE std::optional<LocalUtility> expectedSeededLocalValue(
	const newHorizonsMetamagic::OverflowPreview &, battleSearch::SearchBudget &,
	const std::function<std::optional<LocalUtility>()> & seedValue, const LocalValue & otherValue);

}
