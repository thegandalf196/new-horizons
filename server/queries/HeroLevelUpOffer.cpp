/*
 * MapQueries.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "MapQueries.h"

#include "QueriesProcessor.h"
#include "../CGameHandler.h"
#include "../TurnTimerHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/entities/hero/SkillChoiceEligibility.h"
#include "../../lib/callback/IGameInfoCallback.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/MiscObjects.h"
#include "../../lib/networkPacks/PacksForServer.h"

#include "HeroLevelUpOffer.h"
#include "../../lib/callback/GameRandomizer.h"

CHeroLevelUpDialogQuery::CHeroLevelUpDialogQuery(CGameHandler * owner, const HeroLevelUp & Hlu, const CGHeroInstance * Hero):
	CDialogQuery(owner, TYPE), hero(Hero)
{
	hlu = Hlu;
	addPlayer(hero->tempOwner);
}

bool CHeroLevelUpDialogQuery::hasOptionalPolicy() const
{
	return newHorizonsHeroes::quickStudyRerollInterval(hero->getPerkState().rules).has_value();
}

bool CHeroLevelUpDialogQuery::canReroll() const
{
	const auto interval = newHorizonsHeroes::quickStudyRerollInterval(hero->getPerkState().rules);
	return prompted && !answer && hlu.optionalRerollAvailable && interval
		&& hlu.reachedLevel == hero->level && !hlu.skills.empty()
		&& hero->hasActivePerk("new-horizons:learning", "new-horizons:learning.connoisseur")
		&& !rerollCandidates().empty();
}

bool CHeroLevelUpDialogQuery::hasValidOffer() const
{
	if(hlu.perkOfferSeed < 0)
		return false;
	for(auto skill : hlu.skills)
		if(!newHorizonsHeroes::isValidLevelUpSkillChoice(gh->gameInfo(), hero, skill))
			return false;
	return hlu.perks == hero->getPerkState().prepareOffer([this](const std::string & skill)
		{ return hero->getPerkSkillRank(skill); }, hlu.perkOfferSeed, perkWeightContext());
}

bool CHeroLevelUpDialogQuery::confirmCurrentOffer(PlayerColor player, QueryID requestedID) const
{
	const auto * currentHero = gh->gameInfo().getHero(hlu.heroId);
	if(!currentHero || currentHero != hero || currentHero->getOwner() != player
		|| hlu.player != player || !vstd::contains(players, player)
		|| owner->topQuery(player).get() != this || requestedID != queryID
		|| hlu.queryID != queryID || queryID < 0 || !prompted || answer
		|| hlu.replacesQueryID != QueryID::NONE || hlu.reachedLevel != currentHero->level
		|| !hasOptionalPolicy())
		return false;
	try
	{
		if(!hasValidOffer())
			return false;
	}
	catch(const std::runtime_error &)
	{
		return false; // An invalid held offer cannot confirm a safe retry.
	}

	// Re-expose the held offer, not a newly generated pool or a spent reroll.
	// Equal query IDs distinguish this non-growing confirmation from replacement.
	auto confirmation = hlu;
	confirmation.replacesQueryID = queryID;
	confirmation.optionalRerollAvailable = canReroll();
	gh->sendAndApply(confirmation);
	return true;
}

std::set<SecondarySkill> CHeroLevelUpDialogQuery::rerollCandidates() const
{
	return newHorizonsHeroes::connoisseurSkillCandidates(gh->gameInfo(), *hero, hlu.skills);
}

bool CHeroLevelUpDialogQuery::requestReroll(PlayerColor player, size_t skillIndex)
{
	if(!vstd::contains(players, player) || hero->getOwner() != player
		|| owner->topQuery(player).get() != this)
		return false;
	if(!canReroll())
	{
		confirmCurrentOffer(player, queryID);
		return false;
	}
	if(skillIndex >= hlu.skills.size() || !hasValidOffer())
		return false;
	const auto candidates = rerollCandidates();
	// Rejection, including no legal alternative, is mutation- and RNG-free.
	if(candidates.empty())
		return false;
	auto replacement = hlu;
	replacement.skills[skillIndex] = gh->randomizer->rollSecondarySkillForLevelup(hero, candidates);
	replacement.optionalRerollAvailable = false;
	replacement.replacesQueryID = queryID;
	replacement.queryID = ++gh->QID;
	queryID = replacement.queryID;
	hlu = std::move(replacement);
	gh->sendAndApply(hlu);
	hlu.replacesQueryID = QueryID::NONE;
	return true;
}

void newHorizonsLevelUp::validateNoPendingOptionalOffer(const CGameHandler & handler)
{
	for(const auto & query : handler.queries->allQueries())
		if(const auto * levelUp = dynamic_cast<const CHeroLevelUpDialogQuery *>(query.get());
			levelUp && levelUp->hasOptionalPolicy())
			throw std::runtime_error("Choose a level-up option before saving: optional offers are not persistent");
}

const newHorizonsHeroes::PerkState::OfferWeightContext & CHeroLevelUpDialogQuery::perkWeightContext() const
{
	auto current = hero->getPerkOfferWeightContext();
	const auto & normalization = hero->getPerkState().rules["perkOfferNormalization"];
	if(current.mode == cachedPerkWeightContext.mode
		&& current.referenceWeights == cachedPerkWeightContext.referenceWeights
		&& normalization == cachedPerkNormalization)
		current.normalizedWeights = cachedPerkWeightContext.normalizedWeights;
	cachedPerkWeightContext = std::move(current);
	cachedPerkNormalization = normalization;
	return cachedPerkWeightContext;
}

void CHeroLevelUpDialogQuery::refreshChoicesBeforePrompt()
{
	std::erase_if(hlu.skills, [this](SecondarySkill skill)
	{
		return !newHorizonsHeroes::isValidLevelUpSkillChoice(gh->gameInfo(), hero, skill);
	});

	// Perk replies are validated against the complete deterministic offer, not a
	// filtered subset. Rebuild it from the saved seed and current hero ranks so
	// the offer reflects current eligibility while retaining seeded ordering.
	hlu.perks = hero->getPerkState().prepareOffer([this](const std::string & skillId)
	{
		return hero->getPerkSkillRank(skillId);
	}, hlu.perkOfferSeed, perkWeightContext());
	// All current Skill cards are excluded from the shared replacement pool,
	// so every valid card index has the same availability. Publish authority.
	hlu.optionalRerollAvailable = hlu.optionalRerollAvailable && !rerollCandidates().empty();
}

bool CHeroLevelUpDialogQuery::isValidReply(std::optional<int32_t> reply) const
{
	if(hasOptionalPolicy() && (!prompted || hero->getOwner() != hlu.player || hero->level != hlu.reachedLevel))
		return false;
	if(!reply || *reply < 0)
		return false;
	const size_t choiceCount = hlu.skills.size() + hlu.perks.size();
	if(choiceCount == 0)
		return *reply == 0;
	const size_t choice = static_cast<size_t>(*reply);
	if(choice >= choiceCount)
		return false;
	if(choice < hlu.skills.size())
		return newHorizonsHeroes::isValidLevelUpSkillChoice(gh->gameInfo(), hero, hlu.skills[choice]);
	try
	{
		auto validated = hero->getPerkState();
		validated.acceptOffer(hlu.perks, choice - hlu.skills.size(), [this](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		}, hlu.perkOfferSeed, perkWeightContext());
		return true;
	}
	catch(const std::exception &)
	{
		return false;
	}
}

void CHeroLevelUpDialogQuery::onRemoval(PlayerColor color)
{
	assert(answer && isValidReply(static_cast<int32_t>(*answer)));
	if(hlu.skills.empty() && hlu.perks.empty())
	{
		gh->sendQueryResolved(queryID);
		logGlobal->trace("Completing hero level-up query. %s gains no secondary skill", hero->getNameTextID());
		gh->heroLevelUpChoiceDone(hero);
		return;
	}

	const size_t choice = static_cast<size_t>(*answer);
	if(choice < hlu.skills.size())
	{
		logGlobal->trace("Completing hero level-up query. %s gains skill %d", hero->getNameTextID(), answer.value());
		// Apply the authoritative choice before acknowledging the dialog. The client may
		// refresh hero state as soon as QueryResolved arrives.
		gh->levelUpHero(hero, hlu.skills[choice], false);
		gh->sendQueryResolved(queryID);
		gh->heroLevelUpChoiceDone(hero);
		return;
	}
	const size_t perkChoice = choice - hlu.skills.size();
	auto validated = hero->getPerkState();
	validated.acceptOffer(hlu.perks, perkChoice, [this](const std::string & skillId)
	{
		return hero->getPerkSkillRank(skillId);
	}, hlu.perkOfferSeed, perkWeightContext());
	logGlobal->trace("Completing hero level-up query. %s gains perk %s", hero->getNameTextID(),
		hlu.perks.at(perkChoice).selection.perkId);
	// HeroPerkChosen is the authoritative state transition. It must precede the
	// query acknowledgement so clients never observe a resolved choice against
	// the old perk state. Progression chaining remains after the acknowledgement
	// to preserve the existing level-up packet ordering.
	gh->levelUpHero(hero, hlu.perks, perkChoice, hlu.perkOfferSeed, false);
	gh->sendQueryResolved(queryID);
	gh->heroLevelUpChoiceDone(hero);
}

void CHeroLevelUpDialogQuery::onAdded(PlayerColor color)
{
	if(prompted || answer)
		return;

	if(owner->topQuery(color).get() != this)
		return;

	if(!gh->uiReadyForDialogs.contains(color))
		return;

	refreshChoicesBeforePrompt();
	prompted = true;
	hlu.queryID = queryID;
	gh->sendAndApply(hlu);
}

void CHeroLevelUpDialogQuery::onExposure(QueryPtr topQuery)
{
	if(answer)
	{
		owner->popIfTop(*this);
		return;
	}

	if(prompted)
		return;

	for(auto color : players)
	{
		if(owner->topQuery(color).get() != this)
			continue;

		if(!gh->uiReadyForDialogs.contains(color))
			continue;

		refreshChoicesBeforePrompt();
		prompted = true;
		hlu.queryID = queryID;
		gh->sendAndApply(hlu);
		break;
	}
}

void CHeroLevelUpDialogQuery::notifyObjectAboutRemoval(const CGObjectInstance * visitedObject, const CGHeroInstance * visitingHero) const
{
	visitedObject->heroLevelUpDone(*gh, visitingHero);
}


void newHorizonsLevelUp::prepareChoices(CGameHandler & handler, const CGHeroInstance * hero, HeroLevelUp & hlu)
{
	hlu.skills = handler.randomizer->rollSecondarySkills(hero);
	if(!newHorizonsHeroes::usesPerkRules(hero->getPerkState().rules))
		return;

	const auto & perkState = hero->getPerkState();
	std::erase_if(hlu.skills, [hero, &perkState](SecondarySkill skill)
	{
		const auto * definition = LIBRARY->skillh->getById(skill);
		return !definition || !perkState.canAdvanceSkillNormally(definition->getJsonKey(),
			hero->getSecSkillLevel(skill));
	});
	const size_t maxSkillChoices = static_cast<size_t>(perkState.rules["maxSkillChoices"].Integer());
	if(hlu.skills.size() > maxSkillChoices)
		hlu.skills.resize(maxSkillChoices);
	hlu.perkOfferSeed = static_cast<uint32_t>(handler.randomizer->getDefault().nextInt());
	hlu.perks = perkState.prepareOffer([hero](const std::string & skillId)
	{
		return hero->getPerkSkillRank(skillId);
	}, hlu.perkOfferSeed, hero->getPerkOfferWeightContext());
}
