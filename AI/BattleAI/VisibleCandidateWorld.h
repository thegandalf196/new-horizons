/*
 * VisibleCandidateWorld.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../../lib/battle/VisibleHeroPolicy.h"
#include <memory>

class HypotheticBattle;
class AttackPossibility;
class DamageCache;
class BattleHex;
namespace battle { class Unit; }

/// Owns the actual ordinary candidate's callback/query-provider interval. Does
/// not invent native ownership, copy hidden metadata, parallel graph or lease.
/// Environment/library and mutable native aliases keep their existing caller
/// contract. Knowledge is queried lazily, never cached across mutations.
class VisibleCandidateWorld final
{
	std::shared_ptr<HypotheticBattle> source;
public:
	explicit VisibleCandidateWorld(std::shared_ptr<HypotheticBattle> source);
	VisibleHeroPolicy heroPolicy(BattleSide side) const;
	const std::shared_ptr<HypotheticBattle> & callback() const { return source; }
	static AttackPossibility evaluate(const std::shared_ptr<const VisibleCandidateWorld> & owner,
		const battle::Unit * attacker, const battle::Unit * defender, int distance,
		bool shooting, const BattleHex & from, DamageCache & damageCache,
		int validatedVoluntaryFlightDistance = 0, bool validatedVoluntaryApproach = false);
};
