/*
* TownPortalAction.h, part of VCMI engine
*
* Authors: listed in file AUTHORS in main folder
*
* License: GNU General Public License v2.0 or later
* Full text of license available in license.txt file, in main folder
*
*/

#pragma once

#include "SpecialAction.h"
#include "../../../../lib/mapObjects/MapObjects.h"
#include "../../Goals/AdventureSpellCast.h"

namespace NK2AI
{
namespace AIPathfinding
{
	class TownPortalAction : public SpecialAction
	{
	private:
		const CGTownInstance * target;
		SpellID usedSpell;
		bool usesSharedDailyOpportunity;

	public:
		TownPortalAction(const CGTownInstance * target, SpellID usedSpell, bool usesSharedDailyOpportunity)
			:target(target)
			,usedSpell(usedSpell)
			,usesSharedDailyOpportunity(usesSharedDailyOpportunity)
		{
		}

		void execute(AIGateway * aiGw, const CGHeroInstance * hero) const override;
		bool canAct(const Nullkiller * aiNk, const AIPathNode * source) const override;
		bool canAct(const Nullkiller * aiNk, const AIPathNode * source, int plannedTurn) const override;
		bool usesNewHorizonsAdventureSpellOpportunity() const override { return usesSharedDailyOpportunity; }
		void applyOnDestination(
			const CGHeroInstance * hero,
			CDestinationNodeInfo & destination,
			const PathNodeInfo & source,
			AIPathNode * dstNode,
			const AIPathNode * srcNode) const override;

		std::string toString() const override;
	};
}

}
