/*
 * NewTurnProcessor.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../lib/constants/EntityIdentifiers.h"
#include "../../lib/constants/Enumerations.h"
#include "../../lib/gameState/RumorState.h"

class CGTownInstance;
class ResourceSet;
struct SetAvailableCreatures;
struct SetMovePoints;
struct SetMana;
struct InfoWindow;
struct NewTurn;

class CGameHandler;

namespace newHorizonsEconomy
{
/// New Horizons Treasury interest is calculated from the gold balance at the
/// beginning of the week, independently for every Treasury building.
inline constexpr int TREASURY_INTEREST_PERCENT = 10;
inline constexpr int TREASURY_INTEREST_CAP = 2000;
inline constexpr int MYSTIC_POND_WEEKLY_RESOURCE_COUNT = 2;

constexpr int treasuryWeeklyIncome(int currentGold)
{
	const auto interest = static_cast<long long>(currentGold) * TREASURY_INTEREST_PERCENT / 100;
	return interest < TREASURY_INTEREST_CAP ? static_cast<int>(interest) : TREASURY_INTEREST_CAP;
}
}

class NewTurnProcessor : boost::noncopyable
{
	CGameHandler * gameHandler;

	std::vector<SetMana> updateHeroesManaPoints();
	std::vector<SetMovePoints> updateHeroesMovementPoints();

	ResourceSet generatePlayerIncome(PlayerColor playerID, bool newWeek,
		std::map<ObjectInstanceID, std::vector<GameResID>> & mysticPondResults);
	SetAvailableCreatures generateTownGrowth(const CGTownInstance * town, EWeekType weekType, CreatureID creatureWeek, bool firstDay, int additionalGrowth);
	RumorState pickNewRumor();
	InfoWindow createInfoWindow(EWeekType weekType, CreatureID creatureWeek, bool newMonth, int additionalGrowth);
	std::tuple<EWeekType, CreatureID, int> pickWeekType(bool newMonth);

	NewTurn generateNewTurnPack();
	void handleTimeEvents(PlayerColor player);
	void handleTownEvents(const CGTownInstance *town);

	void updateNeutralTownGarrison(const CGTownInstance * t, int currentWeek) const;

public:
	NewTurnProcessor(CGameHandler * gameHandler);

	void onNewTurn();
	void onPlayerTurnStarted(PlayerColor color);
	void onPlayerTurnEnded(PlayerColor color);
};
