/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "MetamagicExpectedValue.h"
#include <map>
#include <set>
#include <limits>
#include <tuple>
namespace metamagicAI
{
std::optional<PoolExpectation> expectedPool(
	std::span<const newHorizonsMetamagic::RemovalCandidate> effects,
	const newHorizonsMetamagic::ResolutionPolicy & policy, int rank, bool halon,
	newHorizonsMetamagic::ResolutionKind kind, int64_t capacity,
	const std::function<int64_t(int64_t)> & adjustCombinedPool)
{
	constexpr size_t maximumEffects = 64;
	constexpr size_t maximumOutcomes = 4096;
	if(effects.size() > maximumEffects || capacity < 0) return std::nullopt;
	const double success = newHorizonsMetamagic::removalChancePercent(policy, rank, halon) / 100.0;
	PoolExpectation result;
	if(success == 0) return result;
	std::map<int64_t, double> distribution{{0, 1}};
	std::set<std::tuple<SpellID, PlayerColor, std::string>> identities;
	std::optional<uint32_t> recipient;
	for(const auto & effect : effects)
	{
		if(!recipient) recipient = effect.recipient;
		if(*recipient != effect.recipient || effect.listedMana < 0) return std::nullopt;
		// A chain revisit or multiple records cannot earn a second independent roll.
		if(!identities.emplace(effect.family, effect.caster, effect.statusIdentity).second) continue;
		result.removals += success;
		const auto payout = kind == newHorizonsMetamagic::ResolutionKind::OFFENSE
			? newHorizonsMetamagic::energyHitPoints(effect.listedMana, policy.damagePerMana)
			: newHorizonsMetamagic::energyHitPoints(effect.listedMana, policy.restorationPerMana);
		std::map<int64_t, double> next;
		for(const auto & [amount, probability] : distribution)
		{
			if(success < 1) next[amount] += probability * (1 - success);
			const auto total = amount + std::min(payout, std::numeric_limits<int64_t>::max() - amount);
			next[total] += probability * success;
			if(next.size() > maximumOutcomes) return std::nullopt;
		}
		distribution = std::move(next);
	}
	for(const auto & [amount, probability] : distribution)
	{
		const auto adjusted = adjustCombinedPool ? adjustCombinedPool(amount) : amount;
		result.hitPoints += std::clamp<int64_t>(adjusted, 0, capacity) * probability;
	}
	return result;
}
}
