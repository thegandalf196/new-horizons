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
	const auto taught = learningExchangeSpecialtyRules(&teacher);
	const auto received = learningExchangeSpecialtyRules(&recipient);
	const auto currentMagic = [](const CGHeroInstance & hero)
	{
		const auto & magic = hero.getMagicRules();
		return newHorizonsMagic::rulesActive(magic)
			&& magic["rulesetVersion"].Integer() == newHorizonsMagic::CURRENT_RULESET_VERSION;
	};
	if(!currentMagic(teacher) || !currentMagic(recipient))
		return 1;
	return 1 + std::max(taught ? taught->additionalSpellsTaught : 0,
		received ? received->additionalSpellsReceived : 0);
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
	result.directedBonus = firstBudget > 1 || secondBudget > 1;
	return result;
}
}
