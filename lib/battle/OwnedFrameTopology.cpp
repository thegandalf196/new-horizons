/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedFrameTopology.h"
#include "BattleInfo.h"
#include "../CStack.h"
#include "../CCreatureHandler.h"
#include "../mapObjects/army/CStackInstance.h"
#include "../mapObjects/army/CArmedInstance.h"
#include <algorithm>
#include <stdexcept>
namespace battle::ownedFrameCapture
{
Topology::Topology(const CStack & recipient, CreatureID form, bool native)
	: recipient(recipient), native(native)
{
	if(!recipient.getBattle()) throw std::invalid_argument("Owned active-battle topology requires its native occurrence");
	if(native)
	{
		if(recipient.base) rootParents.push_back(recipient.base);
		else
		{
			rootParents.push_back(recipient.battleFormOriginalCreature().toCreature());
			rootParents.push_back(recipient.getBattle()->getSideArmy(recipient.unitSide()));
		}
		if(form != recipient.battleFormOriginalCreature()) rootParents.push_back(form.toCreature());
		return; // Hypothetical native form has EMPTY own records and CURRENT upstream arrivals.
	}
	rootParents = liveBonusGraph().parents(recipient);
	const auto actual = stackFormSourceIdentity(recipient);
	const auto * desired = form != actual.baseCreature ? form.toCreature() : nullptr;
	if(desired == actual.attached) return;
	if(actual.attached)
	{
		const auto found = std::find(rootParents.begin(), rootParents.end(), actual.attached);
		if(found == rootParents.end()) throw std::invalid_argument("Actual form source is missing from native inheritance");
		rootParents.erase(found);
	}
	if(desired)
	{
		if(std::find(rootParents.begin(), rootParents.end(), desired) != rootParents.end())
			throw std::invalid_argument("Form source is already inherited");
		rootParents.push_back(desired);
	}
}
const BonusList & Topology::local(const CBonusSystemNode & node) const { return native && &node == &recipient ? empty : liveBonusGraph().local(node); }
const TCNodesVector & Topology::parents(const CBonusSystemNode & node) const { return &node == &recipient ? rootParents : liveBonusGraph().parents(node); }
const TNodesVector & Topology::propagatingParents(const CBonusSystemNode & node) const { return liveBonusGraph().propagatingParents(node); }
const TNodesVector & Topology::children(const CBonusSystemNode & node) const { return liveBonusGraph().children(node); }
BonusEvaluationContext Topology::evaluation(const CBonusSystemNode & node) const { return liveBonusGraph().evaluation(node); }
}
