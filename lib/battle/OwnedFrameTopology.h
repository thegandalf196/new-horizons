/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../bonuses/BonusGraphQuery.h"
#include "StackFormSourceIdentity.h"
class CStack;
namespace battle::ownedFrameCapture
{
/// Immediate graph reader with a single root topology/value override. Raw
/// accepted lists stay CURRENT until owned canonical source-relation replay.
class Topology final : public BonusGraphQuery
{
	const CStack & recipient;
	TCNodesVector rootParents;
	BonusList empty;
	bool native;
public:
	Topology(const CStack &, CreatureID form, bool native);
	const BonusList & local(const CBonusSystemNode &) const override;
	const TCNodesVector & parents(const CBonusSystemNode &) const override;
	const TNodesVector & propagatingParents(const CBonusSystemNode &) const override;
	const TNodesVector & children(const CBonusSystemNode &) const override;
	BonusEvaluationContext evaluation(const CBonusSystemNode &) const override;
};
}
