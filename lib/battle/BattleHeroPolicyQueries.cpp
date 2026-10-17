/*
 * BattleHeroPolicyQueries.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "CBattleInfoEssentials.h"
#include "IBattleState.h"

bool CBattleInfoEssentials::battleDoWeKnowAbout(BattleSide side) const
{
	RETURN_IF_NOT_BATTLE(false);
	auto p = battleGetMySide();
	return p == BattleSide::ALL_KNOWING || p == side;
}

const CGHeroInstance * CBattleInfoEssentials::battleGetFightingHero(BattleSide side) const
{
	RETURN_IF_NOT_BATTLE(nullptr);
	if(side != BattleSide::DEFENDER && side != BattleSide::ATTACKER)
	{
		logGlobal->error("FIXME: %s wrong argument!", __FUNCTION__);
		return nullptr;
	}

	if(!battleDoWeKnowAbout(side))
	{
		// A player-scoped callback is expected to query hidden sides while
		// evaluating battle state. Keep the visibility check, but do not turn
		// every denied query into an error-level log entry.
		return nullptr;
	}

	return getBattle()->getSideHero(side);
}

VisibleHeroPolicy CBattleInfoEssentials::battleGetVisibleHeroPolicy(BattleSide side) const
{
	if(!getBattle() || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
		return {side, VisibleHeroPolicy::Knowledge::UNAVAILABLE, nullptr};
	if(!battleDoWeKnowAbout(side))
		return {side, VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN, nullptr};
	const auto * hero = getBattle()->getSideHero(side);
	return {side, hero ? VisibleHeroPolicy::Knowledge::VISIBLE_HERO
		: VisibleHeroPolicy::Knowledge::KNOWN_ABSENT, hero};
}
