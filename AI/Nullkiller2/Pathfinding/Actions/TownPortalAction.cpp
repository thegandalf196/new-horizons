/*
* TownPortalAction.cpp, part of VCMI engine
*
* Authors: listed in file AUTHORS in main folder
*
* License: GNU General Public License v2.0 or later
* Full text of license available in license.txt file, in main folder
*
*/

#include "StdInc.h"
#include "../../Goals/AdventureSpellCast.h"
#include "../../../../lib/mapObjects/MapObjects.h"
#include "../../../../lib/spells/CSpell.h"
#include "../AINodeStorage.h"
#include "TownPortalAction.h"

namespace NK2AI
{

using namespace AIPathfinding;

bool TownPortalAction::canAct(const Nullkiller * aiNk, const AIPathNode * source) const
{
	return canAct(aiNk, source, source->turns);
}

bool TownPortalAction::canAct(const Nullkiller *, const AIPathNode * source, const int plannedTurn) const
{
	const auto * hero = source->actor ? source->actor->hero : nullptr;
	if(!hero || !hero->canCastThisSpell(usedSpell.toSpell()))
		return false;

	if(usesSharedDailyOpportunity
		&& hasNewHorizonsAdventureSpellCastFlag(dayFlagsForTurn(source, plannedTurn)))
		return false;

	return hero->getManaAvailable() >= source->manaCost + hero->getSpellCost(usedSpell.toSpell());
}

void TownPortalAction::applyOnDestination(
	const CGHeroInstance * hero,
	CDestinationNodeInfo & destination,
	const PathNodeInfo & source,
	AIPathNode * dstNode,
	const AIPathNode * srcNode) const
{
	dstNode->manaCost = srcNode->manaCost + hero->getSpellCost(usedSpell.toSpell());
	dstNode->theNodeBefore = source.node;
	dstNode->dayFlags = dayFlagsForTurn(srcNode, destination.turn);
	if(usesSharedDailyOpportunity)
		dstNode->dayFlags = static_cast<DayFlags>(dstNode->dayFlags | DayFlags::NEW_HORIZONS_ADVENTURE_SPELL_CAST);
}

void TownPortalAction::execute(AIGateway * aiGw, const CGHeroInstance * hero) const
{
	auto goal = Goals::AdventureSpellCast(hero, usedSpell);
	
	goal.town = target;
	goal.tile = target->visitablePos();

	goal.accept(aiGw);
}

std::string TownPortalAction::toString() const
{
	return "Town Portal to " + target->getNameTextID();
}
/*
bool TownPortalAction::canAct(const CGHeroInstance * hero, const AIPathNode * source) const
{
#ifdef VCMI_TRACE_PATHFINDER
	logAi->trace(
		"Hero %s has %d mana and needed %d and already spent %d",
		hero->name,
		hero->getManaAvailable(),
		getManaCost(hero),
		source->manaCost);
#endif

	return hero->getManaAvailable() >= source->manaCost + getManaCost(hero);
}

uint32_t TownPortalAction::getManaCost(const CGHeroInstance * hero) const
{
	SpellID summonBoat = SpellID::TOWN_PORTAL;

	return hero->getSpellCost(summonBoat.toSpell());
}*/

}
