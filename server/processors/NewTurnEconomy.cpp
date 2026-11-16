/* Part of VCMI engine; GNU General Public License v2.0 or later. */
#include "StdInc.h"
#include "NewTurnProcessor.h"
#include "../CGameHandler.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/StartInfo.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/MiscObjects.h"
#include "../../lib/mapObjects/NewHorizonsEstates.h"
#include "../../lib/mapObjects/NewHorizonsEstatesPolicy.h"
#include "../../lib/entities/ResourceTypeHandler.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/callback/IGameRandomizer.h"
#include <vstd/RNG.h>

ResourceSet NewTurnProcessor::generatePlayerIncome(PlayerColor playerID, bool newWeek,
	std::map<ObjectInstanceID, std::vector<GameResID>> & mysticPondResults)
{
	const auto & playerSettings = gameHandler->gameInfo().getPlayerSettings(playerID);
	const PlayerState & state = gameHandler->gameState().players.at(playerID);
	ResourceSet income;
	const bool usesNewHorizonsEconomy = newHorizonsMagic::rulesActive(gameHandler->gameState().getMagicRules());

	for (const auto & town : state.getTowns())
	{
		if (newWeek && town->hasBuilt(BuildingSubID::TREASURY))
		{
			// Legacy Treasury interest is intentionally unchanged. New Horizons
			// applies its cap per physical Treasury, using the balance at week start.
			const int treasuryIncome = usesNewHorizonsEconomy
				? newHorizonsEconomy::treasuryWeeklyIncome(state.resources[EGameResID::GOLD])
				: state.resources[EGameResID::GOLD] / 10;
			income[EGameResID::GOLD] += treasuryIncome;
		}

		// Give resources if there's a Mystic Pond. Legacy worlds keep the
		// original one-resource, 1..4 output and town reveal properties. New
		// Horizons emits two one-unit picks into the authoritative weekly income
		// pack; duplicate picks aggregate naturally in ResourceSet.
		if (newWeek && town->hasBuilt(BuildingSubID::MYSTIC_POND))
		{
			static constexpr std::array rareResources = {
				GameResID::MERCURY,
				GameResID::SULFUR,
				GameResID::CRYSTAL,
				GameResID::GEMS
			};

			if (usesNewHorizonsEconomy)
			{
				auto & result = mysticPondResults[town->id];
				result.clear();
				result.reserve(newHorizonsEconomy::MYSTIC_POND_WEEKLY_RESOURCE_COUNT);
				for (int pick = 0; pick < newHorizonsEconomy::MYSTIC_POND_WEEKLY_RESOURCE_COUNT; ++pick)
				{
					const auto resID = *RandomGeneratorUtil::nextItem(rareResources, gameHandler->getRandomGenerator());
					result.push_back(resID);
					income[resID] += 1;
				}
			}
			else
			{
				auto resID = *RandomGeneratorUtil::nextItem(rareResources, gameHandler->getRandomGenerator());
				int resVal = gameHandler->getRandomGenerator().nextInt(1, 4);
				income[resID] += resVal;

				gameHandler->setObjPropertyValue(town->id, ObjProperty::BONUS_VALUE_FIRST, resID);
				gameHandler->setObjPropertyValue(town->id, ObjProperty::BONUS_VALUE_SECOND, resVal);
			}
		}
	}

	for(auto & k : LIBRARY->resourceTypeHandler->getAllObjects())
	{
		income += state.valOfBonuses(BonusType::RESOURCES_CONSTANT_BOOST, BonusSubtypeID(k));
		income += state.valOfBonuses(BonusType::RESOURCES_TOWN_MULTIPLYING_BOOST, BonusSubtypeID(k)) * state.getTowns().size();
	}

	if(newWeek) //weekly crystal generation if 1 or more crystal dragons in any hero army or town garrison
	{
		bool hasCrystalGenCreature = false;
		for (const auto & hero : state.getHeroes())
			for(const auto & stack : hero->stacks)
				if(stack.second->hasBonusOfType(BonusType::SPECIAL_CRYSTAL_GENERATION))
					hasCrystalGenCreature = true;

		for(const auto & town : state.getTowns())
			for(const auto & stack : town->stacks)
				if(stack.second->hasBonusOfType(BonusType::SPECIAL_CRYSTAL_GENERATION))
					hasCrystalGenCreature = true;

		if(hasCrystalGenCreature)
			income[EGameResID::CRYSTAL] += 3;
	}

	if(newWeek)
		income += newHorizonsEstates::weeklyAlchemistIncome(state);
	TResources incomeHandicapped = income;
	incomeHandicapped.applyHandicap(playerSettings->handicap.percentIncome);

	for(const auto * object : state.getOwnedObjects())
        incomeHandicapped += object->asOwnable()->dailyIncome();

	if (!state.isHuman())
	{
		// Initialize bonuses for different resources
		int difficultyIndex = gameHandler->gameState().getStartInfo()->difficulty;
		const std::string & difficultyName = GameConstants::DIFFICULTY_NAMES[difficultyIndex];
		const JsonNode & weeklyBonusesConfig = gameHandler->gameState().getSettings().getValue(EGameSettings::RESOURCES_WEEKLY_BONUSES_AI);
		const JsonNode & difficultyConfig = weeklyBonusesConfig[difficultyName];

		// Distribute weekly bonuses over 7 days, depending on the current day of the week
		auto calendar = gameHandler->gameState().getCalendar();
		for (GameResID i : LIBRARY->resourceTypeHandler->getAllObjects())
		{
			const std::string & name = i.toResource()->getJsonKey();
			int64_t weeklyBonus = difficultyConfig[name].Integer();
			int64_t dayOfWeek = calendar.getDayOfWeek();
			int64_t dailyIncome = incomeHandicapped[i];
			int64_t amountTillToday = dailyIncome * weeklyBonus * (dayOfWeek-1) / calendar.getDaysInWeek() / 100;
			int64_t amountAfterToday = dailyIncome * weeklyBonus * dayOfWeek / calendar.getDaysInWeek() / 100;
			int64_t dailyBonusToday = amountAfterToday - amountTillToday;
			int64_t totalIncomeToday = std::min(GameConstants::PLAYER_RESOURCES_CAP, incomeHandicapped[i] + dailyBonusToday);
			incomeHandicapped[i] = totalIncomeToday;
		}
	}

	return incomeHandicapped;
}


void NewTurnProcessor::grantWeeklyMineDefenders(int32_t week)
{
	std::map<PlayerColor, std::pair<int32_t, int32_t>> amounts;
	for(const auto & [player, state] : gameHandler->gameState().players)
		amounts[player] = {newHorizonsEstates::mercenaryWeeklyCount(state), newHorizonsEstates::companyDefenseLevel(state)};
	for(const auto * mine : gameHandler->gameState().getMap().getObjects<CGMine>())
	{
		if(!mine->getOwner().isValidPlayer() || !amounts.contains(mine->getOwner()))
			continue;
		const auto [legacy, defensiveLevel] = amounts.at(mine->getOwner());
		mine->grantWeeklyMercenaries(*gameHandler, week, legacy);
		if(defensiveLevel > 0)
			mine->grantWeeklyCompanyDefenders(*gameHandler, week, defensiveLevel);
	}
}
