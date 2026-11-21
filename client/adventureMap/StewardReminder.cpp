/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "StewardReminder.h"
#include "../CPlayerInterface.h"
#include "../GameInstance.h"
#include "../../lib/CConfigHandler.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/EstatesStewardship.h"
#include "../../lib/texts/MetaString.h"

#include <algorithm>

namespace stewardReminder
{
std::optional<std::string> pendingReminder(const PlayerState & player)
{
	auto heroes = player.getHeroes();
	auto towns = player.getTowns();
	std::ranges::sort(heroes, {}, &CGHeroInstance::id);
	std::ranges::sort(towns, {}, &CGTownInstance::id);
	const auto freeHero = std::ranges::find_if(heroes, [](const auto * hero)
	{
		return newHorizonsEstates::eligibleSteward(hero) && !newHorizonsEstates::stewardedTown(*hero);
	});
	if(freeHero == heroes.end())
		return std::nullopt;
	const auto unattended = std::ranges::find_if(towns, [](const auto * town)
	{
		return !newHorizonsEstates::townSteward(*town);
	});
	if(unattended == towns.end())
		return std::nullopt;
	MetaString text = MetaString::createFromTextID("new-horizons.economy.stewardship.endTurnReminder");
	text.replaceRawString(GAME->translator().translate((*unattended)->getNameTextID()));
	text.replaceRawString(GAME->translator().translate((*freeHero)->getNameTextID()));
	return text.toString(&GAME->translator());
}

void confirmEndTurn(const std::function<void()> & endTurn)
{
	const auto * player = GAME->interface()->cb->getPlayerState(GAME->interface()->playerID);
	const auto reminder = player && settings["adventure"]["stewardReminder"].Bool()
		? pendingReminder(*player) : std::nullopt;
	if(reminder)
		GAME->interface()->showYesNoDialog(*reminder, endTurn, nullptr);
	else
		endTurn();
}
}
