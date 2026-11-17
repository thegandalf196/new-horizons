/* Hero advancement valuation, part of VCMI / New Horizons; GPL v2.0 or later. */
#include "StdInc.h"
#include "AIGateway.h"
#include "Helpers/LeadershipEvaluation.h"
#include "Analyzers/HeroManager.h"
#include "../../lib/entities/hero/NewHorizonsFourOfferProgression.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include <algorithm>
#include <set>
#include <tuple>

namespace
{
bool worsensTierReservation(const CGHeroInstance & hero, const newHorizonsHeroes::PerkOfferCandidate & candidate)
{
	const auto & state = hero.getPerkState();
	if(newHorizonsHeroes::usesFourOfferProgression(state.getRules()))
		return newHorizonsHeroes::perkChoiceBlocksTierCompletion(state, candidate,
			hero.getPerkSkillRank(candidate.selection.skillId));
	// Historical R2's active-tier reservation remains byte-for-byte arithmetic,
	// including catalogues with a planned, unavailable later tier.
	std::set<int> missingTiers;
	for(const auto & definition : newHorizonsHeroes::perkOptions(state.getRules(), candidate.selection.skillId))
		if(definition.effect["status"].String() == "active")
			missingTiers.insert(newHorizonsHeroes::perkRequiredRank(definition.requiredRank));
	si64 count = 0;
	for(const auto & selection : state.getSelected())
	{
		if(selection.skillId != candidate.selection.skillId) continue;
		++count;
		const auto definition = newHorizonsHeroes::perkDefinition(state.getRules(), selection.skillId, selection.perkId);
		if(definition) missingTiers.erase(newHorizonsHeroes::perkRequiredRank(definition->requiredRank));
	}
	const auto remaining = state.getRules()["maxPerksPerSkill"].Integer() - count;
	const auto before = std::max<si64>(0, static_cast<si64>(missingTiers.size()) - remaining);
	missingTiers.erase(candidate.requiredRank);
	const auto after = std::max<si64>(0, static_cast<si64>(missingTiers.size()) - (remaining - 1));
	return after > before;
}
}

namespace NK2AI
{
int AIGateway::chooseLevelUpAdvancement(const HeroPtr & heroPtr, const std::vector<SecondarySkill> & skills,
	const std::vector<newHorizonsHeroes::PerkOfferCandidate> & perks)
{
	int sel = 0;

	if(heroPtr.isVerified())
	{
		std::unique_lock lockGuard(nullkiller->aiStateMutex);
		nullkiller->heroManager->update();
		if(!skills.empty())
			sel = nullkiller->heroManager->selectBestSkillIndex(heroPtr, skills);
		if(!perks.empty())
		{
			const auto * observedHero = heroPtr.getUnverified();
			std::vector<size_t> usablePerks;
			if(observedHero)
			{
				for(size_t index = 0; index < perks.size(); ++index)
				{
					const auto & candidate = perks[index];
					try
					{
						const auto definition = newHorizonsHeroes::perkDefinition(
							observedHero->getPerkState().getRules(),
							candidate.selection.skillId, candidate.selection.perkId);
						// Level-up offers are server-authored, but retain this check at
						// the AI boundary so a stale/legacy planned entry is never
						// deliberately selected by Nullkiller.
						if(definition
							&& definition->effect["status"].String() == "active"
							&& candidate.requiredRank == newHorizonsHeroes::perkRequiredRank(definition->requiredRank)
							&& observedHero->getPerkState().canAcquirePerk(
								candidate.selection.skillId, candidate.selection.perkId,
								observedHero->getPerkSkillRank(candidate.selection.skillId)))
							usablePerks.push_back(index);
					}
					catch(const std::exception &)
					{
						// The authoritative query will reject malformed data. Do not
						// turn an invalid candidate into an AI-selected answer.
					}
				}
			}

			if(usablePerks.empty())
				return skills.empty() ? -1 : sel;

			const auto ownFactionSkill = observedHero
				? observedHero->compiledDevelopment()->factionSkill(observedHero->getFactionID())
				: std::nullopt;
			const auto isFactionPerk = [observedHero, &ownFactionSkill, &perks](size_t index)
			{
				if(!observedHero || !ownFactionSkill)
					return false;
				const int decoded = SecondarySkill::decode(perks[index].selection.skillId);
				return decoded >= 0 && observedHero->compiledDevelopment()->isFactionSkillForFaction(observedHero->getFactionID(), SecondarySkill(decoded));
			};
			const auto inspirationalLeaderScore = [observedHero, &perks](size_t index)
			{
				if(!observedHero
					|| perks[index].selection.perkId
						!= "new-horizons:discipline.inspirationalLeader")
					return 0;

				// Inspirational Leader scales with the number of friendly
				// activations that can benefit from a morale trigger. Use the
				// currently visible positive morale and army size as a stable
				// valuation signal instead of selecting every basic perk blindly.
				const int positiveMorale = std::max(0, observedHero->moraleVal());
				const int armyStacks = std::max(1, static_cast<int>(observedHero->Slots().size()));
				return positiveMorale * armyStacks;
			};
			// Prefer preserving later tiers when same-tier alternatives are legal.
			// The player remains free to make that irreversible tradeoff.
			const auto & perkRules = observedHero->getPerkState().getRules();
			if(newHorizonsHeroes::usesFourOfferProgression(perkRules)
				|| (newHorizonsHeroes::usesR2PerkOffers(perkRules)
					&& !newHorizonsHeroes::usesStrictPerkProgression(perkRules)))
			{
				std::vector<size_t> preferredPerks;
				std::copy_if(usablePerks.begin(), usablePerks.end(), std::back_inserter(preferredPerks),
					[&](size_t index) { return !worsensTierReservation(*observedHero, perks[index]); });
				if(preferredPerks.empty() && !skills.empty())
					return sel;
				if(!preferredPerks.empty())
					usablePerks = std::move(preferredPerks);
				// Forced legal historical offers still require an answer.
			}
			const auto best = std::max_element(usablePerks.begin(), usablePerks.end(),
				[&](size_t leftIndex, size_t rightIndex)
				{
					const auto & left = perks[leftIndex];
					const auto & right = perks[rightIndex];
					return std::tuple{
						isFactionPerk(leftIndex), leadershipAI::perkGain(*observedHero, left), inspirationalLeaderScore(leftIndex),
						left.requiredRank, left.selection.perkId}
						< std::tuple{
							isFactionPerk(rightIndex), leadershipAI::perkGain(*observedHero, right), inspirationalLeaderScore(rightIndex),
							right.requiredRank, right.selection.perkId};
				});
			// Advanced/Expert perks are specialized enough to outrank another
			// generic skill roll. Basic perks remain the fallback when no skill
			// choice exists, preserving early-game skill development. A valid
			// faction perk is deliberately preferred even at Basic: it is the
			// hero's faction identity and is the intended early-game path.
			if(skills.empty() || isFactionPerk(*best)
				|| perks[*best].requiredRank >= 2 || inspirationalLeaderScore(*best) > 0)
				sel = static_cast<int>(skills.size() + *best);
		}
	}
	return sel;
}
}
