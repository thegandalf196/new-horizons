/* AIPathNodeExport.h, part of VCMI engine; GPL v2.0 or later. */
#pragma once

#include "../../../lib/pathfinder/CGPathNode.h"
#include <cstdint>
#include <memory>

class CGHeroInstance;

namespace NK2AI
{
struct AIPathNode;
class Nullkiller;
class SpecialAction;

struct AIPathNodeInfo
{
	float cost;
	uint8_t turns;
	int3 coord;
	EPathfindingLayer layer;
	uint64_t danger;
	const CGHeroInstance * targetHero;
	int parentIndex;
	uint64_t chainMask;
	std::shared_ptr<const SpecialAction> specialAction;
	bool actionIsBlocked;
	// Native exchange receiver already at its source, waiting for this donor.
	// Synthetic graph actions have no such provenance.
	const CGHeroInstance * exchangeReadinessDonor = nullptr;

	bool isExchangeReadinessFor(const CGHeroInstance * hero, const AIPathNodeInfo & dependency) const
	{
		return hero && targetHero == hero && exchangeReadinessDonor && exchangeReadinessDonor != hero
			&& dependency.targetHero == exchangeReadinessDonor && dependency.coord == coord
			&& turns == 0 && dependency.turns == 0
			&& layer == EPathfindingLayer::LAND && dependency.layer == EPathfindingLayer::LAND
			&& !specialAction && !actionIsBlocked && !dependency.specialAction && !dependency.actionIsBlocked;
	}
};

/// Materialize one native chain entry for execution.
DLL_LINKAGE AIPathNodeInfo exportAIPathNode(const AIPathNode & node, int parent, const Nullkiller * ai);
}
