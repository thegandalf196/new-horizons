/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "TownEstateUI.h"
#include "CCastleInterface.h"
#include "CMarketWindow.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../widgets/TextControls.h"
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
class TownIncomeMenu final : public CInfoWindow
{
public:
	TownIncomeMenu(PlayerColor player, std::function<void()> market, std::function<void()> administration)
		: CInfoWindow("{Town Economy}\n\nChoose a destination.", player, TCompsInfo{},
			TButtonsInfo{{AnimationPath::builtin("settingsWindow/button190"), market},
				{AnimationPath::builtin("settingsWindow/button190"), administration}})
	{
		buttons[0]->setTextOverlay("Marketplace", FONT_SMALL, Colors::YELLOW);
		buttons[1]->setTextOverlay("Town Administration", FONT_SMALL, Colors::YELLOW);
		// Escape dismisses this local menu; it must not choose Administration.
		buttons[1]->assignedKey = EShortcut::TOWN_OPEN_ADMINISTRATION;
		addUsedEvents(KEYBOARD);
	}
	void keyPressed(EShortcut key) override
	{
		if(key == EShortcut::GLOBAL_CANCEL)
			close();
	}
	void close() override
	{
		WindowBase::close();
	}
};

std::vector<const CGHeroInstance *> eligibleStewards()
{
	std::vector<const CGHeroInstance *> result;
	for(const auto * hero : GAME->interface()->cb->getHeroesInfo())
		if(newHorizonsEstates::eligibleSteward(hero))
			result.push_back(hero);
	return result;
}

std::string assignedTownName(const CGHeroInstance & hero)
{
	const auto * town = newHorizonsEstates::stewardedTown(hero);
	return town ? GAME->translator().translate(town->getNameTextID()) : std::string();
}

std::string goldText(int32_t gold)
{
	return gold >= 1000 ? std::to_string(gold / 1000) + "," + std::to_string(1000 + gold % 1000).substr(1)
		: std::to_string(gold);
}

bool coreStewardship()
{
	const auto heroes = GAME->interface()->cb->getHeroesInfo();
	return std::any_of(heroes.begin(), heroes.end(), [](const auto * hero)
	{
		return newHorizonsEstates::usesCoreStewardship(hero->getPerkState().getRules());
	});
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
		auto name = GAME->translator().translate(hero->getNameTextID())
			+ " (+" + goldText(newHorizonsEstates::stewardGold(*hero)) + ")";
		const auto assigned = assignedTownName(*hero);
		if(!assigned.empty())
			name += " — " + assigned;
		labels.push_back(std::move(name));
	}
	if(candidates.empty())
	{
		GAME->interface()->showInfoDialog(coreStewardship() ? "You have no hero to appoint as steward."
			: "No active hero has Stewardship. Learn Stewardship to appoint a town steward.");
		return;
	}
	auto select = [callback, townID, candidates](int index) {
		if(index >= 0 && static_cast<size_t>(index) < candidates.size())
			callback->assignTownSteward(townID, candidates[index]);
	};
	// The native list confirms once and closes before sending the request.
	// Income and assignment readbacks change only with the accepted state pack.
	auto icon = std::make_shared<CAnimImage>(AnimationPath::builtin("NH_perk_steward"), 0);
	auto window = std::make_shared<CObjectListWindow>(labels, icon, "Town Steward",
		coreStewardship() ? "A steward adds 500 Gold daily, or 1,000 with Stewardship." : "Adds 1,000 Gold daily.",
		std::move(select));
	window->onPopup = [candidates](int index) {
		if(index < 0 || static_cast<size_t>(index) >= candidates.size())
			return;
		const auto * hero = candidates[index] == ObjectInstanceID::NONE ? nullptr
			: GAME->interface()->cb->getHero(candidates[index]);
		const auto help = hero ? GAME->translator().translate(hero->getNameTextID())
			+ " administers one owned town from any location. Appointing this hero replaces their previous assignment. "
			  "Movement, exploration and combat remain available. The town earns "
			+ goldText(newHorizonsEstates::stewardGold(*hero)) + " additional Gold at the next daily income."
			: "Dismiss this town's steward. Future daily income no longer receives the Stewardship bonus.";
		CRClickPopup::createAndPush(help);
	};
	ENGINE->windows().pushWindow(window);
}
}

namespace townEstateUI
{
void showAdministration(const CGTownInstance & town)
{
	showStewards(town.id);
}

void showIncomeMenu(const CGTownInstance & town, std::function<void()> marketplace)
{
	if(town.getOwner() != GAME->interface()->playerID || !GAME->interface()->makingTurn)
		return;
	auto administration = [townID = town.id] {
		if(const auto * currentTown = GAME->interface()->cb->getTown(townID))
			showAdministration(*currentTown);
	};
	ENGINE->windows().pushWindow(std::make_shared<TownIncomeMenu>(
		GAME->interface()->playerID, std::move(marketplace), std::move(administration)));
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
		return newHorizonsEstates::usesRevisedPolicy(hero->getPerkState().getRules());
	});
	std::string help;
	if(!revised)
		help = GAME->translator().translate("new-horizons.economy.stewardship.help");
	else if(const auto * hero = newHorizonsEstates::townSteward(town))
		help = "Steward: " + GAME->translator().translate(hero->getNameTextID())
			+ ". This hero adds " + goldText(newHorizonsEstates::stewardGold(*hero))
			+ " Gold daily from any location. Manage the appointment through the Gold display or press S.";
	else if(std::any_of(heroes.begin(), heroes.end(), [](const auto * hero) {
			return newHorizonsEstates::usesCoreStewardship(hero->getPerkState().getRules()); }))
		help = "Appoint any hero as steward through the Gold display or press S to add 500 Gold daily, or 1,000 with Stewardship. The hero need not remain in town.";
	else
		help = "Appoint a hero with Stewardship through the Gold display or press S to add 1,000 Gold daily. The hero need not remain in town.";
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

void CCastleBuildings::enterAnyMarket()
{
	if(town->hasBuilt(BuildingID::MARKETPLACE))
	{
		ENGINE->windows().createAndPushWindow<CMarketWindow>(town, nullptr, nullptr, EMarketMode::RESOURCE_RESOURCE);
		return;
	}

	std::vector<const CGTownInstance*> towns = GAME->interface()->cb->getTownsInfo(true);
	for(auto & town : towns)
	{
		if(town->hasBuilt(BuildingID::MARKETPLACE))
		{
			ENGINE->windows().createAndPushWindow<CMarketWindow>(town, nullptr, nullptr, EMarketMode::RESOURCE_RESOURCE);
			return;
		}
	}
	GAME->interface()->showInfoDialog(LIBRARY->generaltexth->translate("vcmi.adventureMap.noTownWithMarket"));
}

void CCastleInterface::refreshIncome()
{
	if(income)
		income->setText(std::to_string(town->dailyIncome()[EGameResID::GOLD]));
	redraw();
}
