/*
 * NewHorizonsPortalRecruitmentTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"

#include <array>

#include "AI/Nullkiller2/AIGateway.h"
#include "AI/Nullkiller2/Analyzers/ArmyManager.h"
#include "AI/Nullkiller2/Engine/Nullkiller.h"
#include "lib/CPlayerState.h"
#include "lib/GameConstants.h"
#include "lib/GameLibrary.h"
#include "lib/IGameSettings.h"
#include "lib/ResourceSet.h"
#include "lib/entities/creature/NewHorizonsMusterRules.h"
#include "lib/mapObjects/CGDwelling.h"
#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/mapObjects/CGTownInstance.h"
#include "lib/modding/CModHandler.h"
#include "lib/spells/NewHorizonsMagic.h"
#include "mock/TinyH3MBuilder.h"
#include "nullkiller2/NullkillerTest.h"

namespace
{
const PlayerColor PLAYER(0);
const PlayerColor OTHER_PLAYER(1);

CreatureID creature(const char * id)
{
	return CreatureID(CreatureID::decode(id));
}

constexpr std::array<GameResID, 7> ALL_RESOURCES = {
	GameResID(EGameResID::WOOD), GameResID(EGameResID::MERCURY), GameResID(EGameResID::ORE),
	GameResID(EGameResID::SULFUR), GameResID(EGameResID::CRYSTAL), GameResID(EGameResID::GEMS),
	GameResID(EGameResID::GOLD)
};

ResourceSet resourcesWithEachResource(int amount)
{
	ResourceSet result;
	for(const auto resource : ALL_RESOURCES)
		result[resource] = amount;
	return result;
}

ui64 armyCandidateValue(
	NK2AI::AIGateway & gateway,
	const CGHeroInstance * carrier,
	const CGDwelling * source)
{
	ui64 value = 0;
	const auto candidates = gateway.nullkiller->armyManager->getArmyAvailableToBuy(
		carrier, source, gateway.cc->getResourceAmount(), 0, carrier);
	for(const auto & candidate : candidates)
		if(candidate.count > 0 && candidate.creID.toCreature())
			value += static_cast<ui64>(candidate.creID.toCreature()->getAIValue()) * candidate.count;
	return value;
}

class NewHorizonsPortalRecruitmentTest : public NullkillerTest
{
protected:
	void SetUp() override
	{
		NullkillerTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			useNewHorizonsMagicRules ? JsonNode(JsonPath::builtin("config/newHorizonsMagic")) : JsonNode());
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
	}

	void startGame()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false)
			.playerActive(PLAYER)
			.town({10, 10, 0}, FactionID(FactionID::decode("core:dungeon")), PLAYER)
			.hero({6, 10, 0}, HeroTypeID(0), PLAYER)
			// Subtypes 56 and 58 are the core Pikeman and Swordsman dwellings.
			// Quantities are replaced below for explicit, deterministic AI cases.
			.dwelling({20, 20, 0}, MapObjectSubID(56), PLAYER)
			.dwelling({26, 26, 0}, MapObjectSubID(58), PLAYER);
		startWithMap(std::move(builder));
		pikeman = creature("core:pikeman");
		swordsman = creature("core:swordsman");

		town = findFirst<CGTownInstance>();
		hero = findHeroByOwner(PLAYER);
		pikeSource = dynamic_cast<CGDwelling *>(findObjectAt({20, 20, 0}));
		swordsmanSource = dynamic_cast<CGDwelling *>(findObjectAt({26, 26, 0}));
		ASSERT_NE(town, nullptr);
		ASSERT_NE(hero, nullptr);
		ASSERT_NE(pikeSource, nullptr);
		ASSERT_NE(swordsmanSource, nullptr);

		town->addBuilding(BuildingID::SPECIAL_3);
		town->setVisitingHero(hero);
		hero->clearSlots();
		pikeSource->creatures = {{24, {pikeman}}};
		swordsmanSource->creatures = {{10, {swordsman}}};
		gameState()->getPlayerState(PLAYER)->resources = resourcesWithEachResource(100'000);
		revealMap(PLAYER);
	}

	bool useNewHorizonsMagicRules = true;
	CreatureID pikeman = CreatureID::NONE;
	CreatureID swordsman = CreatureID::NONE;
	CGTownInstance * town = nullptr;
	CGHeroInstance * hero = nullptr;
	CGDwelling * pikeSource = nullptr;
	CGDwelling * swordsmanSource = nullptr;
};
}

TEST_F(NewHorizonsPortalRecruitmentTest, ChoosesTheMostUsefulOwnedPortalDwellingWithoutChangingStockOrChoice)
{
	startGame();
	ASSERT_TRUE(newHorizonsMagic::rulesActive(gameState()->getMagicRules()));
	ASSERT_EQ(town->getFactionID(), FactionID(FactionID::decode("core:dungeon")));
	ASSERT_TRUE(town->hasBuilt(BuildingSubID::PORTAL_OF_SUMMONING));
	ASSERT_EQ(town->getVisitingHero(), hero);

	const auto gateway = makeGateway(PLAYER);
	const ui64 pikeValue = armyCandidateValue(*gateway, hero, pikeSource);
	const ui64 swordsmanValue = armyCandidateValue(*gateway, hero, swordsmanSource);
	ASSERT_GT(pikeValue, 0u);
	ASSERT_GT(swordsmanValue, pikeValue);

	const auto pikeStockBefore = pikeSource->creatures;
	const auto swordsmanStockBefore = swordsmanSource->creatures;
	EXPECT_EQ(town->portalSourceDwellingId, ObjectInstanceID::NONE);
	EXPECT_EQ(town->portalLastSelectionWeek, -1);

	EXPECT_EQ(gateway->getBestPortalRecruitmentDwelling(town, hero), swordsmanSource);
	EXPECT_EQ(gateway->getBestPortalRecruitmentDwelling(town, hero), swordsmanSource);
	EXPECT_EQ(town->portalSourceDwellingId, ObjectInstanceID::NONE)
		<< "a read-only AI choice must not submit or persist the weekly selection";
	EXPECT_EQ(town->portalLastSelectionWeek, -1);
	EXPECT_EQ(pikeSource->creatures, pikeStockBefore);
	EXPECT_EQ(swordsmanSource->creatures, swordsmanStockBefore);
	EXPECT_EQ(hero->stacksCount(), 0u);
}

TEST_F(NewHorizonsPortalRecruitmentTest, WeeklySavedChoiceWinsOverABetterAlternativeAndInvalidSourcesAreRejected)
{
	startGame();
	const auto gateway = makeGateway(PLAYER);
	ASSERT_GT(armyCandidateValue(*gateway, hero, swordsmanSource), armyCandidateValue(*gateway, hero, pikeSource));

	const auto currentWeek = newHorizonsMuster::absoluteWeek(
		gameState()->getCalendar().getCurrentDay(), gameState()->getCalendar().getDaysInWeek());
	town->portalSourceDwellingId = pikeSource->id;
	town->portalLastSelectionWeek = currentWeek;
	const auto savedSource = town->portalSourceDwellingId;
	const int savedWeek = town->portalLastSelectionWeek;

	EXPECT_EQ(gateway->getBestPortalRecruitmentDwelling(town, hero), pikeSource);
	EXPECT_EQ(gateway->getBestPortalRecruitmentDwelling(town, nullptr), nullptr)
		<< "a Portal destination must be this town, its visiting hero, or its garrison hero";

	const PlayerColor originalOwner = pikeSource->tempOwner;
	pikeSource->tempOwner = OTHER_PLAYER;
	EXPECT_EQ(gateway->getBestPortalRecruitmentDwelling(town, hero), nullptr)
		<< "a saved link does not authorize a source that is no longer owned";
	pikeSource->tempOwner = originalOwner;

	EXPECT_EQ(town->portalSourceDwellingId, savedSource);
	EXPECT_EQ(town->portalLastSelectionWeek, savedWeek);
	EXPECT_EQ(pikeSource->creatures.front().first, 24u);
}

TEST_F(NewHorizonsPortalRecruitmentTest, ArmyCandidatesRespectFreeExternalStockCostsPhysicalSlotsAndLeadership)
{
	startGame();
	const auto gateway = makeGateway(PLAYER);
	const auto * armyManager = gateway->nullkiller->armyManager.get();
	ASSERT_NE(armyManager, nullptr);

	const auto pikemanCapacity = hero->getLeadershipSlotCapacity(pikeman);
	ASSERT_TRUE(pikemanCapacity);
	ASSERT_GT(pikemanCapacity->maximum, 0);
	EXPECT_TRUE(pikeSource->getRecruitmentCost(pikeman).empty())
		<< "external level-one Portal stock is free under the ordinary dwelling cost rule";
	const auto freeCandidates = armyManager->getArmyAvailableToBuy(
		hero, pikeSource, ResourceSet(), 0, hero);
	ASSERT_EQ(freeCandidates.size(), 1u);
	EXPECT_EQ(freeCandidates.front().creID, pikeman);
	EXPECT_EQ(freeCandidates.front().count, std::min<int>(24, pikemanCapacity->maximum));

	// Seven full physical slots prevent the AI from inventing an eighth stack
	// when the offered Pike stack is not valuable enough to replace any resident.
	hero->clearSlots();
	const std::array<CreatureID, GameConstants::ARMY_SIZE> fullArmy = {
		creature("core:archer"), creature("core:griffin"), creature("core:swordsman"),
		creature("core:monk"), creature("core:cavalier"), creature("core:crusader"),
		creature("core:angel")
	};
	for(size_t slot = 0; slot < fullArmy.size(); ++slot)
		ASSERT_TRUE(hero->setCreature(SlotID(slot), fullArmy[slot], 1));
	ASSERT_EQ(hero->stacksCount(), GameConstants::ARMY_SIZE);
	pikeSource->creatures = {{1, {pikeman}}};
	EXPECT_TRUE(armyManager->getArmyAvailableToBuy(hero, pikeSource, ResourceSet(), 0, hero).empty());
	EXPECT_EQ(pikeSource->creatures.front().first, 1u);

	// Non-level-one creatures retain their normal source cost; the candidate is
	// limited to what the supplied treasury can actually afford and the hero can command.
	hero->clearSlots();
	swordsmanSource->creatures = {{10, {swordsman}}};
	const ResourceSet swordsmanCost = swordsmanSource->getRecruitmentCost(swordsman);
	ASSERT_FALSE(swordsmanCost.empty());
	const auto swordsmanCapacity = hero->getLeadershipSlotCapacity(swordsman);
	ASSERT_TRUE(swordsmanCapacity);
	ASSERT_GE(swordsmanCapacity->maximum, 2);
	const ResourceSet exactTwoSwordsmen = swordsmanCost * 2;
	const auto costedCandidates = armyManager->getArmyAvailableToBuy(
		hero, swordsmanSource, exactTwoSwordsmen, 0, hero);
	ASSERT_EQ(costedCandidates.size(), 1u);
	EXPECT_EQ(costedCandidates.front().creID, swordsman);
	EXPECT_EQ(costedCandidates.front().count, 2);
	EXPECT_EQ(swordsmanSource->creatures.front().first, 10u);
}

TEST_F(NewHorizonsPortalRecruitmentTest, LegacySavedMagicRulesDisablePortalCandidates)
{
	useNewHorizonsMagicRules = false;
	startGame();
	ASSERT_FALSE(newHorizonsMagic::rulesActive(gameState()->getMagicRules()));
	const auto gateway = makeGateway(PLAYER);
	EXPECT_EQ(gateway->getBestPortalRecruitmentDwelling(town, hero), nullptr);
	EXPECT_EQ(pikeSource->creatures.front().first, 24u);
	EXPECT_EQ(town->portalSourceDwellingId, ObjectInstanceID::NONE);
}
