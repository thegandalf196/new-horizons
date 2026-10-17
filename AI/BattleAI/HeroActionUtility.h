/*
 * HeroActionUtility.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../../lib/constants/EntityIdentifiers.h"

#include <memory>
#include <optional>

class BattleExchangeEvaluator;
class DamageCache;
class HypotheticBattle;
class PossibleSpellcast;
namespace battle { class Unit; }

namespace heroActionUtility
{
/// Signed immediate terms already present in the generic spell model. These
/// are NOT raw canonical Order priors or a replacement for a forecast horizon.
/// Field order retains the caller's original initialization/query sequence.
struct ImmediateSignedUtility
{
	float toHostiles;
	float toFriendlies;
	float initiative;
};

/// Existing post-action continuation score, absolute rather than a change from
/// the no-cast baseline. It may carry the existing INEFFECTIVE sentinel.
struct AbsoluteContinuationUtility { float value; };
struct AbsoluteHeroActionUtility { float value; };

/// Call-local nonowning subject. Generic spells pass a null measurement actor;
/// calculateDamageReduce then chooses its existing opposing reference actor.
/// Preserve before-victim identity even when the projected victim has died.
struct HealthDeltaSubject
{
	const battle::Unit * measurementActor;
	const battle::Unit * beforeVictim;
	const battle::Unit * projectedVictim;
	int64_t beforeHealth;
	int64_t projectedHealth;
	bool phantomArmyStack;
};

struct HealthDeltaUtility { float dpsReduce; };

/// Call only for a changed HP pair, after the caller's spell-specific effects.
/// Adds directly to the existing accumulators, retaining +=/-= FP grouping and
/// lazy owner/resistance/magical checks and evaluator weight queries. Absence
/// means the original loop continued (spawn handled or magical healing skipped),
/// not zero utility; no callback/provider/input lifetime is retained.
std::optional<HealthDeltaUtility> accumulateHealthDelta(const HealthDeltaSubject & subject,
	const PossibleSpellcast & ps, PlayerColor playerID, DamageCache & innerCache,
	const std::shared_ptr<HypotheticBattle> & state, const BattleExchangeEvaluator & scoreEvaluator,
	ImmediateSignedUtility & immediate);

/// Same left-associated expression as the generic selector. No baseline or raw
/// Order scalar can be supplied; immediate effects and continuation enter once.
AbsoluteHeroActionUtility composeAbsoluteUtility(AbsoluteContinuationUtility continuation,
	const ImmediateSignedUtility & immediate);
}
