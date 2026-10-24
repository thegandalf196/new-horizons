/* AIPathNodeExport.cpp, part of VCMI engine; GPL v2.0 or later. */
#include "StdInc.h"
#include "AIPathNodeExport.h"
#include "AINodeStorage.h"
#include "Actors.h"

namespace NK2AI
{
AIPathNodeInfo exportAIPathNode(const AIPathNode & node, int parent, const Nullkiller * ai)
{
	AIPathNodeInfo result;
	result.cost = node.getCost();
	result.targetHero = node.actor->hero;
	result.chainMask = node.actor->chainMask;
	result.specialAction = node.specialAction;
	result.turns = node.turns;
	result.danger = node.danger;
	result.coord = node.coord;
	result.parentIndex = parent;
	result.actionIsBlocked = false;
	result.layer = node.layer;

	if(result.specialAction)
	{
		const auto * source = node.theNodeBefore
			? static_cast<const AIPathNode *>(node.theNodeBefore) : &node;
		result.actionIsBlocked = !result.specialAction->canAct(ai, source, node.turns);
	}
	// This exchange entry preserves receiver readiness before the donor branch.
	// The original carrier, rather than the combined actor, owns source position.
	const auto * carrier = node.actor->carrierParent;
	const auto * donor = node.actor->otherParent;
	if(carrier && donor && carrier->hero == result.targetHero && donor->hero
		&& donor->hero != result.targetHero && node.chainOther && node.chainOther->actor
		&& node.chainOther->actor->hero == donor->hero && node.chainOther->coord == node.coord
		&& node.chainOther->turns == 0 && node.chainOther->getCost() > 0
		&& !node.theNodeBefore && !node.specialAction && node.action == EPathNodeAction::NORMAL
		&& node.layer == EPathfindingLayer::LAND && node.turns == 0 && node.coord == carrier->initialPosition)
		result.exchangeReadinessDonor = donor->hero;
	return result;
}
}
