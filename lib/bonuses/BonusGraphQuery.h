/*
 * BonusGraphQuery.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include "CBonusSystemNode.h"
#include "BonusEvaluationContext.h"

/// Storage access only. The shared algorithms own traversal and formula order.
class DLL_LINKAGE BonusGraphQuery
{
public:
	virtual ~BonusGraphQuery() = default;
	virtual const BonusList & local(const CBonusSystemNode & node) const = 0;
	virtual const TCNodesVector & parents(const CBonusSystemNode & node) const = 0;
	virtual const TNodesVector & propagatingParents(const CBonusSystemNode & node) const = 0;
	virtual const TNodesVector & children(const CBonusSystemNode & node) const = 0;
	virtual BonusEvaluationContext evaluation(const CBonusSystemNode & node) const = 0;
};

DLL_LINKAGE void collectBonusGraph(const BonusGraphQuery & graph, const CBonusSystemNode & node,
	BonusList & out, bool inherited = false);
DLL_LINKAGE void collectLiveBonusGraph(const CBonusSystemNode & node, BonusList & out, bool inherited);
DLL_LINKAGE const BonusGraphQuery & liveBonusGraph();
DLL_LINKAGE bool bonusSourceOnly(BonusNodeType type);
DLL_LINKAGE void bonusRedParents(const BonusGraphQuery & graph, const CBonusSystemNode & node, TCNodes & out);
DLL_LINKAGE void bonusRedChildren(const BonusGraphQuery & graph, const CBonusSystemNode & node, TCNodes & out);
DLL_LINKAGE void bonusRedAncestors(const BonusGraphQuery & graph, const CBonusSystemNode & node, TCNodes & out);
/// Existing shouldBeAttached predicate and propagationUpdater source context.
DLL_LINKAGE std::shared_ptr<Bonus> propagatedBonus(const std::shared_ptr<Bonus> & bonus,
	const CBonusSystemNode & destination, const BonusEvaluationContext & source);
DLL_LINKAGE bool removePropagatedBonus(BonusList & local, const std::shared_ptr<Bonus> & bonus);
