/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "GraveDustApplication.h"
#include "NewHorizonsGraveDust.h"
#include "Unit.h"
#include "../bonuses/BonusList.h"
#include <stdexcept>
namespace newHorizonsGraveDust
{
newHorizonsOrdinaryStrike::EffectPlan prepareApplication(const battle::Unit & recipient,
	const int32_t reductionBasisPoints, const PlayerColor sourcePlayer)
{
	newHorizonsOrdinaryStrike::EffectPlan result;
	auto next = records(reductionBasisPoints, sourcePlayer); // Validates domain; zero creates no records.
	if(next.empty() || !recipient.alive() || recipient.isGhost() || recipient.isTimeStopped()) return result;
	const auto existing = recipient.getUnstackedBonuses(CSelector([](const Bonus * bonus)
	{
		return bonus && isReservedSource(*bonus);
	}));
	if(!existing) throw std::invalid_argument("Invalid Grave Dust provenance");
	std::vector<Bonus> current;
	for(const auto & bonus : *existing)
	{
		if(!bonus) throw std::invalid_argument("Invalid Grave Dust provenance");
		current.push_back(*bonus);
	}
	if(!current.empty())
	{
		validateSourceGroup(current);
		const Bonus * oldMarker = nullptr;
		const Bonus * oldReduction = nullptr;
		for(const auto & bonus : current)
			if(bonus.type == BonusType::PHYSICAL_AFFLICTION) oldMarker = &bonus;
			else oldReduction = &bonus;
		// Identical STACK_ACTIVATION clocks already end at the same next accepted action.
		// Equal refresh is an exact no-op, retaining the old caster/order/presentation.
		if(oldReduction->val >= reductionBasisPoints) return result;
		next.front().parameters = oldMarker->parameters; // Immutable metadata lease retains cleanse order.
	}
	result.target = recipient.unitId();
	result.remove = std::move(current);
	result.add = std::move(next);
	return result;
}
}
