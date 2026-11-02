/*
 * VisibleHeroPolicy.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "BattleSide.h"
#include <stdexcept>

class CGHeroInstance;
class CBattleInfoEssentials;

/// Availability and optional native address in one REAL callback perspective.
/// An information-only copy preserves knowledge, never a source/identity lease.
class DLL_LINKAGE VisibleHeroPolicy
{
public:
	enum class Knowledge { UNAVAILABLE, HIDDEN_UNKNOWN, KNOWN_ABSENT, VISIBLE_HERO };
private:
	BattleSide queriedSide;
	Knowledge knowledge;
	const CGHeroInstance * hero;
	bool nativeAddressAvailable = true;
	VisibleHeroPolicy(BattleSide side, Knowledge knowledge, const CGHeroInstance * hero)
		: queriedSide(side), knowledge(knowledge), hero(hero) {}
	friend class CBattleInfoEssentials;
public:
	BattleSide side() const { return queriedSide; }
	Knowledge kind() const { return knowledge; }
	/// Preserve existing legacy branches that use nullptr for hidden policy.
	/// Callers must NOT interpret this compatibility pointer as known absence.
	const CGHeroInstance * legacyHero() const
	{
		if(knowledge == Knowledge::VISIBLE_HERO && !nativeAddressAvailable)
			throw std::logic_error("Copied visible Hero knowledge has no native address domain");
		return hero;
	}
	/// Copies an existing observation without retaining its Hero address. This
	/// does not construct visibility, grant hidden policy or authorize a source.
	VisibleHeroPolicy informationOnly() const
	{
		auto result = *this;
		result.hero = nullptr;
		result.nativeAddressAvailable = false;
		return result;
	}
	bool matchesSuppliedHero(const CGHeroInstance * supplied) const
	{
		return (knowledge == Knowledge::VISIBLE_HERO && nativeAddressAvailable && hero && hero == supplied)
			|| (knowledge == Knowledge::KNOWN_ABSENT && !supplied);
	}
};
