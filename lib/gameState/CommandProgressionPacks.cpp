/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "GameStatePackVisitor.h"
#include "CGameState.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../mapObjects/NewHorizonsLevelUpOffer.h"

void GameStatePackVisitor::visitSetSecSkill(SetSecSkill & pack)
{
	auto * hero = gs.getHero(pack.id);
	if(!hero)
		throw std::runtime_error("Secondary Skill change for a missing hero");
	const auto previousRank = hero->getSecSkillLevel(pack.which);
	hero->setSecSkillLevel(pack.which, pack.val, pack.mode);
	if(!hero->hasPendingCommandLevelEvent())
		hero->captureCommandProgression();
	if(hero->getSecSkillLevel(pack.which) < previousRank)
		spellPointHeroes.insert(pack.id);
}

void GameStatePackVisitor::visitHeroLevelUp(HeroLevelUp & pack)
{
	const auto * hero = gs.getHero(pack.heroId);
	if(hero && pack.replacesQueryID == QueryID::NONE && hero->hasPendingCommandLevelEvent())
		throw std::runtime_error("Previous ordinary level choice is unfinished");
	newHorizonsHeroes::applyLevelUpOffer(gs.getHero(pack.heroId), pack);
	if(pack.replacesQueryID == QueryID::NONE)
		gs.getHero(pack.heroId)->beginCommandLevelEvent();
}

void GameStatePackVisitor::visitHeroPerkChosen(HeroPerkChosen & pack)
{
	auto * hero = gs.getHero(pack.hero);
	if(!hero)
		throw std::runtime_error("Perk choice for a missing hero");
	hero->applyPerkSelection(pack.selection);
}

void GameStatePackVisitor::visitHeroMasteryOffer(HeroMasteryOffer & pack)
{
	auto * hero = gs.getHero(pack.offer.hero);
	if(!hero)
		throw std::runtime_error("Mastery offer for a missing hero");
	hero->applyMasteryOffer(pack.offer);
}

void GameStatePackVisitor::visitHeroMasteryChosen(HeroMasteryChosen & pack)
{
	auto * hero = gs.getHero(pack.hero);
	if(!hero)
		throw std::runtime_error("Mastery choice for a missing hero");
	hero->applyMasteryChoice(pack.sequence, pack.choice);
}

void GameStatePackVisitor::visitHeroCommandProgression(HeroCommandProgression & pack)
{
	auto * hero = gs.getHero(pack.hero);
	if(!hero)
		throw std::runtime_error("Command level event for a missing hero");
	hero->completeCommandLevelEvent(pack.level, pack.authorityRank);
}

void GameStatePackVisitor::visitHeroCommandEnhancementChosen(HeroCommandEnhancementChosen & pack)
{
	auto * hero = gs.getHero(pack.offer.hero);
	const auto command = hero ? hero->getArmyCommand() : std::nullopt;
	if(!hero || !command || hero->hasPendingCommandLevelEvent()
		|| !newHorizonsHeroes::validCommandEnhancementReply(pack.offer, hero->id, hero->getOwner(),
			pack.offer.milestone, pack.choice, hero->getCommandState(), hero->getArmyCommandRules(), command->effective()))
		throw std::runtime_error("Invalid replicated Command enhancement choice");
	hero->selectCommandEnhancement(pack.offer.milestone, pack.offer.options.at(pack.choice).enhancement);
}
