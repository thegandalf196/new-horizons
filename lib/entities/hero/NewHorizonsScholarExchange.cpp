/*
 * NewHorizonsScholarExchange.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsLearningExchangeSpecialties.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../spells/NewHorizonsMagic.h"
namespace newHorizonsHeroes
{
int scholarExchangeSpellBudget(const CGHeroInstance & teacher, const CGHeroInstance & recipient)
{
	validateLearningExchangeSpecialtiesSerialization(teacher.getPrimaryGrowthRules(), true);
	validateLearningExchangeSpecialtiesSerialization(recipient.getPrimaryGrowthRules(), true);
	return std::numeric_limits<int>::max(); // All eligible unknown spells, never a specialty count.
}
std::optional<ScholarExchange> prepareScholarExchange(
	const CGHeroInstance & first, const CGHeroInstance & second, int32_t week)
{
	// Validate both captured policy blocks before an authoritative caller can
	// spend the ordinary receipt. Budget queries alone never authorize a meeting.
	const auto firstBudget = scholarExchangeSpellBudget(first, second);
	const auto secondBudget = scholarExchangeSpellBudget(second, first);
	if(!first.canExchangeNewHorizonsScholarWith(second, week))
		return std::nullopt;
	ScholarExchange result;
	result.firstTeaches = first.getNewHorizonsScholarSpellsFor(second, static_cast<size_t>(firstBudget));
	result.secondTeaches = second.getNewHorizonsScholarSpellsFor(first, static_cast<size_t>(secondBudget));
	if(result.firstTeaches.empty() && result.secondTeaches.empty())
		return std::nullopt;
	result.directedBonus = true;
	return result;
}
}
