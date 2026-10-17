/*
 * VisibleHeroBonusQueries.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleProxy.h"
#include "IBattleState.h"
#include "../bonuses/BonusList.h"
#include "../mapObjects/CGHeroInstance.h"

TConstBonusListPtr CBattleInfoCallback::battleGetHeroBonuses(BattleSide side,
	const CSelector & selector, bool unstacked) const
{
	static const auto empty = std::make_shared<const BonusList>();
	if(!getBattle() || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| !battleDoWeKnowAbout(side))
		return empty;
	return visibleHeroBonuses(side, selector, unstacked);
}

TConstBonusListPtr CBattleInfoCallback::visibleHeroBonuses(BattleSide side,
	const CSelector & selector, bool unstacked) const
{
	// A player callback can wrap an owned projected IBattleInfo. Retain the
	// outer visibility gate, then use that actual world's query authority.
	if(const auto * world = dynamic_cast<const CBattleInfoCallback *>(getBattle()); world && world != this)
		return world->battleGetHeroBonuses(side, selector, unstacked);
	if(const auto * hero = battleGetFightingHero(side))
		return unstacked ? hero->getUnstackedBonuses(selector) : hero->getAllBonuses(selector, "");
	static const auto empty = std::make_shared<const BonusList>();
	return empty;
}

TConstBonusListPtr BattleProxy::visibleHeroBonuses(BattleSide side,
	const CSelector & selector, bool unstacked) const
{
	return subject->battleGetHeroBonuses(side, selector, unstacked);
}
