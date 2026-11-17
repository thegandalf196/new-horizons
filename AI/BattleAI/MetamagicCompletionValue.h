/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/spells/NewHorizonsMetamagicCompletion.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include <memory>
#include <optional>
class HypotheticBattle;
class DamageCache;
class Environment;
class CSpell;
class CGHeroInstance;
namespace metamagicAI
{
struct Observation
{
	newHorizonsMetamagic::RecipientSnapshots before;
	std::vector<uint32_t> resolved;
	std::vector<BattleStackAttacked> injuries;
	std::map<uint32_t, float> applicationChance;
	std::map<uint32_t, PlayerColor> originalOwners;
};
Observation observeBefore(const HypotheticBattle &, const spells::Mechanics &);
void observeAfter(Observation &, const spells::Mechanics &);
/// Conditional local heuristic: independent status marginals plus one capped
/// payout expectation, in existing signed combat utility. Fixed attacks only;
/// no additional action selection, queue depth, full-outcome forecasts or RNG.
/// Discharge defense correlation and future Memory/refund value are unmodeled.
std::optional<float> completionValue(const Environment *, const std::shared_ptr<HypotheticBattle> &,
	const CGHeroInstance *, const CSpell *, const Observation &, const DamageCache &,
	float positiveEffectMultiplier, float negativeEffectMultiplier);
}
