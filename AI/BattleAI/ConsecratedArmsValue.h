/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/ConsecratedArms.h"
#include "../../lib/battle/BattleSide.h"
#include <functional>
#include <memory>
#include <span>
#include <array>
#include <vector>
class HypotheticBattle;
class BattleAction;
class CBattleInfoCallback;
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
	/// Chosen Prayers only: [0,1] chance hostile non-damaging magic targets this side.
	double enemySpellThreat = 0;
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
/// Chosen Prayers (version 4): the heuristic value of granting `selected` to one
/// recipient. Extends the exposure terms with Heaven's Judgment (outgoing and
/// retaliation damage), Vicarious Redemption (half-credited shield from incoming
/// casualties) and Steadfast Soul (resisted hostile non-damaging magic).
DLL_LINKAGE float chosenRecipientValue(const consecratedArms::Policy &, int actualFaith,
	const Prayers &, const Exposure &, std::span<const consecratedArms::Prayer> selected);
struct Choice
{
	uint32_t recipient = 0;
	std::vector<consecratedArms::Prayer> prayers;
	float value = 0;
};
struct RecipientExposure
{
	uint32_t recipient = 0;
	Prayers before;
	Exposure exposure;
};
/// Argmax over (prayer or unordered prayer pair) x recipient. Strictly greater
/// values win, so ties keep the lowest recipient order, then the lowest prayers.
DLL_LINKAGE std::optional<Choice> bestChoice(const consecratedArms::Policy &, int actualFaith,
	std::span<const RecipientExposure> recipients);
/// Fills an empty version 4 choice on an Order action from cheap exposure on
/// `state` (the decision's current-source board). Falls back to the first
/// eligible recipient and lowest prayers when no exposure is measurable. No-op
/// when the action needs no choice or already carries one.
DLL_LINKAGE void applyChosenPrayers(const CBattleInfoCallback & live, const std::shared_ptr<HypotheticBattle> & state,
	const DamageCache & damage, BattleAction & action);
/// Reuses visible current-source damage cache. At most two pair damage queries
/// per recipient; six/fifteen pure arithmetic outcomes, one ordinary continuation.
std::optional<float> projectedValue(const Environment *, const std::shared_ptr<HypotheticBattle> &,
	BattleSide, const HeroOrderState &, const consecratedArms::Policy &, const DamageCache &, float positive, float negative);
}
