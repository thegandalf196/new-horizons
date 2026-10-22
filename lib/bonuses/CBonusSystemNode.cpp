/*
 * CBonusSystemNode.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"

#include "CBonusSystemNode.h"
#include "NativeBonusGraphAccess.h"
#include "BonusDurationStep.h"
#include "Limiters.h"
#include "BonusLimitation.h"
#include "BonusGraphQuery.h"
#include "Updaters.h"
#include "Propagators.h"

static std::atomic<int32_t> globalCounter = 1;

std::shared_ptr<Bonus> CBonusSystemNode::getLocalBonus(const CSelector & selector)
{
	NativeBonusGraphAccess graphAccess;
	auto ret = bonuses.getFirst(selector);
	if(ret)
		return ret;
	return nullptr;
}

std::shared_ptr<const Bonus> CBonusSystemNode::getFirstBonus(const CSelector & selector) const
{
	NativeBonusGraphAccess graphAccess;
	auto ret = bonuses.getFirst(selector);
	if(ret)
		return ret;

	TCNodes lparents;
	getDirectParents(lparents);
	for(const CBonusSystemNode *pname : lparents)
	{
		ret = pname->getFirstBonus(selector);
		if (ret)
			return ret;
	}

	return nullptr;
}

void CBonusSystemNode::getDirectParents(TCNodes & out) const /*retrieves list of parent nodes (nodes to inherit bonuses from) */
{
	NativeBonusGraphAccess graphAccess;
	for(const auto * elem : parentsToInherit)
		out.insert(elem);
}

void CBonusSystemNode::getAllBonusesRec(BonusList &out, bool inherited) const
{
	collectLiveBonusGraph(*this, out, inherited);
}

CBonusSystemNode::CBonusSystemNode(BonusNodeType NodeType, bool isHypotetic):
	nodeType(NodeType),
	cachedLast(0),
	nodeChanged(0),
	isHypotheticNode(isHypotetic)
{
	NativeBonusGraphAccess graphAccess;
}

CBonusSystemNode::CBonusSystemNode(BonusNodeType NodeType):
	CBonusSystemNode(NodeType, false)
{}

CBonusSystemNode::~CBonusSystemNode()
{
	NativeBonusGraphAccess graphAccess;
	detachFromAll();

	if(!children.empty())
	{
		while(!children.empty())
			children.front()->detachFrom(*this);
	}
}

void CBonusSystemNode::attachTo(CBonusSystemNode & parent)
{
	NativeBonusGraphAccess graphAccess;
	assert(!vstd::contains(parentsToPropagate, &parent));
	parentsToPropagate.push_back(&parent);

	attachToSource(parent);

	if(!isHypothetic())
	{
		if(!parent.actsAsBonusSourceOnly())
			newRedDescendant(parent);

		assert(!vstd::contains(parent.children, this));
		parent.children.push_back(this);
	}

	// The new branch was invalidated by attachToSource. Adding it does not
	// change the parent or existing branches; propagation invalidates every
	// node whose bonuses actually change.
}

void CBonusSystemNode::attachToSource(const CBonusSystemNode & parent)
{
	NativeBonusGraphAccess graphAccess;
	assert(!vstd::contains(parentsToInherit, &parent));
	parentsToInherit.push_back(&parent);

	++globalCounter;

	if(!isHypothetic())
	{
		if(parent.actsAsBonusSourceOnly())
			parent.newRedDescendant(*this);
	}

	invalidateChildrenNodes(globalCounter);
}

void CBonusSystemNode::detachFrom(CBonusSystemNode & parent)
{
	NativeBonusGraphAccess graphAccess;
	assert(vstd::contains(parentsToPropagate, &parent));

	if(!isHypothetic())
	{
		if(!parent.actsAsBonusSourceOnly())
			removedRedDescendant(parent);
	}

	detachFromSource(parent);

	if (vstd::contains(parentsToPropagate, &parent))
	{
		parentsToPropagate -= &parent;
	}
	else
	{
		logBonus->error("Error on Detach. Node %s (nodeType=%d) has not parent %s (nodeType=%d)",
			nodeShortInfo(), static_cast<int>(nodeType), parent.nodeShortInfo(), static_cast<int>(parent.nodeType));
	}

	if(!isHypothetic())
	{
		if(vstd::contains(parent.children, this))
			parent.children -= this;
		else
		{
			logBonus->error("Error on Detach. Node %s (nodeType=%d) is not a child of %s (nodeType=%d)",
				nodeShortInfo(), static_cast<int>(nodeType), parent.nodeShortInfo(), static_cast<int>(parent.nodeType));
		}
	}

	parent.nodeHasChanged();
}


