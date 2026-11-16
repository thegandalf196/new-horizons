/* Hero level-up callbacks, part of VCMI / New Horizons; GPL v2.0 or later. */
#include "StdInc.h"
#include "AIGateway.h"
#include "Helpers/LeadershipEvaluation.h"
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
	executeActionAsync("connoisseurDecision", [this, heroPtr, heroId = hero->id, pskill, skills, perks, queryID, reachedLevel]() mutable
	{
		std::optional<size_t> reroll;
		if(heroPtr.isVerified())
		{
			std::unique_lock lock(nullkiller->aiStateMutex);
			nullkiller->heroManager->update();
			reroll = nullkiller->heroManager->connoisseurSkillCard(heroPtr, skills, reachedLevel);
		}
		if(reroll && levelUpOffers.reroll(heroId, queryID))
		{
			answerQuery(queryID, -2 - static_cast<int32_t>(*reroll));
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
		answerQuery(queryID, chooseLevelUpAdvancement(heroPtr, skills, perks));
	});
}

}
