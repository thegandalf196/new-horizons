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
#include "../../lib/constants/EntityIdentifiers.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/CCreatureHandler.h"

#include <algorithm>
#include <string_view>

namespace newHorizonsMusterUI
{
namespace
{
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

bool isExternalRecruiterDwelling(const CGDwelling * dwelling)
{
	return dwelling && (dwelling->ID == Obj::CREATURE_GENERATOR1 || dwelling->ID == Obj::CREATURE_GENERATOR4);
}

int currentWeek()
{
	if(!GAME || !GAME->interface() || !GAME->interface()->cb)
		return 0;

	const auto calendar = GAME->interface()->cb->getCalendar();
	// The serialized marker is an absolute week, unlike Calendar::getWeek(),
	// which is the week within the current month.
	return ::newHorizonsMuster::absoluteWeek(calendar.getCurrentDay(), calendar.getDaysInWeek());
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

std::string rankSummary(const int rank, const ::newHorizonsMuster::PerkModifiers & modifiers)
{
	const auto amount = [rank, &modifiers](const newHorizonsCreatures::CreatureCategory category)
	{
		const auto value = ::newHorizonsMuster::amountForCategory(rank, category, modifiers);
		return value ? std::to_string(*value) : std::string("-");
	};

	switch(rank)
	{
	case 1:
		return "+" + amount(newHorizonsCreatures::CreatureCategory::CORE) + " Core";
	case 2:
		return "+" + amount(newHorizonsCreatures::CreatureCategory::CORE) + " Core / +"
			+ amount(newHorizonsCreatures::CreatureCategory::ELITE) + " Elite";
	case 3:
		return "+" + amount(newHorizonsCreatures::CreatureCategory::CORE) + " Core / +"
			+ amount(newHorizonsCreatures::CreatureCategory::ELITE) + " Elite / +"
			+ amount(newHorizonsCreatures::CreatureCategory::CHAMPION) + " Champion";
	default:
		return {};
	}
}

} // namespace

std::optional<Offer> offerFor(const CGDwelling * dwelling, const CGHeroInstance * destinationHero)
{
	if(!dwelling || !GAME || !GAME->interface() || !GAME->interface()->cb)
		return std::nullopt;

	const auto * town = dynamic_cast<const CGTownInstance *>(dwelling);
	const auto * hero = town ? townHero(town) : destinationHero;
	if(!hero || !newHorizonsHeroes::usesPerkRules(hero->getPerkState().rules))
		return std::nullopt;

	const int rank = hero->getPerkSkillRank(std::string(::newHorizonsMuster::RECRUITMENT_SKILL));
	if(rank < 1 || rank > 3)
		return std::nullopt;

	if(dwelling->tempOwner != hero->tempOwner)
		return std::nullopt;

	const bool externalDwelling = isExternalRecruiterDwelling(dwelling);
	if(!town && (!externalDwelling || !hero->hasActivePerk(std::string(::newHorizonsMuster::RECRUITMENT_SKILL),
		std::string(::newHorizonsMuster::EXTERNAL_RECRUITER_PERK))))
		return std::nullopt;

	const int week = currentWeek();
	::newHorizonsMuster::PerkModifiers modifiers;
	modifiers.volunteerNetwork = hero->hasActivePerk(std::string(::newHorizonsMuster::RECRUITMENT_SKILL),
		std::string(::newHorizonsMuster::VOLUNTEER_NETWORK_PERK));
	modifiers.eliteDraft = hero->hasActivePerk(std::string(::newHorizonsMuster::RECRUITMENT_SKILL),
		std::string(::newHorizonsMuster::ELITE_DRAFT_PERK));
	modifiers.championsCall = hero->hasActivePerk(std::string(::newHorizonsMuster::RECRUITMENT_SKILL),
		std::string(::newHorizonsMuster::CHAMPIONS_CALL_PERK));
	modifiers.masterRecruiter = hero->hasActivePerk(std::string(::newHorizonsMuster::RECRUITMENT_SKILL),
		std::string(::newHorizonsMuster::MASTER_RECRUITER_PERK));
	const int usesThisWeek = hero->getNewHorizonsMusterUsesThisWeek(week);
	const int maximumUses = ::newHorizonsMuster::maximumUsesPerWeek(modifiers);
	const bool targetUsedThisWeek = dwelling->getNewHorizonsMusterLastWeek() == week;
	return Offer{dwelling, hero, rank, week,
		usesThisWeek >= maximumUses || targetUsedThisWeek, targetUsedThisWeek, usesThisWeek, maximumUses,
		externalDwelling, modifiers};
}

std::vector<Target> targetsFor(const Offer & offer)
{
	std::vector<Target> result;
	if(!offer.dwelling || !GAME || !GAME->interface() || !GAME->interface()->cb || offer.usedThisWeek)
		return result;

	for(size_t row = 0; row < offer.dwelling->creatures.size(); ++row)
	{
		const auto & choices = offer.dwelling->creatures[row].second;
		if(choices.empty())
			continue;

		// A town row can contain base and upgraded alternatives.  Match the
		// normal recruitment presentation by targeting the final available form,
		// while retaining the row index as the destination identity.
		const CreatureID creature = choices.back();
		const auto category = GAME->interface()->cb->getCreatureCategory(creature);
		if(!category)
			continue;

		if(offer.externalDwelling && category->category != newHorizonsCreatures::CreatureCategory::CORE)
			continue;

		// External Recruiter contributes its fixed amount regardless of rank;
		// it is separate from the town Core row and Volunteer Network modifiers.
		const auto amount = offer.externalDwelling
			? ::newHorizonsMuster::amountForExternalCategory(offer.recruitmentRank, category->category, true)
			: ::newHorizonsMuster::amountForCategory(offer.recruitmentRank, category->category, offer.modifiers);
		if(!amount)
			continue;

		const auto * creatureType = creature.toCreature();
		if(!creatureType)
			continue;

		result.push_back(Target{static_cast<int>(row), creature, category->category, *amount, creatureType});
	}

	return result;
}

bool isEligible(const CGDwelling * dwelling, const CGHeroInstance * destinationHero)
{
	return offerFor(dwelling, destinationHero).has_value();
}

std::string status(const Offer & offer)
{
	if(offer.usesThisWeek >= offer.maximumUses)
		return translate("new-horizons.muster.used", "Muster used this week");
	if(offer.targetUsedThisWeek)
		return translate(offer.externalDwelling ? "new-horizons.muster.externalTargetUsed" : "new-horizons.muster.targetUsed",
			offer.externalDwelling ? "Muster already used at this dwelling" : "Muster already used in this town");
	if(offer.maximumUses > 1)
		return translate("new-horizons.muster.masterAvailable", "Muster available this week")
			+ " (" + std::to_string(offer.usesThisWeek) + "/" + std::to_string(offer.maximumUses) + ")";
	return translate("new-horizons.muster.available", "Muster available this week");
}

void open(const CGDwelling * dwelling, const CGHeroInstance * destinationHero)
{
	const auto offer = offerFor(dwelling, destinationHero);
	if(!offer)
		return;

	const auto targets = targetsFor(*offer);
	const std::string unavailableNote = offer->externalDwelling
		? translate("new-horizons.muster.externalRecruiterNote", "Choose one owned external Core dwelling to reinforce.")
		: translate("new-horizons.muster.rankOnlyNote", "Choose one town dwelling to reinforce.");
	const std::string heading = translate("new-horizons.muster.title", "Muster");

	if(offer->usedThisWeek)
	{
		GAME->interface()->showInfoDialog(heading + "\n\n" + status(*offer) + ".\n" + unavailableNote);
		return;
	}

	if(targets.empty())
	{
		GAME->interface()->showInfoDialog(heading + "\n\n"
			+ (offer->externalDwelling
				? translate("new-horizons.muster.noExternalTargets", "No legal Core recruit is available in this external dwelling.")
				: translate("new-horizons.muster.noTargets", "No legal Core, Elite, or Champion dwelling row is available in this town."))
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

	const std::string amountSummary = offer->externalDwelling
		? "+" + std::to_string(*::newHorizonsMuster::amountForExternalCategory(offer->recruitmentRank,
			newHorizonsCreatures::CreatureCategory::CORE, true)) + " Core"
		: rankSummary(offer->recruitmentRank, offer->modifiers);
	const std::string description = status(*offer) + ": " + amountSummary
		+ ". " + translate("new-horizons.muster.chooseRow", "Choose a dwelling row.")
		+ "\n" + unavailableNote;
	ENGINE->windows().pushWindow(std::make_shared<CObjectListWindow>(entries, nullptr, heading, description,
		[hero = offer->hero, targetDwelling = offer->dwelling, targets](const int index)
		{
			if(index < 0 || static_cast<size_t>(index) >= targets.size() || !GAME || !GAME->interface()
				|| !GAME->interface()->cb)
				return;

			GAME->interface()->cb->musterCreatures(hero, targetDwelling, targets[index].creature);
		}));
}

} // namespace newHorizonsMusterUI