void CBonusSystemNode::detachFromSource(const CBonusSystemNode & parent)
{
	NativeBonusGraphAccess graphAccess;
	assert(vstd::contains(parentsToInherit, &parent));

	++globalCounter;

	if(!isHypothetic())
	{
		if(parent.actsAsBonusSourceOnly())
			parent.removedRedDescendant(*this);
	}

	if (vstd::contains(parentsToInherit, &parent))
	{
		parentsToInherit -= &parent;
	}
	else
	{
		logBonus->error("Error on Detach. Node %s (nodeType=%d) has not parent %s (nodeType=%d)",
			nodeShortInfo(), static_cast<int>(nodeType), parent.nodeShortInfo(), static_cast<int>(parent.nodeType));
	}

	invalidateChildrenNodes(globalCounter);
}

void CBonusSystemNode::removeBonusesRecursive(const CSelector & s)
{
	NativeBonusGraphAccess graphAccess;
	removeBonuses(s);
	for(CBonusSystemNode * child : children)
		child->removeBonusesRecursive(s);
}

void CBonusSystemNode::reduceBonusDurations(const CSelector &s)
{
	NativeBonusGraphAccess graphAccess;
	bonusDuration::NativeStorage storage{*this};
	bonusDuration::reduceSelected(exportedBonuses, s, storage);

	for(CBonusSystemNode *child : children)
		child->reduceBonusDurations(s);
}

void CBonusSystemNode::addNewBonus(const std::shared_ptr<Bonus>& b)
{
	NativeBonusGraphAccess graphAccess;
	//turnsRemain shouldn't be zero for following durations
	if(Bonus::NTurns(b.get()) || Bonus::NDays(b.get()) || Bonus::OneWeek(b.get()))
	{
		assert(b->turnsRemain);
	}

	assert(!vstd::contains(exportedBonuses, b));
	exportedBonuses.push_back(b);
	exportBonus(b);
}

void CBonusSystemNode::accumulateBonus(const std::shared_ptr<Bonus>& b)
{
	NativeBonusGraphAccess graphAccess;
	auto bonus = exportedBonuses.getFirst(Selector::typeSubtypeValueType(b->type, b->subtype, b->valType)); //only local bonuses are interesting
	if(bonus)
		bonus->val += b->val;
	else
		addNewBonus(std::make_shared<Bonus>(*b)); //duplicate needed, original may get destroyed
}

void CBonusSystemNode::removeBonus(const std::shared_ptr<Bonus>& b)
{
	NativeBonusGraphAccess graphAccess;
	exportedBonuses -= b;
	if(b->propagator)
	{
		unpropagateBonus(b);
	}
	else
	{
		bonuses -= b;
		nodeHasChanged();
	}
}

void CBonusSystemNode::removeBonuses(const CSelector & selector)
{
	NativeBonusGraphAccess graphAccess;
	BonusList toRemove;
	exportedBonuses.getBonuses(toRemove, selector);
	for(const auto & bonus : toRemove)
		removeBonus(bonus);
}

bool CBonusSystemNode::actsAsBonusSourceOnly() const
{
	return bonusSourceOnly(nodeType);
}

void CBonusSystemNode::propagateBonus(const std::shared_ptr<Bonus> & b, const CBonusSystemNode & source)
{
	if(auto propagated = propagatedBonus(b, *this, source))
	{
		bonuses.push_back(propagated);
		logBonus->trace("Bonus (type=%d, val=%d) propagated to %s", static_cast<int>(propagated->type), propagated->val, nodeName());
		nodeHasChanged();
	}

	TNodes lchildren;
	getRedChildren(lchildren);
	for(CBonusSystemNode *pname : lchildren)
		pname->propagateBonus(b, source);
}

void CBonusSystemNode::unpropagateBonus(const std::shared_ptr<Bonus> & b)
{
	if(b->propagator->shouldBeAttached(this))
	{
		if (b->propagationUpdater)
		{
			removePropagatedBonus(bonuses, b);
		}
		else
		{
			if (removePropagatedBonus(bonuses, b))
				logBonus->trace("Bonus (type=%d, val=%d) is no longer propagated to %s", static_cast<int>(b->type), b->val, nodeName());
			else
				logBonus->warn("Attempt to remove bonus (type=%d, val=%d), which is not propagated to %s", static_cast<int>(b->type), b->val, nodeName());
		}

		nodeHasChanged();
	}

	TNodes lchildren;
	getRedChildren(lchildren);
	for(CBonusSystemNode *pname : lchildren)
		pname->unpropagateBonus(b);
}

