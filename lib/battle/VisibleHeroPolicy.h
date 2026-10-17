/*
 * VisibleHeroPolicy.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "BattleSide.h"

class CGHeroInstance;
class CBattleInfoEssentials;

/// Availability/identity in one REAL callback perspective, not bonus parity,
/// a native-authority capability, immutable metadata copy or source lease.
class DLL_LINKAGE VisibleHeroPolicy
{
public:
	enum class Knowledge { UNAVAILABLE, HIDDEN_UNKNOWN, KNOWN_ABSENT, VISIBLE_HERO };
private:
	BattleSide queriedSide;
	Knowledge knowledge;
	const CGHeroInstance * hero;
	VisibleHeroPolicy(BattleSide side, Knowledge knowledge, const CGHeroInstance * hero)
		: queriedSide(side), knowledge(knowledge), hero(hero) {}
	friend class CBattleInfoEssentials;
public:
	BattleSide side() const { return queriedSide; }
	Knowledge kind() const { return knowledge; }
	/// Preserve existing legacy branches that use nullptr for hidden policy.
	/// Callers must NOT interpret this compatibility pointer as known absence.
	const CGHeroInstance * legacyHero() const { return hero; }
	bool matchesSuppliedHero(const CGHeroInstance * supplied) const
	{
		return (knowledge == Knowledge::VISIBLE_HERO && hero == supplied)
			|| (knowledge == Knowledge::KNOWN_ABSENT && !supplied);
	}
};
