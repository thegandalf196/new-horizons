/*
 * HeroLevelProgression.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "CGameHandler.h"
#include "queries/MapQueries.h"
#include "queries/QueriesProcessor.h"
#include "../lib/GameLibrary.h"
#include "../lib/CSkillHandler.h"
#include "../lib/CPlayerState.h"
#include "../lib/callback/GameRandomizer.h"
#include "../lib/entities/hero/CHeroHandler.h"
#include "../lib/mapObjects/army/CCommanderInstance.h"
#include "../lib/gameState/CGameState.h"
#include "../lib/mapObjects/CGHeroInstance.h"
#include "../lib/mapObjects/CGTownInstance.h"
#include "../lib/mapping/CMap.h"
#include "../lib/networkPacks/PacksForClient.h"

namespace
{
bool hasHeroLevelQuery(const QueriesProcessor & queries, ObjectInstanceID hero)
{
	for(const auto & query : queries.allQueries())
		if(query->getType() == CHeroLevelUpDialogQuery::TYPE
			&& static_cast<const CHeroLevelUpDialogQuery &>(*query).hero->id == hero)
			return true;
	return false;
}
}

void CGameHandler::levelUpHero(const CGHeroInstance * hero, SecondarySkill skill, bool continueProgression)
{
	if(hero && newHorizonsHeroes::usesPerkRules(hero->getPerkState().rules))
	{
		const auto * definition = LIBRARY->skillh->getById(skill);
		if(!definition || !hero->getPerkState().canAdvanceSkillNormally(definition->getJsonKey(),
			hero->getSecSkillLevel(skill)))
			throw std::runtime_error("New Horizons skill advancement requires its preceding perk tier");
	}
	changeSecSkill(hero, skill, 1, ChangeValueMode::RELATIVE);
	if(continueProgression)
		heroLevelUpChoiceDone(hero);
}

void CGameHandler::changeSecSkill(const CGHeroInstance * hero, SecondarySkill which, int val, ChangeValueMode mode)
{
	if(!hero)
	{
		logGlobal->error("changeSecSkill provided no hero");
		return;
	}
	const auto replaced = newHorizonsMagic::replacementSkill(hero->getMagicRules(), which);
	const auto skill = newHorizonsHeroes::normalizeRewardSkill(hero->getPrimaryGrowthRules(), replaced);
	if(!skill)
	{
		// Map/script rewards must not resurrect retired Skills.
		logGlobal->warn("Ignoring retired or unavailable secondary skill %s for hero %s",
			SecondarySkill::encode(which.getNum()), hero->nodeName());
		return;
	}
	const int oldRank = hero->getSecSkillLevel(*skill);
	SetSecSkill change;
	change.id = hero->id;
	change.which = *skill;
	change.val = val;
	change.mode = mode;
	sendAndApply(change);
	const int newRank = hero->getSecSkillLevel(*skill);
	if(!hero->hasPendingCommandLevelEvent())
		offerHeroCommandEnhancement(hero);
	if(const auto * town = hero->getVisitedTown())
		giveSpells(town, hero, hero == town->getVisitingHero());
	if(hero->getOwner().isValidPlayer())
		changeFogOfWar(hero->getSightCenter(), hero->getSightRadius(), hero->getOwner(), ETileVisibility::REVEALED);

	if(newRank > oldRank && newHorizonsHeroes::usesPerkRules(hero->getPerkState().rules))
	{
		const auto * def = LIBRARY->skillh->getById(*skill);
		if(def)
		{
			const auto skillId = def->getJsonKey();
			const auto * pState = gameInfo().getPlayerState(hero->getOwner());
			const bool isHuman = pState && pState->isHuman();

			for(int rank = oldRank + 1; rank <= newRank && rank <= 3; ++rank)
			{
				const auto candidates = hero->getPerkState().candidatesForSkillTier(skillId, rank);
				if(!candidates.empty())
				{
					if(!isHuman)
					{
						levelUpHero(hero, candidates, 0, 0, false);
					}
					else
					{
						HeroLevelUp perkHlu;
						perkHlu.player = hero->getOwner();
						perkHlu.heroId = hero->id;
						perkHlu.skills.clear();
						perkHlu.perks = candidates;
						perkHlu.perkOfferSeed = 0;
						perkHlu.reachedLevel = hero->level;
						auto perkQuery = std::make_shared<CHeroLevelUpDialogQuery>(this, perkHlu, hero);
						queries->addQuery(perkQuery);
					}
				}
			}
		}
	}
}

void CGameHandler::levelUpHero(const CGHeroInstance * hero,
	const std::vector<newHorizonsHeroes::PerkOfferCandidate> & offer, size_t choice, uint64_t seed,
	bool continueProgression)
{
	if(!hero)
		throw std::runtime_error("Cannot choose a perk for a missing hero");
	const auto rankLookup = [hero](const std::string & skillId)
	{
		return hero->getPerkSkillRank(skillId);
	};
	// Validate without mutating authoritative state. The replicated pack below is
	// the sole state-change path for the server and every connected client.
	auto validated = hero->getPerkState();
	validated.acceptOffer(offer, choice, rankLookup, seed, hero->getPerkOfferWeightContext());

	HeroPerkChosen chosen;
	chosen.hero = hero->id;
	chosen.selection = offer.at(choice).selection;
	const bool prodigy = chosen.selection.perkId == "new-horizons:learning.prodigy";
	const int previousSight = hero->getSightRadius();
	sendAndApply(chosen);
	if(prodigy)
		pendingProdigyLevels[hero->id] = 3;
	if(hero->getOwner().isValidPlayer() && hero->getSightRadius() > previousSight)
		changeFogOfWar(hero->getSightCenter(), hero->getSightRadius(), hero->getOwner(), ETileVisibility::REVEALED);
	if(continueProgression)
		heroLevelUpChoiceDone(hero);
}

void CGameHandler::heroLevelUpChoiceDone(const CGHeroInstance * hero)
{
	if(hero->hasPendingCommandLevelEvent())
	{
		HeroCommandProgression event;
		event.hero = hero->id;
		event.level = hero->level;
		event.authorityRank = hero->getPerkSkillRank("new-horizons:authority");
		sendAndApply(event);
	}
	if(offerHeroCommandEnhancement(hero) || hasHeroLevelQuery(*queries, hero->id))
		return;
	if(!offerHeroMastery(hero))
		expGiven(hero);
}

bool CGameHandler::offerHeroMastery(const CGHeroInstance * hero)
{
	if(!hero->getOwner().isValidPlayer())
		return false;
	for(const auto & existing : queries->allQueries())
		if(existing->getType() == CHeroMasteryDialogQuery::TYPE
			&& static_cast<const CHeroMasteryDialogQuery &>(*existing).heroId == hero->id)
			return true;
	if(const auto offer = hero->prepareMasteryOffer())
	{
		if(hero->getSecSkillLevel(offer->skill) != MasteryLevel::EXPERT)
			return false;
		HeroMasteryOffer pack;
		pack.offer = *offer;
		sendAndApply(pack);
	}
	if(!hero->getMasteryState().pending
		|| hero->getSecSkillLevel(hero->getMasteryState().pending->skill) != MasteryLevel::EXPERT)
		return false;
	queries->addQuery(std::make_shared<CHeroMasteryDialogQuery>(this, hero));
	return true;
}

void CGameHandler::resumeMasteryQueries(PlayerColor player)
{
	if(!player.isValidPlayer() || !uiReadyForDialogs.contains(player) || queries->topQuery(player))
		return;
	const auto * state = gameInfo().getPlayerState(player);
	if(!state)
		return;
	for(const auto * hero : state->getHeroes())
		if(offerHeroCommandEnhancement(hero) || offerHeroMastery(hero))
			return;
}

void CGameHandler::levelUpHero(const CGHeroInstance * hero)
{
	if(hasHeroLevelQuery(*queries, hero->id))
		return;
	if(offerHeroCommandEnhancement(hero))
		return;
	if(hero->getMasteryState().pending && offerHeroMastery(hero))
		return;
	// required exp for at least 1 lvl-up hasn't been reached
	if (!hero->gainsLevel())
	{
		if (hero->getCommander() && hero->getCommander()->gainsLevel())
			levelUpCommander(hero->getCommander());
		return;
	}

	const bool artilleryExpertBeforeGain = hero->getSecSkillLevel(SecondarySkill::ARTILLERY) == MasteryLevel::EXPERT;
	const bool logisticsExpertBeforeGain = hero->getSecSkillLevel(SecondarySkill::LOGISTICS) == MasteryLevel::EXPERT
		&& newHorizonsHeroes::masteryOptions(hero->getMasteryState().rules, SecondarySkill::LOGISTICS).has_value();
	// give primary skill
	logGlobal->trace("%s got level %d", hero->getNameTextID(), hero->level);
	auto gains = randomizer->rollPrimarySkillsForLevelup(hero);
	const auto primarySkill = PrimarySkill(std::distance(gains.begin(), std::max_element(gains.begin(), gains.end())));
	for(int i = 0; i < GameConstants::PRIMARY_SKILLS; ++i)
	{
		const auto before = hero->getBasePrimarySkillValue(PrimarySkill(i));
		if(gains[i])
		{
			SetPrimarySkill sps;
			sps.id = hero->id;
			sps.which = PrimarySkill(i);
			sps.mode = ChangeValueMode::RELATIVE;
			sps.val = gains[i];
			sendAndApply(sps);
		}
		gains[i] = hero->getBasePrimarySkillValue(PrimarySkill(i)) - before;
	}

	HeroLevelUp hlu;
	hlu.player = hero->tempOwner;
	hlu.heroId = hero->id;
	hlu.primskill = primarySkill;
	hlu.primaryGains = gains;
	hlu.artilleryExpertBeforeGain = artilleryExpertBeforeGain;
	hlu.logisticsExpertBeforeGain = logisticsExpertBeforeGain;
	newHorizonsLevelUp::prepareChoices(*this, hero, hlu);

	const int reachedLevel = hero->level + 1;
	if(newHorizonsHeroes::quickStudyRerollInterval(hero->getPerkState().rules))
	{
		hlu.reachedLevel = reachedLevel;
		hlu.optionalRerollAvailable = hero->hasActivePerk("new-horizons:learning", "new-horizons:learning.connoisseur")
			&& !hlu.skills.empty();
	}

	if (!hero->getOwner().isValidPlayer())
	{
		sendAndApply(hlu);
		if(hlu.skills.empty() && hlu.perks.empty())
			heroLevelUpChoiceDone(hero);
		else if(!hlu.skills.empty())
			levelUpHero(hero, hlu.skills.front());
		else
			levelUpHero(hero, hlu.perks, 0, hlu.perkOfferSeed);
	}
	else
	{
		auto levelUpQuery = std::make_shared<CHeroLevelUpDialogQuery>(this, hlu, hero);
		queries->addQuery(levelUpQuery);
		//level up will be called on query reply
	}
}

void CGameHandler::expGiven(const CGHeroInstance * hero)
{
	if(hero->gainsLevel())
	{
		levelUpHero(hero);
		return;
	}
	const auto pending = pendingProdigyLevels.find(hero->id);
	if(pending != pendingProdigyLevels.end())
	{
		ui32 maximum = LIBRARY->heroh->maxSupportedLevel();
		if(gameState().getMap().levelLimit > 0)
			maximum = std::min<ui32>(maximum, gameState().getMap().levelLimit);
		if(hero->level < maximum && pending->second > 0)
		{
			// Spend before publishing XP: nested progression cannot replay the event.
			if(--pending->second == 0)
				pendingProdigyLevels.erase(pending);
			const auto span = static_cast<TExpType>(LIBRARY->heroh->reqExp(hero->level + 1)
				- LIBRARY->heroh->reqExp(hero->level));
			giveExperienceWithoutLevelUp(hero, span);
			levelUpHero(hero); // Ordinary primary/specialty growth and mandatory choices.
			return;
		}
		pendingProdigyLevels.erase(pending);
	}
	if(hero->getCommander() && hero->getCommander()->gainsLevel())
		levelUpCommander(hero->getCommander());
}


void CGameHandler::validatePendingProdigyLevels() const
{
	for(const auto & [id, count] : pendingProdigyLevels)
	{
		const auto * hero = gameState().getHero(id);
		if(!hero || count < 1 || count > 3
			|| !hero->hasActivePerk("new-horizons:learning", "new-horizons:learning.prodigy"))
			throw std::runtime_error("Invalid pending Prodigy progression");
	}
}
