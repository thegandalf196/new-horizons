/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "TownEstateUI.h"
#include "GUIClasses.h"
#include "InfoWindows.h"
#include "../CPlayerInterface.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../gui/WindowHandler.h"
#include "../render/Colors.h"
#include "../widgets/Buttons.h"
#include "../widgets/Images.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/EstatesStewardship.h"
#include "../../lib/mapObjects/NewHorizonsEstatesPolicy.h"
#include "../../lib/texts/MetaString.h"

namespace
{
std::vector<const CGHeroInstance *> eligibleStewards()
{
	std::vector<const CGHeroInstance *> result;
	for(const auto * hero : GAME->interface()->cb->getHeroesInfo())
		if(newHorizonsEstates::hasRevisedPerk(hero, "new-horizons:estates.stewardship"))
			result.push_back(hero);
	return result;
}

std::string assignedTownName(const CGHeroInstance & hero)
{
	const auto * player = hero.cb->getPlayerState(hero.getOwner());
	if(!player)
		return {};
	for(const auto & [townID, heroID] : player->estates.stewards)
	{
		if(heroID != hero.id)
			continue;
		const auto * town = hero.cb->getTown(townID);
		if(town && newHorizonsEstates::townSteward(*town) == &hero)
			return GAME->translator().translate(town->getNameTextID());
	}
	return {};
}

void showStewards(ObjectInstanceID townID)
{
	const auto callback = GAME->interface()->cb;
	const auto * town = callback->getTown(townID);
	if(!town || town->getOwner() != GAME->interface()->playerID || !GAME->interface()->makingTurn)
		return;
	std::vector<ObjectInstanceID> candidates;
	std::vector<std::string> labels;
	if(newHorizonsEstates::townSteward(*town))
	{
		candidates.push_back(ObjectInstanceID::NONE);
		labels.push_back("Dismiss the current steward");
	}
	for(const auto * hero : eligibleStewards())
	{
		candidates.push_back(hero->id);
		auto name = GAME->translator().translate(hero->getNameTextID());
		const auto assigned = assignedTownName(*hero);
		if(!assigned.empty())
			name += " — " + assigned;
		labels.push_back(std::move(name));
	}
	if(candidates.empty())
		return;
	auto select = [callback, townID, candidates](int index) {
		if(index >= 0 && static_cast<size_t>(index) < candidates.size())
			callback->assignTownSteward(townID, candidates[index]);
	};
	// The native list confirms once and closes before sending the request.
	// Income and assignment readbacks change only with the accepted state pack.
	auto icon = std::make_shared<CAnimImage>(AnimationPath::builtin("NH_perk_steward"), 0);
	auto window = std::make_shared<CObjectListWindow>(labels, icon, "Town Steward",
		"Adds 1,000 Gold daily.", std::move(select));
	window->onPopup = [candidates](int index) {
		if(index < 0 || static_cast<size_t>(index) >= candidates.size())
			return;
		const auto * hero = candidates[index] == ObjectInstanceID::NONE ? nullptr
			: GAME->interface()->cb->getHero(candidates[index]);
		const auto help = hero ? GAME->translator().translate(hero->getNameTextID())
			+ " administers one owned town from any location. Appointing this hero replaces their previous assignment. "
			  "Movement, exploration and combat remain available. The town earns 1,000 additional Gold at the next daily income."
			: "Dismiss this town's steward. Future daily income no longer receives the Stewardship bonus.";
		CRClickPopup::createAndPush(help);
	};
	ENGINE->windows().pushWindow(window);
}
}

namespace townEstateUI
{
std::shared_ptr<CButton> createStewardButton(const CGTownInstance & town)
{
	if(town.getOwner() != GAME->interface()->playerID
		|| (eligibleStewards().empty() && !newHorizonsEstates::townSteward(town)))
		return {};
	auto button = std::make_shared<CButton>(Point(660, 1), AnimationPath::builtin("settingsWindow/button80"),
		CButton::tooltip("Stewardship", "Appoint, replace or dismiss a town steward. A steward adds 1,000 Gold daily and may work remotely."),
		[townID = town.id] { showStewards(townID); });
	button->setTextOverlay("Steward", FONT_SMALL, Colors::YELLOW);
	button->block(!GAME->interface()->makingTurn);
	return button;
}

std::string incomeHelp(const CGTownInstance & town)
{
	MetaString total = MetaString::createFromTextID("new-horizons.economy.stewardship.total");
	total.replaceNumber(town.dailyIncome()[EGameResID::GOLD]);
	MetaString contribution = MetaString::createFromTextID("new-horizons.economy.stewardship.contribution");
	contribution.replaceNumber(town.getStewardGoldBeforeHandicap());
	const auto * player = town.cb->getPlayerState(town.getOwner());
	const auto heroes = player ? player->getHeroes() : std::vector<const CGHeroInstance *>{};
	const bool revised = std::any_of(heroes.begin(), heroes.end(), [](const auto * hero) {
		return newHorizonsEstates::usesRevisedPolicy(hero->getPerkState().rules);
	});
	std::string help;
	if(!revised)
		help = GAME->translator().translate("new-horizons.economy.stewardship.help");
	else if(const auto * hero = newHorizonsEstates::townSteward(town))
		help = "Steward: " + GAME->translator().translate(hero->getNameTextID())
			+ ". This hero adds 1,000 Gold daily from any location. Manage the appointment in the Town Hall.";
	else
		help = "Appoint a hero with Stewardship in the Town Hall to add 1,000 Gold daily. The hero need not remain in town.";
	return CInfoWindow::genText(total.toString(&GAME->translator()),
		contribution.toString(&GAME->translator()) + "\n\n" + help);
}
std::string capitolHelp(const CGTownInstance & town, bool blocked)
{
	const auto * player = town.cb->getPlayerState(town.getOwner());
	if(!player)
		return {};
	const auto towns = player->getTowns();
	const auto count = std::count_if(towns.begin(), towns.end(), [](const auto * ownedTown) {
		return ownedTown->hasBuilt(BuildingID::CAPITOL);
	});
	return std::string(blocked ? "The kingdom's Capitol construction limit has been reached.\n" : "")
		+ "Capitols: " + std::to_string(count) + "/" + std::to_string(newHorizonsEstates::capitolLimit(*player)) + "."
		+ (newHorizonsEstates::capitolLimit(*player) == 2 ? " Decentralization allows two Capitols." : "");
}

std::string constructionHelp(const CGTownInstance & town)
{
	for(const auto * hero : {town.getVisitingHero(), town.getGarrisonHero()})
		if(hero && hero->getOwner() == town.getOwner()
			&& newHorizonsEstates::hasRevisedPerk(hero, "new-horizons:estates.architect"))
			return "\n\nArchitect: the shown Gold, Wood and Ore costs include a 20% discount, rounded up. Other resources are unchanged.";
	return {};
}

}
