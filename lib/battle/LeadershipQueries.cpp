/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleInfo.h"
#include "BattleProxy.h"
#include "Unit.h"
#include "../bonuses/BonusList.h"
#include "../mapObjects/CGHeroInstance.h"

const newHorizonsHeroes::LeadershipComposition & BattleInfo::getLeadershipComposition(BattleSide side) const
{
	return sides.at(side).leadershipComposition;
}

const newHorizonsHeroes::LeadershipComposition & BattleProxy::getLeadershipComposition(BattleSide side) const
{
	return subject->getBattle()->getLeadershipComposition(side);
}

newHorizonsHeroes::LeadershipStackReadout CBattleInfoCallback::battleLeadershipReadout(const battle::Unit * unit) const
{
	using namespace newHorizonsHeroes;
	if(!unit || !getBattle()) return {};
	if(unit->isTurret() || unit->hasBonusOfType(BonusType::SIEGE_WEAPON))
		return leadershipStackReadout(nullptr, unit->creatureId(), unit->getCount());
	const auto side = playerToSide(battleGetOwner(unit));
	if(side == BattleSide::NONE || !battleDoWeKnowAbout(side))
		return leadershipStackReadout(nullptr, unit->creatureId(), unit->getCount());
	const auto * hero = battleGetFightingHero(side);
	if(!hero) return leadershipStackReadout(nullptr, unit->creatureId(), unit->getCount());
	auto modifiers = captureLeadershipModifiers(*hero);
	// This virtual query reads branch-owned hero bonuses in hypothetical worlds.
	modifiers.flatLeadership = battleGetHeroBonuses(side, Selector::type()(BonusType::LEADERSHIP), false)->totalValue();
	return leadershipStackReadout(hero, unit->creatureId(), unit->getCount(),
		&getBattle()->getLeadershipComposition(side), nullptr, &modifiers);
}
