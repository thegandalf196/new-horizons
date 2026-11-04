/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/PassiveDivineMandate.h"
#include <functional>
#include <optional>
#include <span>

namespace passiveDivineMandateAI
{
/// A marginal signed exchange value relative to the same post-Order parent.
/// The provider must project exactly one native automatic cast, never a future
/// Hero decision or random sequence. Absence means unsupported, not worthless.
using OutcomeValue = std::function<std::optional<float>(
	const newHorizonsPassiveDivineMandate::SpellChoice &,
	const newHorizonsPassiveDivineMandate::TargetChoice &)>;
struct Workload
{
	size_t spells = 0;
	size_t outcomes = 0;
	size_t evaluatedOutcomes = 0;
	size_t uncertainAutomaticBenefit = 0;
};
/// Uniform spell draw followed by uniform target draw within that spell.
/// No pool filtering, maximization, RNG or probability-mass renormalization.
/// Empty native pool has exactly zero marginal value and invokes no provider.
std::optional<float> expectedOneDraw(
	std::span<const newHorizonsPassiveDivineMandate::SpellChoice> pool,
	const OutcomeValue & outcomeValue, Workload * workload = nullptr);
}
