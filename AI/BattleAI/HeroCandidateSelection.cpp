/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "BattleEvaluator.h"
#include "HeroActionSelection.h"
#include "HeroActionTiming.h"

bool BattleEvaluator::finishHeroSelection(const std::vector<PossibleSpellcast> & possibleCasts,
	float noCastBaseline, bool mandatoryOrder, heroActionSelection::Observer * observer,
	heroActionTiming::Summary * timing)
{
	const auto selection = heroActionSelection::choose(possibleCasts, noCastBaseline, mandatoryOrder, observer);
	if(!selection)
		return false; // Existing nonempty guard above makes this defensive only.
	auto castToPerform = possibleCasts[selection->ordinal];
	if(selection->disposition == heroActionSelection::Disposition::RETAIN_FOLLOWUP)
	{
		print("No beneficial hero action; retaining the Spell Action for this round.");
		return false;
	}
	if(selection->disposition == heroActionSelection::Disposition::PERFORM)
	{
		print(boost::str(boost::format("Best hero action is %s (value %d). Will perform.") % castToPerform.name() % castToPerform.value));
		return submitHeroAction(castToPerform, timing);
	}

	print(boost::str(boost::format("Best hero action is %s. But it is actually useless (value %d).") % castToPerform.name() % castToPerform.value));

	return false;
}
