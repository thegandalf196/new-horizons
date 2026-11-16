/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "queries/HeroLevelUpOffer.h"
#include "queries/MapQueries.h"
#include "queries/QueriesProcessor.h"
#include "CGameHandler.h"
#include "../lib/GameLibrary.h"
#include "../lib/CSkillHandler.h"
#include "../lib/CPlayerState.h"
#include "../lib/callback/IGameInfoCallback.h"
#include "../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../lib/mapObjects/CGHeroInstance.h"
#include <algorithm>

bool newHorizonsLevelUp::admitExternalSkillRank(const CGHeroInstance & hero, SecondarySkill skill, int targetRank)
{
	const auto & state = hero.getPerkState();
	if(newHorizonsHeroes::usesFourOfferProgression(state.rules))
	{
		const auto & rules = hero.getPrimaryGrowthRules();
		if(targetRank > hero.getSecSkillLevel(skill) && newHorizonsHeroes::isFactionSkill(rules, skill)
			&& !newHorizonsHeroes::isFactionSkillForFaction(rules, hero.getFactionID(), skill))
		{
			logGlobal->warn("Ignoring secondary Skill grant: the Skill belongs to another faction");
			return false;
		}
		const bool freeSlot = hero.getSecSkillLevel(skill) > 0 || targetRank == 0 || hero.canLearnSkill();
		if(!freeSlot) logGlobal->warn("Ignoring secondary Skill grant: the hero has no free Skill slot");
		return freeSlot;
	}
	if(newHorizonsHeroes::usesUnifiedSkillProgression(state.rules))
	{
		auto proposed = state;
		// Legacy admission proves all required future choices exist before
		// publishing the rank. It does not select for an interactive player.
		proposed.selectMissingPerksDeterministically([&](const std::string & skillId)
		{
			return skillId == SecondarySkill::encode(skill.getNum()) ? targetRank : hero.getPerkSkillRank(skillId);
		});
	}
	return true;
}

void newHorizonsLevelUp::completeSkillRank(CGameHandler & handler, const CGHeroInstance * hero, SecondarySkill skill)
{
	const auto & state = hero->getPerkState();
	if(!newHorizonsHeroes::usesPerkRules(state.rules) || newHorizonsHeroes::usesFourOfferProgression(state.rules))
		return; // Revised progression spends one advancement, never backfills perks.
	if(newHorizonsHeroes::usesUnifiedSkillProgression(state.rules))
	{
		handler.offerHeroRequiredPerk(hero);
		return;
	}
	const auto * definition = LIBRARY->skillh->getById(skill);
	if(!definition) return;
	const auto & skillId = definition->getJsonKey();
	const auto * player = handler.gameInfo().getPlayerState(hero->getOwner());
	const bool human = player && player->isHuman();
	for(int rank = 1; rank <= hero->getSecSkillLevel(skill) && rank <= 3; ++rank)
	{
		if(state.hasPerkForTier(skillId, rank)) continue;
		const auto candidates = state.candidatesForSkillTier(skillId, rank);
		if(candidates.empty()) continue;
		if(!human)
		{
			handler.levelUpHero(hero, candidates, 0, 0, false);
			continue;
		}
		HeroLevelUp offer;
		offer.player = hero->getOwner();
		offer.heroId = hero->id;
		offer.perks = candidates;
		offer.reachedLevel = hero->level;
		handler.queries->addQuery(std::make_shared<CHeroLevelUpDialogQuery>(&handler, offer, hero));
	}
}
