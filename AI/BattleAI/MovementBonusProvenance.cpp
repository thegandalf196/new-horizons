/*
 * MovementBonusProvenance.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "MovementBonusProvenance.h"
#include "StackWithBonuses.h"
#include "../../lib/bonuses/BonusGraphQuery.h"
#include "../../lib/CStack.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

#include <set>

namespace
{
bool invariantNativeSources(const CBonusSystemNode & source)
{
	// This is the collector's actual local+inheritance closure, not exported
	// lists: local includes already materialized propagated bonuses as well.
	const auto & graph = liveBonusGraph();
	std::set<const CBonusSystemNode *> active, complete;
	std::vector<std::pair<const CBonusSystemNode *, bool>> work{{&source, false}};
	size_t nodes = 0, edges = 0, records = 0;
	while(!work.empty())
	{
		const auto [node, leaving] = work.back();
		work.pop_back();
		if(!node)
			return false;
		if(leaving)
		{
			active.erase(node);
			complete.insert(node);
			continue;
		}
		if(complete.contains(node))
			continue;
		if(!active.insert(node).second || ++nodes > 1024)
			return false;
		for(const auto & bonus : graph.local(*node))
			if(++records > 8192 || !bonus || !movementBonusProvenance::invariantRecord(*bonus))
				return false;
		const auto & parents = graph.parents(*node);
		if(parents.size() > 4096 - edges)
			return false;
		edges += parents.size();
		work.emplace_back(node, true);
		for(auto parent = parents.rbegin(); parent != parents.rend(); ++parent)
			work.emplace_back(*parent, false);
	}
	return true;
}
}

namespace movementBonusProvenance
{
bool invariantRecord(const Bonus & bonus)
{
	// Do not guess whether a known/foreign limiter or updater happens to be
	// position independent. Entangle removal must not unpropagate an aura that
	// HypotheticBattle's local-only removal cannot faithfully reproduce.
	return !bonus.limiter && !bonus.updater && !bonus.propagationUpdater
		&& !(bonus.type == BonusType::BIND_EFFECT && bonus.propagator);
}

bool invariantSources(const IBonusBearer & source)
{
	if(const auto * projected = dynamic_cast<const StackWithBonuses *>(&source))
		return typeid(source) == typeid(StackWithBonuses) && projected->hasMovementInvariantBonusSources();
	// Only these current native query owners have reviewed collector/suppression
	// semantics. A foreign subclass can override effective queries independently
	// of its raw graph, so a mere CBonusSystemNode cast is not enough.
	if(typeid(source) != typeid(CStack) && typeid(source) != typeid(CGHeroInstance)
		&& typeid(source) != typeid(CBonusSystemNode))
		return false;
	const auto * native = dynamic_cast<const CBonusSystemNode *>(&source);
	return native && invariantNativeSources(*native);
}
}
