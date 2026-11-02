/* Quick Study offer policy, part of VCMI / New Horizons; GPL v2.0 or later. */
#include "../StdInc.h"
#include "HeroManager.h"
#include "../../../lib/callback/CCallback.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include <algorithm>
#include <cmath>

namespace NK2AI
{
namespace
{
bool preferredPerk(const CGHeroInstance & hero, const newHorizonsHeroes::PerkSelection & selection, int rank)
{
	const auto ownFaction = newHorizonsHeroes::factionSkill(hero.getPrimaryGrowthRules(), hero.getFactionID());
	const auto decoded = SecondarySkill::decode(selection.skillId);
	if(ownFaction && decoded >= 0 && newHorizonsHeroes::isFactionSkillForFaction(
		hero.getPrimaryGrowthRules(), hero.getFactionID(), SecondarySkill(decoded)))
		return true;
	if(rank >= 2)
		return true;
	return selection.perkId == "new-horizons:discipline.inspirationalLeader"
		&& std::max(0, hero.moraleVal()) * std::max(1, static_cast<int>(hero.Slots().size())) > 0;
}
}

bool HeroManager::shouldRerollLevelUpOffer(const HeroPtr & heroPtr, const std::vector<SecondarySkill> & skills,
	const std::vector<newHorizonsHeroes::PerkOfferCandidate> & perks, int reachedLevel) const
{
	const auto * hero = heroPtr.getUnverified();
	const auto player = cc->getPlayerID();
	if(!hero || !player || hero->getOwner() != *player || reachedLevel != hero->level
		|| (skills.empty() && perks.empty()))
		return false;
	try
	{
		const auto & state = hero->getPerkState();
		const auto interval = newHorizonsHeroes::quickStudyRerollInterval(state.rules);
		if(!interval || reachedLevel % *interval != 0
			|| !hero->hasActivePerk("new-horizons:learning", "new-horizons:learning.quickStudy")
			|| !newHorizonsHeroes::usesR2PerkOffers(state.rules)
			|| !newHorizonsHeroes::usesStrictPerkProgression(state.rules))
			return false;
		if(!skills.empty())
		{
			const float score = bestLevelUpSkill(heroPtr, skills).second;
			if(!std::isfinite(score) || score > 0.0f)
				return false;
		}
		for(const auto & candidate : perks)
		{
			const auto definition = newHorizonsHeroes::perkDefinition(state.rules,
				candidate.selection.skillId, candidate.selection.perkId);
			if(!definition || definition->effect["status"].String() != "active"
				|| candidate.requiredRank != newHorizonsHeroes::perkRequiredRank(definition->requiredRank)
				|| !state.canAcquirePerk(candidate.selection.skillId, candidate.selection.perkId,
					hero->getPerkSkillRank(candidate.selection.skillId))
				|| preferredPerk(*hero, candidate.selection, candidate.requiredRank))
				return false;
		}
		const auto weights = state.quantizedParentWeights(hero->getPerkOfferWeightContext());
		for(const auto & [skillId, weight] : weights)
		{
			if(weight == "0")
				continue;
			const int rank = hero->getPerkSkillRank(skillId);
			for(const auto & definition : newHorizonsHeroes::perkOptions(state.rules, skillId))
			{
				const newHorizonsHeroes::PerkSelection alternative{skillId, definition.id};
				const int requiredRank = newHorizonsHeroes::perkRequiredRank(definition.requiredRank);
				if(definition.effect["status"].String() == "active"
					&& state.canAcquirePerk(skillId, definition.id, rank)
					&& preferredPerk(*hero, alternative, requiredRank)
					&& std::none_of(perks.begin(), perks.end(), [&](const auto & offered)
					{ return offered.selection == alternative; }))
					return true;
			}
		}
	}
	catch(const std::exception &)
	{
		// Incomplete captured rules never justify discarding an existing offer.
	}
	return false;
}
}
