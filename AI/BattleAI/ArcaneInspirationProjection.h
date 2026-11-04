/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/ArcaneReprisal.h"
#include "../../lib/constants/EntityIdentifiers.h"
#include <memory>
#include <set>
#include <span>
#include <utility>
#include <vector>
class CBattleInfoCallback;
class DamageCache;
class HypotheticBattle;
class StackWithBonuses;
namespace arcaneInspirationAI
{
bool needsStrikeProjection(const battle::Unit * actor, std::span<const battle::Unit * const> targets);
bool needsReprisalProjection(const battle::Unit * actor, const battle::Unit * target);
class StrikeProjection
{
	HypotheticBattle * branch;
	struct Pending { uint32_t actor; arcaneInspiration::Reprisal receipt; };
	struct Observation
	{
		std::shared_ptr<StackWithBonuses> before;
		PlayerColor owner;
		int64_t damage;
	};
	std::vector<Pending> pending;
	std::set<uint32_t> struckActors;
	std::vector<Observation> observations;
public:
	explicit StrikeProjection(HypotheticBattle * value) : branch(value) {}
	std::optional<arcaneInspiration::Reprisal> prepare(const BattleAttackInfo & attack, int64_t beforeShieldDamage);
	void record(std::optional<arcaneInspiration::Reprisal> receipt, int64_t postShieldDamage);
	void finish(uint32_t actor);
	/// Existing signed exchange units, friendly cost then hostile benefit.
	/// Selected replay applies these effects without adding this utility twice.
	std::pair<float, float> value(DamageCache &, const std::shared_ptr<CBattleInfoCallback> &, PlayerColor reference) const;
};
}
