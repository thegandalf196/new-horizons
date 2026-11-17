/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/CreatureAbilityChance.h"
#include <array>
namespace nominalAbilityChance
{
/// Candidate-local nominal mass: failure/free, success/free, failure/spent, success/spent.
/// This is not native biased-RNG prediction or canonical unit/resource state.
using Outcomes = std::array<double, 4>;
Outcomes harmful(const creatureAbilityChance::Rounding & chance, bool rerollAvailable);
/// One conditional prevention check after the existing proc/reroll. Retains
/// the same four masses and the already resolved adverse-roll availability.
DLL_LINKAGE Outcomes afterPrevention(Outcomes, int resistanceBasisPoints);
double success(const creatureAbilityChance::Rounding & chance, double rerollAvailability);
double availableAfter(const creatureAbilityChance::Rounding & chance, double rerollAvailability);
}
