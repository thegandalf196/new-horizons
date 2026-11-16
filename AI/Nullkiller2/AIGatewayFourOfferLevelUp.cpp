/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "AIGateway.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include <vector>

namespace NK2AI
{
void AIGateway::heroGotFourOfferLevel(const CGHeroInstance * hero, PrimarySkill,
	const newHorizonsHeroes::FourOfferProgressionOffer & offers, QueryID queryID,
	bool, QueryID replacesQueryID, int)
{
	if(replacesQueryID >= 0) return; // This controller did not request a reroll.
	status.addQuery(queryID, "Hero four-offer advancement");
	HeroPtr heroPtr(hero, cc.get());
	executeActionAsync("heroGotFourOfferLevel", [this, heroPtr, offers, queryID]()
	{
		if(!heroPtr.isVerified()) return answerQuery(queryID, -1);
		const auto * currentHero = heroPtr.getUnverified();
		const auto & state = currentHero->getPerkState();
		// executeActionAsync owns the shared game-state lease here. External
		// event grants may have changed eligibility since callback receipt.
		const auto candidates = newHorizonsHeroes::fourOfferCandidates(*cc, *currentHero);
		const auto legal = newHorizonsHeroes::legalFourOfferReplies(offers, state, candidates);
		std::vector<SecondarySkill> skills;
		std::vector<newHorizonsHeroes::PerkOfferCandidate> perks;
		std::vector<int32_t> replies;
		for(size_t slot = 0; slot < offers.skills.size(); ++slot)
			if(vstd::contains(legal, static_cast<int32_t>(slot)))
			{
				skills.push_back(offers.skills[slot]);
				replies.push_back(static_cast<int32_t>(slot));
			}
		const auto groups = newHorizonsHeroes::fourOfferPerkGroups(offers, candidates);
		for(size_t group = 0; group < groups.size(); ++group)
			for(const auto & candidate : groups[group])
			{
				perks.push_back(candidate);
				replies.push_back(newHorizonsHeroes::fourOfferPerkReply(offers, state, group, candidate.selection.perkId));
			}
		if(replies.empty()) return answerQuery(queryID, 0);
		const int selected = chooseLevelUpAdvancement(heroPtr, skills, perks);
		answerQuery(queryID, selected >= 0 && static_cast<size_t>(selected) < replies.size()
			? replies[selected] : -1);
	});
}
}
