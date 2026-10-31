/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BonusSourceRelationRecipe.h"
#include "Propagators.h"

std::shared_ptr<Bonus> appendPropagatedArrival(BonusList & accepted,
	const std::shared_ptr<Bonus> & bonus, const CBonusSystemNode & destination,
	const BonusEvaluationContext & source)
{
	auto result = propagatedBonus(bonus, destination, source);
	if(result) accepted.push_back(result);
	return result;
}

PropagatedArrivalRemoval removePropagatedArrival(BonusList & accepted,
	const std::shared_ptr<Bonus> & bonus, const CBonusSystemNode & destination)
{
	if(!bonus->propagator->shouldBeAttached(const_cast<CBonusSystemNode *>(&destination)))
		return {false, false};
	return {true, removePropagatedBonus(accepted, bonus)};
}
