/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/ConsecratedArms.h"
#include "../../lib/battle/BattleSide.h"
#include <functional>
#include <memory>
#include <span>
#include <array>
class HypotheticBattle;
class Environment;
class DamageCache;
struct HeroOrderState;
namespace consecratedArmsAI
{
using OutcomeValue = std::function<std::optional<float>(std::span<const consecratedArms::Prayer>)>;
struct Exposure
{
	double outgoing = 0, incoming = 0, retaliation = 0;
	int64_t count = 0, firstHP = 0, maximumHP = 0;
	int movement = 1;
	int moraleRoom = 20, luckRoom = 20;
	bool moraleEligible = true;
};
using Prayers = std::array<std::optional<consecratedArms::ActivePrayer>, 6>;
/// Exactly six uniform singletons or fifteen uniform unordered pairs. Never
/// filter useless/already active prayers or select a preferred random result.
/// Unknown positive-mass outcomes remain unknown without renormalization.
DLL_LINKAGE std::optional<float> expectedPrayers(const consecratedArms::Policy &, const OutcomeValue &);
/// Arithmetic exposure heuristic only: no forecast, RNG, action selection or
/// shield refill. Future shield acquisition, control and magical Judgment
/// exposure remain unpriced; the caller must report this partial estimate.
DLL_LINKAGE float expectedRecipientValue(const consecratedArms::Policy &, int actualFaith,
	const Prayers &, const Exposure &);
/// Reuses visible current-source damage cache. At most two pair damage queries
/// per recipient; six/fifteen pure arithmetic outcomes, one ordinary continuation.
std::optional<float> projectedValue(const Environment *, const std::shared_ptr<HypotheticBattle> &,
	BattleSide, const HeroOrderState &, const consecratedArms::Policy &, const DamageCache &, float positive, float negative);
}