void CBonusSystemNode::detachFromAll()
{
	NativeBonusGraphAccess graphAccess;
	while(!parentsToPropagate.empty())
		detachFrom(*parentsToPropagate.front());

	while(!parentsToInherit.empty())
		detachFromSource(*parentsToInherit.front());
}

bool CBonusSystemNode::isIndependentNode() const
{
	return parentsToInherit.empty() && parentsToPropagate.empty() && children.empty();
}

std::string CBonusSystemNode::nodeName() const
{
	return std::string("Bonus system node of type ") + typeid(*this).name();
}

std::string CBonusSystemNode::nodeShortInfo() const
{
	std::ostringstream str;
	str << "'" << typeid(* this).name() << "'";
	return str.str();
}

void CBonusSystemNode::getRedParents(TCNodes & out) const
{
	bonusRedParents(liveBonusGraph(), *this, out);
}

void CBonusSystemNode::getRedChildren(TNodes &out)
{
	TCNodes selected;
	bonusRedChildren(liveBonusGraph(), *this, selected);
	for(const auto * child : selected)
		out.insert(const_cast<CBonusSystemNode *>(child));
}

void CBonusSystemNode::newRedDescendant(CBonusSystemNode & descendant) const
{
	for(const auto & b : exportedBonuses)
	{
		if(b->propagator)
			descendant.propagateBonus(b, *this);
	}
	TCNodes redParents;
	getRedAncestors(redParents); //get all red parents recursively

	for(const auto * parent : redParents)
	{
		for(const auto & b : parent->exportedBonuses)
		{
			if(b->propagator)
				descendant.propagateBonus(b, *this);
		}
	}
}

void CBonusSystemNode::removedRedDescendant(CBonusSystemNode & descendant) const
{
	for(const auto & b : exportedBonuses)
		if(b->propagator)
			descendant.unpropagateBonus(b);

	TCNodes redParents;
	getRedAncestors(redParents); //get all red parents recursively

	for(auto * parent : redParents)
	{
		for(const auto & b : parent->exportedBonuses)
			if(b->propagator)
				descendant.unpropagateBonus(b);
	}
}

void CBonusSystemNode::getRedAncestors(TCNodes &out) const
{
	bonusRedAncestors(liveBonusGraph(), *this, out);
}

void CBonusSystemNode::exportBonus(const std::shared_ptr<Bonus> & b)
{
	if(b->propagator)
	{
		propagateBonus(b, *this);
	}
	else if(!vstd::contains(bonuses, b))
	{
		bonuses.push_back(b);
		nodeHasChanged();
	}
}

void CBonusSystemNode::exportBonuses()
{
	NativeBonusGraphAccess graphAccess;
	for(const auto & b : exportedBonuses)
		exportBonus(b);
}

BonusNodeType CBonusSystemNode::getNodeType() const
{
	return nodeType;
}

const TCNodesVector& CBonusSystemNode::getParentNodes() const
{
	return parentsToInherit;
}

BonusList & CBonusSystemNode::getExportedBonusList()
{
	return exportedBonuses;
}

const BonusList & CBonusSystemNode::getExportedBonusList() const
{
	return exportedBonuses;
}

void CBonusSystemNode::limitBonuses(const BonusList &allBonuses, BonusList &out) const
{
	::limitBonuses(allBonuses, out, *this);
}

int32_t CBonusSystemNode::preparedGraphGeneration() noexcept
{
	return globalCounter.load();
}

bool CBonusSystemNode::reservePreparedGenerations(int32_t expected, int32_t final) noexcept
{
	return globalCounter.compare_exchange_strong(expected, final);
}

void CBonusSystemNode::publishPreparedInvalidation(int32_t version, const TNodesVector & targets) noexcept
{
	for(auto * target : targets)
		target->nodeChanged.store(version);
}

void CBonusSystemNode::nodeHasChanged()
{
	invalidateChildrenNodes(++globalCounter);
}

void CBonusSystemNode::invalidateChildrenNodes(int32_t changeCounter)
{
	if (nodeChanged == changeCounter)
		return;

	nodeChanged = changeCounter;

	for(CBonusSystemNode * child : children)
		child->invalidateChildrenNodes(changeCounter);
}

int32_t CBonusSystemNode::getTreeVersion() const
{
	return nodeChanged;
}
