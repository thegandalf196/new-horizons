/*
 * NewHorizonsFountainOfFortuneTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "../../mock/GameHandlerTestServer.h"
#include "../../mock/TinyH3MBuilder.h"
#include "../../mock/TinyMapGameTest.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusEnum.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/bonuses/CBonusSystemNode.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/bonuses/Propagators.h"
#include "../../../lib/bonuses/Updaters.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/callback/Calendar.h"
#include "../../../lib/entities/building/CBuilding.h"
#include "../../../lib/entities/faction/CTown.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapObjects/TownBuildingInstance.h"
#include "../../../lib/mapObjects/army/CStackInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/rewardable/Configuration.h"
#include "../../../lib/rewardable/Info.h"
#include "../../../lib/rewardable/Reward.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"

#include <algorithm>
#include <vector>

namespace
{
using Rewardable::Reward;

BonusSourceID fountainSource(const CGTownInstance & town)
{
	const auto & building = town.getTown()->buildings.at(BuildingID::SPECIAL_2);
	return BonusSourceID(building->getUniqueTypeID());
}

std::vector<const Bonus *> luckBonusesFrom(const CBonusSystemNode & bearer, const BonusSourceID & source)
{
	std::vector<const Bonus *> result;
	const auto bonuses = bearer.getBonuses(Selector::source(BonusSource::TOWN_STRUCTURE, source)
		.And(Selector::type()(BonusType::LUCK)));
	for(const auto & bonus : *bonuses)
	{
		if(bonus)
			result.push_back(bonus.get());
	}
	return result;
}

std::vector<const Bonus *> oneBattleLuckBonusesFrom(const CGHeroInstance & hero, const BonusSourceID & source)
{
	auto result = luckBonusesFrom(hero, source);
	std::erase_if(result, [](const Bonus * bonus)
	{
		return !Bonus::OneBattle(bonus);
	});
	return result;
}

int32_t luckValueFrom(const CBonusSystemNode & bearer, const BonusSourceID & source)
{
	int32_t result = 0;
	for(const auto * bonus : luckBonusesFrom(bearer, source))
		result += bonus->val;
	return result;
}

class NewHorizonsFountainOfFortuneTest : public TinyMapGameTest
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
	}

	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void configurePlayer(PlayerSettings & settings) const override
	{
		if(settings.color == PlayerColor(0))
			settings.connectedPlayerIDs.clear();
	}
};
}

TEST_F(NewHorizonsFountainOfFortuneTest, RealWeeklyVisitsAreHeroAndFountainScopedAndLuckExpiresAfterBattle)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name("NewHorizonsFountainOfFortune")
		.playerActive(PlayerColor(0))
		.playerActive(PlayerColor(1))
		.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0)).heroGarrison({{pikeman, 1}})
		.hero({7, 5, 0}, HeroTypeID(1), PlayerColor(0)).heroGarrison({{pikeman, 1}})
		.hero({9, 5, 0}, HeroTypeID(2), PlayerColor(1)).heroGarrison({{pikeman, 1}})
		.town({18, 18, 0}, FactionID::RAMPART, PlayerColor(0)).townGarrison({{pikeman, 3}})
		.town({27, 18, 0}, FactionID::RAMPART, PlayerColor(0)).townGarrison({});
	startWithMap(std::move(builder));

	auto * firstFountain = expectAt<CGTownInstance>({18, 18, 0});
	auto * secondFountain = expectAt<CGTownInstance>({27, 18, 0});
	auto * visitor = findHeroAt({5, 5, 0});
	auto * secondHero = findHeroAt({7, 5, 0});
	auto * remoteHero = findHeroAt({9, 5, 0});
	ASSERT_NE(firstFountain, nullptr);
	ASSERT_NE(secondFountain, nullptr);
	ASSERT_NE(visitor, nullptr);
	ASSERT_NE(secondHero, nullptr);
	ASSERT_NE(remoteHero, nullptr);
	ASSERT_TRUE(firstFountain->rewardableBuildings.contains(BuildingID::SPECIAL_2));
	ASSERT_TRUE(secondFountain->rewardableBuildings.contains(BuildingID::SPECIAL_2));

	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler gameHandler(server, gameState());
	gameHandler.randomizer->setSeed(214);
	ASSERT_TRUE(gameHandler.buildStructure(firstFountain->id, BuildingID::SPECIAL_2, true));
	ASSERT_TRUE(gameHandler.buildStructure(secondFountain->id, BuildingID::SPECIAL_2, true));
	ASSERT_TRUE(firstFountain->hasBuilt(BuildingID::SPECIAL_2));
	ASSERT_TRUE(secondFountain->hasBuilt(BuildingID::SPECIAL_2));

	const auto firstSource = fountainSource(*firstFountain);
	const auto secondSource = fountainSource(*secondFountain);
	ASSERT_EQ(firstSource, secondSource);

	// A local town bonus is inherited by the garrison stack, but not by the
	// visiting hero through the separate town-and-visitor bonus node.
	const auto defensiveLuck = luckBonusesFrom(firstFountain->getStack(SlotID(0)), firstSource);
	ASSERT_EQ(defensiveLuck.size(), 1u);
	EXPECT_EQ(defensiveLuck.front()->val, 3);
	EXPECT_FALSE(Bonus::OneBattle(defensiveLuck.front()));
	EXPECT_TRUE(luckBonusesFrom(*visitor, firstSource).empty());
	EXPECT_TRUE(luckBonusesFrom(*secondHero, firstSource).empty());
	EXPECT_TRUE(luckBonusesFrom(*remoteHero, firstSource).empty());

	auto * rewardable = firstFountain->rewardableBuildings.at(BuildingID::SPECIAL_2).get();
	ASSERT_NE(rewardable, nullptr);
	ASSERT_EQ(rewardable->configuration.visitMode, Rewardable::VISIT_HERO);
	ASSERT_EQ(rewardable->configuration.resetParameters.weeks, 1);
	ASSERT_TRUE(rewardable->configuration.resetParameters.visitors);
	const auto rewardInfo = std::find_if(rewardable->configuration.info.begin(), rewardable->configuration.info.end(), [](const Rewardable::VisitInfo & info)
	{
		return info.visitType == Rewardable::EEventType::EVENT_FIRST_VISIT && !info.reward.heroBonuses.empty();
	});
	ASSERT_NE(rewardInfo, rewardable->configuration.info.end());
	const Reward & reward = rewardInfo->reward;
	ASSERT_EQ(reward.heroBonuses.size(), 1u);
	EXPECT_EQ(reward.heroBonuses.front()->type, BonusType::LUCK);
	EXPECT_EQ(reward.heroBonuses.front()->val, 2);
	EXPECT_TRUE(Bonus::OneBattle(reward.heroBonuses.front().get()));
	EXPECT_FALSE(gameState()->getPlayerState(PlayerColor(0))->isHuman())
		<< "The authoritative Fountain visit must also grant to a computer-owned hero";

	auto visit = [&](CGTownInstance * fountain, CGHeroInstance * hero)
	{
		fountain->setVisitingHero(hero);
		gameHandler.heroVisitCastle(fountain, hero);
		fountain->setVisitingHero(nullptr);
	};

	visit(firstFountain, visitor);
	const auto firstBlessing = oneBattleLuckBonusesFrom(*visitor, firstSource);
	ASSERT_EQ(firstBlessing.size(), 1u);
	EXPECT_EQ(firstBlessing.front()->val, 2);
	EXPECT_TRUE(firstFountain->rewardableBuildings.at(BuildingID::SPECIAL_2)->wasVisited(visitor));

	// This Fountain is limited per hero, not per day or per battle: consuming the
	// effect later must not reopen its weekly visit entitlement.
	visit(firstFountain, visitor);
	EXPECT_EQ(oneBattleLuckBonusesFrom(*visitor, firstSource).size(), 1u);

	// A second physical Fountain can independently bless the same hero; the
	// ordinary Luck bonuses have no shared stacking key and remain additive.
	visit(secondFountain, visitor);
	ASSERT_EQ(oneBattleLuckBonusesFrom(*visitor, firstSource).size(), 2u);
	EXPECT_EQ(luckValueFrom(*visitor, firstSource), 4);
	EXPECT_TRUE(secondFountain->rewardableBuildings.at(BuildingID::SPECIAL_2)->wasVisited(visitor));

	// The same physical Fountain may independently bless another hero.
	visit(firstFountain, secondHero);
	ASSERT_EQ(oneBattleLuckBonusesFrom(*secondHero, firstSource).size(), 1u);
	EXPECT_EQ(oneBattleLuckBonusesFrom(*secondHero, firstSource).front()->val, 2);
	EXPECT_TRUE(firstFountain->rewardableBuildings.at(BuildingID::SPECIAL_2)->wasVisited(secondHero));

	// Neither the permanent defensive +3 nor another hero's reward becomes a
	// kingdom-wide hero bonus.
	EXPECT_TRUE(luckBonusesFrom(*remoteHero, firstSource).empty());
	EXPECT_EQ(luckBonusesFrom(*visitor, firstSource).size(), 2u);
	for(const auto * bonus : luckBonusesFrom(*visitor, firstSource))
	{
		EXPECT_EQ(bonus->val, 2);
		EXPECT_TRUE(Bonus::OneBattle(bonus));
	}

	const auto firstFountainID = firstFountain->id;
	const auto secondFountainID = secondFountain->id;
	const auto visitorID = visitor->id;
	const auto secondHeroID = secondHero->id;
	const auto remoteHeroID = remoteHero->id;
	const auto save = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(save);
	auto * restoredFirstFountain = restored.getTown(firstFountainID);
	auto * restoredSecondFountain = restored.getTown(secondFountainID);
	auto * restoredVisitor = restored.getHero(visitorID);
	auto * restoredSecondHero = restored.getHero(secondHeroID);
	auto * restoredRemoteHero = restored.getHero(remoteHeroID);
	ASSERT_NE(restoredFirstFountain, nullptr);
	ASSERT_NE(restoredSecondFountain, nullptr);
	ASSERT_NE(restoredVisitor, nullptr);
	ASSERT_NE(restoredSecondHero, nullptr);
	ASSERT_NE(restoredRemoteHero, nullptr);
	EXPECT_TRUE(restoredFirstFountain->rewardableBuildings.at(BuildingID::SPECIAL_2)->wasVisited(restoredVisitor));
	EXPECT_TRUE(restoredFirstFountain->rewardableBuildings.at(BuildingID::SPECIAL_2)->wasVisited(restoredSecondHero));
	EXPECT_TRUE(restoredSecondFountain->rewardableBuildings.at(BuildingID::SPECIAL_2)->wasVisited(restoredVisitor));
	EXPECT_EQ(oneBattleLuckBonusesFrom(*restoredVisitor, firstSource).size(), 2u);
	EXPECT_EQ(oneBattleLuckBonusesFrom(*restoredSecondHero, firstSource).size(), 1u);
	EXPECT_TRUE(luckBonusesFrom(*restoredRemoteHero, firstSource).empty());
	const auto restoredDefensiveLuck = luckBonusesFrom(restoredFirstFountain->getStack(SlotID(0)), firstSource);
	ASSERT_EQ(restoredDefensiveLuck.size(), 1u);
	EXPECT_EQ(restoredDefensiveLuck.front()->val, 3);

	BattleResultAccepted acceptedResult;
	acceptedResult.battleID = BattleID(0);
	acceptedResult.heroResult[BattleSide::ATTACKER].heroID = visitorID;
	acceptedResult.heroResult[BattleSide::DEFENDER].heroID = remoteHeroID;
	acceptedResult.winnerSide = BattleSide::ATTACKER;
	gameHandler.sendAndApply(acceptedResult);
	EXPECT_TRUE(oneBattleLuckBonusesFrom(*visitor, firstSource).empty());
	EXPECT_EQ(oneBattleLuckBonusesFrom(*secondHero, firstSource).size(), 1u);
	EXPECT_TRUE(firstFountain->rewardableBuildings.at(BuildingID::SPECIAL_2)->wasVisited(visitor));

	// The same hero remains blocked after the one-battle effect is consumed.
	visit(firstFountain, visitor);
	EXPECT_TRUE(oneBattleLuckBonusesFrom(*visitor, firstSource).empty());

	const auto calendar = gameState()->getCalendar();
	const int resetDuration = firstFountain->rewardableBuildings.at(BuildingID::SPECIAL_2)
		->configuration.getResetDuration(calendar);
	ASSERT_GT(resetDuration, 0);
	int nextResetDay = calendar.getCurrentDay() + 1;
	while(nextResetDay <= 1 || (nextResetDay - 1) % resetDuration != 0)
		++nextResetDay;
	const int turnsUntilReset = nextResetDay - calendar.getCurrentDay();
	for(int day = 0; day < turnsUntilReset; ++day)
		gameHandler.onNewTurn();
	EXPECT_EQ(gameState()->getCalendar().getCurrentDay(), nextResetDay);
	EXPECT_FALSE(firstFountain->rewardableBuildings.at(BuildingID::SPECIAL_2)->wasVisited(visitor));
	EXPECT_FALSE(firstFountain->rewardableBuildings.at(BuildingID::SPECIAL_2)->wasVisited(secondHero));
	EXPECT_FALSE(secondFountain->rewardableBuildings.at(BuildingID::SPECIAL_2)->wasVisited(visitor));

	visit(firstFountain, visitor);
	const auto renewedLuck = oneBattleLuckBonusesFrom(*visitor, firstSource);
	ASSERT_EQ(renewedLuck.size(), 1u);
	EXPECT_EQ(renewedLuck.front()->val, 2);
	EXPECT_TRUE(firstFountain->rewardableBuildings.at(BuildingID::SPECIAL_2)->wasVisited(visitor));
}
