/*
 * BonusLimitation.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "BonusLimitation.h"
#include "Limiters.h"
#include "BonusList.h"
#include <stdexcept>

BonusLimitationContext::BonusLimitationContext(const Bonus & bonus, const CBonusSystemNode & node,
	const BonusList & accepted, const BonusList & undecided)
	: BonusLimitationContext(bonus, node, accepted, undecided, node)
{
}

BonusLimitationContext::BonusLimitationContext(const Bonus & bonus, const CBonusSystemNode & node,
	const BonusList & accepted, const BonusList & undecided, const BonusEvaluationContext & input)
	: b(bonus), node(node), alreadyAccepted(accepted), stillUndecided(undecided), evaluation(input)
{
	if(&node != &evaluation.originalNode())
		throw std::invalid_argument("Bonus limiter node does not match its evaluation context");
}

void limitBonuses(const BonusList &allBonuses, BonusList &out, const BonusEvaluationContext & evaluation)
{
	assert(&allBonuses != &out); //todo should it work in-place?

	BonusList undecided = allBonuses;
	BonusList & accepted = out;

	while(true)
	{
		int undecidedCount = static_cast<int>(undecided.size());
		for(int i = 0; i < undecided.size(); i++)
		{
			auto b = undecided[i];
			if(!evaluation.acceptsBonus(*b))
			{
				undecided.erase(i);
				i--; continue;
			}
			BonusLimitationContext context = {*b, evaluation.originalNode(), out, undecided, evaluation};
			auto decision = b->limiter ? b->limiter->limit(context) : ILimiter::EDecision::ACCEPT; //bonuses without limiters will be accepted by default
			if(decision == ILimiter::EDecision::DISCARD || decision == ILimiter::EDecision::NOT_APPLICABLE)
			{
				undecided.erase(i);
				i--; continue;
			}
			else if(decision == ILimiter::EDecision::ACCEPT)
			{
				accepted.push_back(b);
				undecided.erase(i);
				i--; continue;
			}
			else
				assert(decision == ILimiter::EDecision::NOT_SURE);
		}

		if(undecided.size() == undecidedCount) //we haven't moved a single bonus -> limiters reached a stable state
			return;
	}
}
