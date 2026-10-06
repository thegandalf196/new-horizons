/*
* CastleGateAction.h, part of VCMI engine
*
* Authors: listed in file AUTHORS in main folder
*
* License: GNU General Public License v2.0 or later
* Full text of license available in license.txt file, in main folder
*
*/

#pragma once

#include "SpecialAction.h"

class CGTownInstance;

namespace NK2AI::AIPathfinding
{
	class CastleGateAction final : public SpecialAction
	{
	public:
		CastleGateAction(const CGTownInstance * source, const CGTownInstance * destination);

		bool canAct(const Nullkiller * aiNk, const AIPathNode * source) const override;
		bool canAct(const Nullkiller * aiNk, const AIPathNode * source, int plannedTurn) const override;
		bool usesNewHorizonsCastleGateOpportunity() const override { return true; }
		void execute(AIGateway * aiGw, const CGHeroInstance * hero) const override;
		void applyOnDestination(
			const CGHeroInstance * hero,
			CDestinationNodeInfo & destinationInfo,
			const PathNodeInfo & source,
			AIPathNode * destinationNode,
			const AIPathNode * sourceNode) const override;
		std::string toString() const override;

	private:
		const CGTownInstance * sourceTown;
		const CGTownInstance * destinationTown;
	};
}
