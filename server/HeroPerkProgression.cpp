/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CGameHandler.h"
#include "queries/MapQueries.h"
#include "queries/QueriesProcessor.h"
#include "../lib/CPlayerState.h"
#include "../lib/entities/hero/NewHorizonsPerkState.h"
#include "../lib/gameState/CGameState.h"
#include "../lib/mapObjects/CGHeroInstance.h"
#include <algorithm>
#include <stdexcept>

namespace
{
bool hasProgressionQuery(const QueriesProcessor & queries, ObjectInstanceID hero)
{
	for(const auto & query : queries.allQueries())
		if(query->getType() == CHeroLevelUpDialogQuery::TYPE
			&& static_cast<const CHeroLevelUpDialogQuery &>(*query).hero->id == hero)
			return true;
	return false;
}

void validateOffer(const CGHeroInstance & hero, const HeroLevelUp & offer)
{
	if(!newHorizonsHeroes::usesUnifiedSkillProgression(hero.getPerkState().rules)
		|| offer.heroId != hero.id || offer.player != hero.getOwner()
		|| !offer.player.isValidPlayer() || offer.perkOfferSeed != 0
		|| offer.replacesQueryID != QueryID::NONE)
		throw std::runtime_error("Invalid saved unified hero progression identity");
	if(offer.perkOnly)
	{
		const auto expected = hero.getPerkState().nextRequiredPerkOffer([&](const std::string & skill)
			{ return hero.getPerkSkillRank(skill); });
		if(!offer.skills.empty() || expected.empty() || offer.perks != expected
			|| offer.reachedLevel != hero.level || offer.optionalRerollAvailable
			|| std::any_of(offer.primaryGains.begin(), offer.primaryGains.end(), [](int gain) { return gain != 0; }))
			throw std::runtime_error("Invalid saved mandatory perk offer");
	}
	else if(!offer.perks.empty() || offer.skills.size() > newHorizonsHeroes::PERK_MAX_SKILL_CHOICES
		|| offer.reachedLevel != hero.level + (offer.restoredOffer ? 0 : 1))
		throw std::runtime_error("Invalid saved Skill advancement offer");
}
}

void CGameHandler::rememberHeroProgressionOffer(const HeroLevelUp & offer)
{
	const auto * hero = gameState().getHero(offer.heroId);
	if(!hero || !newHorizonsHeroes::usesUnifiedSkillProgression(hero->getPerkState().rules))
		return;
	auto saved = offer;
	saved.replacesQueryID = QueryID::NONE;
	validateOffer(*hero, saved);
	pendingHeroLevelUps[hero->id] = std::move(saved);
}

void CGameHandler::validatePendingHeroProgression() const
{
	for(const auto & [id, offer] : pendingHeroLevelUps)
	{
		const auto * hero = gameState().getHero(id);
		if(!hero || offer.heroId != id)
			throw std::runtime_error("Saved hero progression has no matching hero");
		validateOffer(*hero, offer);
	}
}

bool CGameHandler::restoreHeroProgressionOffer(const CGHeroInstance * hero)
{
	if(!hero) return false;
	const auto saved = pendingHeroLevelUps.find(hero->id);
	if(saved == pendingHeroLevelUps.end()) return false;
	validateOffer(*hero, saved->second);
	if(hero->getOwner().isValidPlayer() && queries->topQuery(hero->getOwner())) return true;
	if(!hasProgressionQuery(*queries, hero->id))
		queries->addQuery(std::make_shared<CHeroLevelUpDialogQuery>(this, saved->second, hero));
	return true;
}

bool CGameHandler::offerHeroRequiredPerk(const CGHeroInstance * hero)
{
	if(!hero || !newHorizonsHeroes::usesUnifiedSkillProgression(hero->getPerkState().rules))
		return false;
	if(deferredPerkPrompts.contains(hero->id) || hasProgressionQuery(*queries, hero->id))
		return true;
	const auto * player = gameInfo().getPlayerState(hero->getOwner());
	const bool interactive = player && hero->getOwner().isValidPlayer();
	for(size_t count = 0; count < newHorizonsHeroes::PERK_CANONICAL_SKILL_COUNT * newHorizonsHeroes::PERK_MAX_PERKS_PER_SKILL; ++count)
	{
		const auto offer = hero->getPerkState().nextRequiredPerkOffer([hero](const std::string & skill)
			{ return hero->getPerkSkillRank(skill); });
		if(offer.empty()) return false;
		// Complete a map-object/battle continuation before exposing a saved
		// progression transaction. Its transient query graph is not save data.
		if(hero->getOwner().isValidPlayer() && queries->topQuery(hero->getOwner())) return true;
		if(!interactive)
		{
			// Stable captured-registry order; this completion uses neither a player
			// modal nor random draws and still applies the replicated validated pack.
			levelUpHero(hero, offer, 0, 0, false);
			continue;
		}
		HeroLevelUp pending;
		pending.player = hero->getOwner();
		pending.heroId = hero->id;
		pending.perks = offer;
		pending.reachedLevel = hero->level;
		pending.perkOnly = true;
		queries->addQuery(std::make_shared<CHeroLevelUpDialogQuery>(this, pending, hero));
		return true;
	}
	throw std::runtime_error("Mandatory perk completion exceeded the captured registry");
}

void CGameHandler::detachHeroProgression(const CGHeroInstance * hero, bool removed)
{
	if(!hero || !newHorizonsHeroes::usesUnifiedSkillProgression(hero->getPerkState().rules)) return;
	std::vector<QueryPtr> stale;
	for(const auto & query : queries->allQueries())
		if(const auto * level = dynamic_cast<const CHeroLevelUpDialogQuery *>(query.get());
			level && level->hero == hero)
			stale.push_back(query);
	for(const auto & query : stale)
	{
		queries->discardQuery(query);
		// No grant, object callback or stack listener may run against the former
		// owner or a hero about to be destroyed. A held query also becomes inert.
		static_cast<CHeroLevelUpDialogQuery &>(*query).hero = nullptr;
		sendQueryResolved(query->queryID);
	}
	if(removed)
	{
		pendingHeroLevelUps.erase(hero->id);
		pendingProdigyLevels.erase(hero->id);
		deferredPerkPrompts.erase(hero->id);
	}
}

void CGameHandler::resumeTransferredHeroProgression(const CGHeroInstance * hero, PlayerColor previousOwner)
{
	if(!hero || !newHorizonsHeroes::usesUnifiedSkillProgression(hero->getPerkState().rules)) return;
	const auto saved = pendingHeroLevelUps.find(hero->id);
	const bool hadSavedOffer = saved != pendingHeroLevelUps.end();
	if(saved != pendingHeroLevelUps.end())
	{
		saved->second.player = hero->getOwner();
		if(!hero->getOwner().isValidPlayer())
		{
			const auto offer = saved->second;
			pendingHeroLevelUps.erase(saved);
			// Ownerless heroes have no recipient interface. Preserve any already
			// applied growth and finish their exact pending choice deterministically.
			if(!offer.perkOnly)
			{
				auto applied = offer;
				sendAndApply(applied);
				if(!offer.skills.empty()) levelUpHero(hero, offer.skills.front(), false);
			}
			heroLevelUpChoiceDone(hero);
		}
	}
	if(!hero->getOwner().isValidPlayer() && !hadSavedOffer) heroLevelUpChoiceDone(hero);
	if(previousOwner.isValidPlayer()) resumeMasteryQueries(previousOwner);
	if(hero->getOwner().isValidPlayer()) resumeMasteryQueries(hero->getOwner());
}
