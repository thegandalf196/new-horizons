/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../mock/GameHandlerTestServer.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/entities/hero/NewHorizonsInfernoAdventureSpecialties.h"
#include "../../lib/entities/hero/NewHorizonsInfernoSpecialties.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGResource.h"
#include "../../lib/mapObjects/NewHorizonsEstatesPolicy.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../server/CGameHandler.h"
#include "../../server/battles/BattleInfernoRewards.h"

using namespace newHorizonsHeroes;

TEST(InfernoAdventureSpecialties, CutpurseScalesCapsRoundsDownAndPreservesRecipientHeadroom)
{
	const CutpurseRules rules{5, 50, 15};
	EXPECT_EQ(cutpurseGold(rules, 1, 10000, 0), 500);
	EXPECT_EQ(cutpurseGold(rules, 10, 10000, 0), 950);
	EXPECT_EQ(cutpurseGold(rules, 20, 10000, 0), 1450);
	EXPECT_EQ(cutpurseGold(rules, 21, 10000, 0), 1500);
	EXPECT_EQ(cutpurseGold(rules, 100, 10000, 0), 1500);
	EXPECT_EQ(cutpurseGold(rules, 1, 18, 0), 0);
	EXPECT_EQ(cutpurseGold(rules, 1, -1, 0), 0);
	EXPECT_EQ(cutpurseGold(CutpurseRules{10000, 10000, 10000}, 100, 10000, 0), 10000);
	EXPECT_EQ(cutpurseGold(rules, 100, 10000, GameConstants::PLAYER_RESOURCES_CAP - 9), 9);
	EXPECT_EQ(cutpurseGold(rules, 100, 10000, GameConstants::PLAYER_RESOURCES_CAP), 0);
}

TEST(InfernoAdventureSpecialties, SulfurSenseUsesTenLevelThresholdsAndLegacyAbsenceIsInactive)
{
	const SulfurSenseRules rules{1, 10};
	EXPECT_EQ(sulfurSenseAmount(rules, 1), 1);
	EXPECT_EQ(sulfurSenseAmount(rules, 9), 1);
	EXPECT_EQ(sulfurSenseAmount(rules, 10), 2);
	EXPECT_EQ(sulfurSenseAmount(rules, 20), 3);
	EXPECT_EQ(sulfurSenseAmount(nullptr), 0);
	EXPECT_EQ(cutpurseGold(nullptr, 10000, 0), 0);
	ResourceSet base;
	base[EGameResID::GOLD] = 100;
	EXPECT_EQ(sulfurSensePickup(base, nullptr), base);
}

class InfernoAdventureRewards : public TinyMapGameTest
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
	}
	void startInfernoScenario()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:octavia")), PlayerColor(0))
			.hero({10, 5, 0}, HeroTypeID(HeroTypeID::decode("core:calid")), PlayerColor(0))
			.hero({20, 5, 0}, HeroTypeID(0), PlayerColor(1))
			.resource({10, 10, 0}, GameResID::GOLD, 10)
			.resource({12, 10, 0}, GameResID::SULFUR, 3);
		startWithMap(std::move(builder));
	}
};

TEST_F(InfernoAdventureRewards, SharedPileQuoteAddsSulfurOnceAndLeavesOtherHeroesUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(startInfernoScenario());
	auto * calid = findHeroAt({10, 5, 0});
	auto * other = findHeroAt({5, 5, 0});
	auto * gold = expectAt<CGResource>({10, 10, 0});
	auto * sulfur = expectAt<CGResource>({12, 10, 0});
	ASSERT_TRUE(calid && other && gold && sulfur);
	const auto goldBase = gold->getVisitResources(nullptr);
	const auto sulfurBase = sulfur->getVisitResources(nullptr);
	ASSERT_EQ(sulfurSenseAmount(calid), 1);
	EXPECT_EQ(gold->getVisitResources(calid)[EGameResID::GOLD], goldBase[EGameResID::GOLD]);
	EXPECT_EQ(gold->getVisitResources(calid)[EGameResID::SULFUR], 1);
	EXPECT_EQ(sulfur->getVisitResources(calid)[EGameResID::SULFUR], sulfurBase[EGameResID::SULFUR] + 1);
	EXPECT_EQ(gold->getVisitResources(other), newHorizonsEstates::visitReward(goldBase, other));
	EXPECT_EQ(sulfurSenseAmount(other), 0);
	EXPECT_EQ(gold->getVisitResources(nullptr), goldBase);
	ResourceSet huge;
	huge[EGameResID::SULFUR] = std::numeric_limits<TResource>::max();
	EXPECT_EQ(sulfurSensePickup(huge, calid), huge);
}

