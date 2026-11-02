/* Hero level-up callbacks, part of VCMI / New Horizons; GPL v2.0 or later. */
#include "StdInc.h"
#include "AIGateway.h"
#include "Analyzers/HeroManager.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/entities/hero/NewHorizonsPerkRules.h"
#include <algorithm>
#include <set>
#include <tuple>

namespace NK2AI
{
void AIGateway::heroGotLevel(const CGHeroInstance * hero, PrimarySkill pskill,
	std::vector<SecondarySkill> & skills,
	const std::vector<newHorizonsHeroes::PerkOfferCandidate> & perks, QueryID queryID)
{
	heroGotLevelImpl(hero, pskill, skills, perks, queryID, true);
}

void AIGateway::heroGotLevelWithReroll(const CGHeroInstance * hero, PrimarySkill pskill,
	std::vector<SecondarySkill> & skills,
	const std::vector<newHorizonsHeroes::PerkOfferCandidate> & perks, QueryID queryID,
	bool optionalRerollAvailable, QueryID replacesQueryID, int reachedLevel)
{
	if(!hero)
		return;
	if(queryID < QueryID(0) || reachedLevel == 0)
	{
		heroGotLevelImpl(hero, pskill, skills, perks, queryID, true);
		return;
	}
	const auto player = cc->getPlayerID();
	if(!player || hero->getOwner() != *player)
		return;
	const auto event = levelUpOffers.observe(hero->id, queryID, replacesQueryID, optionalRerollAvailable);
	if(event == LevelUpOfferState::Event::IGNORE)
		return;
	const bool registerQuery = event != LevelUpOfferState::Event::CONFIRMED_REJECTION;
	if(registerQuery)
		status.addQuery(queryID, "Hero level-up offer");
	if(!optionalRerollAvailable || event != LevelUpOfferState::Event::INITIAL)
	{
		if(levelUpOffers.select(hero->id, queryID))
			heroGotLevelImpl(hero, pskill, skills, perks, queryID, false);
		return;
	}
	HeroPtr heroPtr(hero, cc.get());
	executeActionAsync("quickStudyDecision", [this, heroPtr, heroId = hero->id, pskill, skills, perks, queryID, reachedLevel]() mutable
	{
		bool reroll = false;
		if(heroPtr.isVerified())
		{
			std::unique_lock lock(nullkiller->aiStateMutex);
			nullkiller->heroManager->update();
			reroll = nullkiller->heroManager->shouldRerollLevelUpOffer(heroPtr, skills, perks, reachedLevel);
		}
		if(reroll && levelUpOffers.reroll(heroId, queryID))
		{
			answerQuery(queryID, -1);
		}
		else if(levelUpOffers.select(heroId, queryID))
		{
			if(heroPtr.isVerified())
				heroGotLevelImpl(heroPtr.getUnverified(), pskill, skills, perks, queryID, false);
		}
	});
}

void AIGateway::heroGotLevelImpl(const CGHeroInstance * hero, PrimarySkill pskill, std::vector<SecondarySkill> & skills,
	const std::vector<newHorizonsHeroes::PerkOfferCandidate> & perks, QueryID queryID,
	bool registerQuery)
{
	LOG_TRACE_PARAMS(logAi, "queryID '%i'", queryID);
	if(registerQuery)
		status.addQuery(queryID, boost::str(boost::format("Hero %s got level %d") % hero->getNameTextID() % hero->level));
	HeroPtr heroPtr(hero, cc.get());

	executeActionAsync("heroGotLevel", [this, heroPtr, skills, perks, queryID]()
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
								observedHero->getPerkState().rules,
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
					return answerQuery(queryID, skills.empty() ? -1 : sel);

				const auto ownFactionSkill = observedHero
					? newHorizonsHeroes::factionSkill(observedHero->getPrimaryGrowthRules(), observedHero->getFactionID())
					: std::nullopt;
				const auto isFactionPerk = [observedHero, &ownFactionSkill, &perks](size_t index)
				{
					if(!observedHero || !ownFactionSkill)
						return false;
					const int decoded = SecondarySkill::decode(perks[index].selection.skillId);
					return decoded >= 0 && newHorizonsHeroes::isFactionSkillForFaction(
						observedHero->getPrimaryGrowthRules(), observedHero->getFactionID(), SecondarySkill(decoded));
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
				// Only historical R2 worlds can spend several slots on one tier.
				// Strict progression makes reservation unnecessary; retain the old
				// valuation solely for those captured historical rules.
				const auto & perkRules = observedHero->getPerkState().rules;
				if(newHorizonsHeroes::usesR2PerkOffers(perkRules)
					&& !newHorizonsHeroes::usesStrictPerkProgression(perkRules))
				{
					const auto worsensTierReservation = [observedHero, &perks](size_t index)
					{
						const auto & state = observedHero->getPerkState();
						const auto & candidate = perks[index];
						std::set<int> missingTiers;
						for(const auto & definition : newHorizonsHeroes::perkOptions(state.rules, candidate.selection.skillId))
							if(definition.effect["status"].String() == "active")
								missingTiers.insert(newHorizonsHeroes::perkRequiredRank(definition.requiredRank));

						si64 count = 0;
						for(const auto & selection : state.selected)
						{
							if(selection.skillId != candidate.selection.skillId)
								continue;
							++count;
							const auto definition = newHorizonsHeroes::perkDefinition(state.rules,
								selection.skillId, selection.perkId);
							if(definition)
								missingTiers.erase(newHorizonsHeroes::perkRequiredRank(definition->requiredRank));
						}
						const auto remaining = state.rules["maxPerksPerSkill"].Integer() - count;
						const auto before = std::max<si64>(0, static_cast<si64>(missingTiers.size()) - remaining);
						missingTiers.erase(candidate.requiredRank);
						const auto after = std::max<si64>(0, static_cast<si64>(missingTiers.size()) - (remaining - 1));
						return after > before;
					};
					std::vector<size_t> preferredPerks;
					std::copy_if(usablePerks.begin(), usablePerks.end(), std::back_inserter(preferredPerks),
						[&](size_t index) { return !worsensTierReservation(index); });
					if(preferredPerks.empty() && !skills.empty())
						return answerQuery(queryID, sel);
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
							isFactionPerk(leftIndex), inspirationalLeaderScore(leftIndex),
							left.requiredRank, left.selection.perkId}
							< std::tuple{
								isFactionPerk(rightIndex), inspirationalLeaderScore(rightIndex),
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

		answerQuery(queryID, sel);
	});
}

}
