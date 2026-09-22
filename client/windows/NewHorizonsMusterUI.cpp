/*
 * NewHorizonsMusterUI.cpp, part of VCMI / New Horizons
 *
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NewHorizonsMusterUI.h"

#include "GUIClasses.h"
#include "NewHorizonsCreatureCategoryUI.h"

#include "../CPlayerInterface.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../gui/WindowHandler.h"
#include "../widgets/Buttons.h"

#include "../../lib/callback/CCallback.h"
#include "../../lib/entities/hero/NewHorizonsPerkRules.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/CCreatureHandler.h"

#include <algorithm>
#include <string_view>

namespace newHorizonsMusterUI
{
namespace
{
constexpr std::string_view RECRUITMENT_SKILL = "new-horizons:recruitment";

std::string translate(std::string_view key, std::string fallback)
{
	if(!GAME)
		return fallback;

	const auto result = GAME->translator().translate(std::string(key));
	return result == key ? std::move(fallback) : result;
}

const CGHeroInstance * townHero(const CGTownInstance * town)
{
	if(!town)
		return nullptr;
	return town->getVisitingHero() ? town->getVisitingHero() : town->getGarrisonHero();
}

int currentWeek()
{
	if(!GAME || !GAME->interface() || !GAME->interface()->cb)
		return 0;

	const auto calendar = GAME->interface()->cb->getCalendar();
	// The serialized marker is an absolute week, unlike Calendar::getWeek(),
	// which is the week within the current month.
	return std::max(0, (calendar.getCurrentDay() - 1) / calendar.getDaysInWeek());
}

std::optional<int> amountForCategory(const int rank, const newHorizonsCreatures::CreatureCategory category)
{
	if(rank < 1 || rank > 3)
		return std::nullopt;

	switch(rank)
	{
	case 1:
		return category == newHorizonsCreatures::CreatureCategory::CORE ? std::optional<int>(2) : std::nullopt;
	case 2:
		if(category == newHorizonsCreatures::CreatureCategory::CORE)
			return 4;
		if(category == newHorizonsCreatures::CreatureCategory::ELITE)
			return 1;
		return std::nullopt;
	case 3:
		if(category == newHorizonsCreatures::CreatureCategory::CORE)
			return 6;
		if(category == newHorizonsCreatures::CreatureCategory::ELITE)
			return 2;
		if(category == newHorizonsCreatures::CreatureCategory::CHAMPION)
			return 1;
		return std::nullopt;
	default:
		return std::nullopt;
	}
}

std::string categoryName(const std::optional<newHorizonsCreatures::CreatureCategoryView> & category,
	const newHorizonsCreatures::CreatureCategory fallback)
{
	if(const auto translated = newHorizonsCreatureCategoryUI::name(category, GAME ? &GAME->translator() : nullptr); !translated.empty())
		return translated;

	switch(fallback)
	{
	case newHorizonsCreatures::CreatureCategory::CORE:
		return "Core";
	case newHorizonsCreatures::CreatureCategory::ELITE:
		return "Elite";
	case newHorizonsCreatures::CreatureCategory::CHAMPION:
		return "Champion";
	}
	return {};
}

std::string rankSummary(const int rank)
{
	switch(rank)
	{
	case 1:
		return "+2 Core";
	case 2:
		return "+4 Core / +1 Elite";
	case 3:
		return "+6 Core / +2 Elite / +1 Champion";
	default:
		return {};
	}
}

} // namespace

std::optional<Offer> offerFor(const CGTownInstance * town)
{
	if(!town || !GAME || !GAME->interface() || !GAME->interface()->cb)
		return std::nullopt;

	const auto * hero = townHero(town);
	if(!hero || !newHorizonsHeroes::usesPerkRules(hero->getPerkState().rules))
		return std::nullopt;

	const int rank = hero->getPerkSkillRank(std::string(RECRUITMENT_SKILL));
	if(rank < 1 || rank > 3)
		return std::nullopt;

	if(town->tempOwner != hero->tempOwner)
		return std::nullopt;

	const int week = currentWeek();
	return Offer{town, hero, rank, week,
		hero->hasUsedNewHorizonsMuster(week) || town->getNewHorizonsMusterLastWeek() == week};
}

std::vector<Target> targetsFor(const Offer & offer)
{
	std::vector<Target> result;
	if(!offer.town || !GAME || !GAME->interface() || !GAME->interface()->cb || offer.usedThisWeek)
		return result;

	for(size_t row = 0; row < offer.town->creatures.size(); ++row)
	{
		const auto & choices = offer.town->creatures[row].second;
		if(choices.empty())
			continue;

		// A town row can contain base and upgraded alternatives.  Match the
		// normal recruitment presentation by targeting the final available form,
		// while retaining the row index as the destination identity.
		const CreatureID creature = choices.back();
		const auto category = GAME->interface()->cb->getCreatureCategory(creature);
		if(!category)
			continue;

		const auto amount = amountForCategory(offer.recruitmentRank, category->category);
		if(!amount)
			continue;

		const auto * creatureType = creature.toCreature();
		if(!creatureType)
			continue;

		result.push_back(Target{static_cast<int>(row), creature, category->category, *amount, creatureType});
	}

	return result;
}

bool isEligible(const CGTownInstance * town)
{
	return offerFor(town).has_value();
}

std::string status(const Offer & offer)
{
	if(offer.usedThisWeek)
		return translate("new-horizons.muster.used", "Muster used this week");
	return translate("new-horizons.muster.available", "Muster available this week");
}

void open(const CGTownInstance * town)
{
	const auto offer = offerFor(town);
	if(!offer)
		return;

	const auto targets = targetsFor(*offer);
	const std::string unavailableNote = translate("new-horizons.muster.rankOnlyNote",
		"Choose one town dwelling to reinforce.");
	const std::string heading = translate("new-horizons.muster.title", "Muster");

	if(offer->usedThisWeek)
	{
		GAME->interface()->showInfoDialog(heading + "\n\n" + status(*offer) + ".\n" + unavailableNote);
		return;
	}

	if(targets.empty())
	{
		GAME->interface()->showInfoDialog(heading + "\n\n"
			+ translate("new-horizons.muster.noTargets", "No legal Core, Elite, or Champion dwelling row is available in this town.")
			+ "\n" + unavailableNote);
		return;
	}

	std::vector<std::string> entries;
	entries.reserve(targets.size());
	for(const auto & target : targets)
	{
		const auto category = categoryName(GAME->interface()->cb->getCreatureCategory(target.creature), target.category);
		entries.push_back("[" + category + "] " + target.creatureType->getNamePluralTranslated()
			+ "  +" + std::to_string(target.amount));
	}

	const std::string description = status(*offer) + ": " + rankSummary(offer->recruitmentRank)
		+ ". " + translate("new-horizons.muster.chooseRow", "Choose a dwelling row.")
		+ "\n" + unavailableNote;
	ENGINE->windows().pushWindow(std::make_shared<CObjectListWindow>(entries, nullptr, heading, description,
		[hero = offer->hero, targetTown = offer->town, targets](const int index)
		{
			if(index < 0 || static_cast<size_t>(index) >= targets.size() || !GAME || !GAME->interface()
				|| !GAME->interface()->cb)
				return;

			GAME->interface()->cb->musterCreatures(hero, targetTown, targets[index].creature);
		}));
}

} // namespace newHorizonsMusterUI