TEST_F(InfernoAdventureRewards, ResourcePickupAppliesTheSharedQuoteAndRemovesThePile)
{
	ASSERT_NO_FATAL_FAILURE(startInfernoScenario());
	auto * calid = findHeroAt({10, 5, 0});
	auto * gold = expectAt<CGResource>({10, 10, 0});
	ASSERT_TRUE(calid && gold);
	const auto before = gameState()->getPlayerState(PlayerColor(0))->resources;
	const auto quoted = gold->getVisitResources(calid);
	const auto object = gold->id;
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	gold->onHeroVisit(handler, calid);
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources, before + quoted);
	EXPECT_EQ(gameState()->getObj(object, false), nullptr);
}

TEST_F(InfernoAdventureRewards, VictoryTransferConservesGoldForLastEnemyHeroAndRejectsInvalidOpponents)
{
	ASSERT_NO_FATAL_FAILURE(startInfernoScenario());
	auto * winner = findHeroAt({5, 5, 0});
	auto * loser = findHeroAt({20, 5, 0});
	ASSERT_TRUE(winner && loser);
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	for(const auto player : {PlayerColor(0), PlayerColor(1)})
	{
		SetResources resources;
		resources.player = player;
		resources.res[EGameResID::GOLD] = 10000;
		handler.sendAndApply(static_cast<CPackForClient &>(resources));
	}
	battleInfernoRewards::grantCutpurse(handler, winner, nullptr);
	battleInfernoRewards::grantCutpurse(handler, winner, winner);
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources[EGameResID::GOLD], 10000);
	const auto expected = cutpurseGold(winner, 10000, 10000);
	ASSERT_GT(expected, 0);
	battleInfernoRewards::grantCutpurse(handler, winner, loser);
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources[EGameResID::GOLD], 10000 + expected);
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(1))->resources[EGameResID::GOLD], 10000 - expected);
}

class InfernoHistoricalAdventureRewards : public InfernoAdventureRewards
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		InfernoAdventureRewards::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		rules["revisedSpecialties"].Struct().erase("cutpurse");
		rules["revisedSpecialties"].Struct().erase("sulfurSense");
		rules.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, std::move(rules));
	}
};

TEST_F(InfernoHistoricalAdventureRewards, AbsentCapturedRowsRetainDailyIncomeAndDisableNewMechanics)
{
	ASSERT_NO_FATAL_FAILURE(startInfernoScenario());
	const auto * octavia = findHeroAt({5, 5, 0});
	const auto * calid = findHeroAt({10, 5, 0});
	ASSERT_TRUE(octavia && calid);
	EXPECT_EQ(newHorizonsHeroes::cutpurseGold(octavia, 10000, 0), 0);
	EXPECT_EQ(newHorizonsHeroes::sulfurSenseAmount(calid), 0);
	int specialtyGold = 0;
	for(const auto & bonus : octavia->getExportedBonusList())
		if(bonus->type == BonusType::GENERATE_RESOURCE && bonus->source == BonusSource::HERO_SPECIAL
			&& bonus->sid == BonusSourceID(octavia->getHeroTypeID()) && bonus->subtype == BonusSubtypeID(GameResID(GameResID::GOLD)))
			specialtyGold += bonus->val;
	EXPECT_EQ(specialtyGold, 350);
	EXPECT_EQ(calid->dailyIncome()[EGameResID::SULFUR], 1);
}
