/*
 * NewHorizonsMerchantPrinceTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../../AI/Nullkiller2/Analyzers/BuildAnalyzer.h"
#include "../../AI/Nullkiller2/Engine/ResourceTrader.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/ResourceSet.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../lib/entities/ResourceTypeHandler.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/IMarket.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/callback/IClient.h"
#include "../../lib/networkPacks/PacksForServer.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../server/CGameHandler.h"
#include "../mock/GameHandlerTestServer.h"
#include "../mock/TinyMapGameTest.h"

namespace
{

const PlayerColor PLAYER(0);
constexpr auto ESTATES_SKILL = "new-horizons:estates";
constexpr auto MERCHANT_PRINCE = "new-horizons:estates.merchantPrince";
constexpr auto RESOURCE_BROKER = "new-horizons:estates.resourceBroker";

class MockBuildAnalyzer final : public NK2AI::BuildAnalyzer
{
public:
	explicit MockBuildAnalyzer() : BuildAnalyzer(nullptr) {}
	MOCK_METHOD(bool, isGoldPressureOverMax, (), (const, override));
};

class ResourceTradeLoopbackClient final : public IClient
{
public:
	explicit ResourceTradeLoopbackClient(CGameHandler & handler)
		: handler(handler)
	{}

	std::vector<TradeOnMarketplace> submittedTrades;

	std::optional<BattleAction> makeSurrenderRetreatDecision(
		PlayerColor, const BattleID &, const BattleStateInfoForRetreat &) override
	{
		return std::nullopt;
	}

	int sendRequest(const CPackForServer & request, PlayerColor player, bool) override
	{
		const auto * trade = dynamic_cast<const TradeOnMarketplace *>(&request);
		if(!trade)
			throw std::runtime_error("Resource Broker AI fixture expected a marketplace trade request");
		submittedTrades.push_back(*trade);
		auto serverRequest = CMemorySerializer::deepCopy(request);
		serverRequest->player = player;
		serverRequest->requestID = ++lastRequestId;
		handler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, *serverRequest);
		return lastRequestId;
	}

private:
	CGameHandler & handler;
	int lastRequestId = 0;
};

std::pair<int, int> expectedResourceOffer(double effectiveness, GameResID sell, GameResID buy)
{
	const double sellPrice = sell.toResource()->getPrice();
	const double buyPrice = buy.toResource()->getPrice() / effectiveness;
	if(sellPrice > buyPrice)
		return {1, static_cast<int>(std::ceil(sellPrice / buyPrice))};
	return {static_cast<int>((buyPrice / sellPrice) + 0.5), 1};
}

class NewHorizonsMerchantPrinceTest : public TinyMapGameTest
{
protected:
	bool legacyPerks = false;
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			legacyPerks ? JsonNode() : JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}
	void acquireMerchantPrince(CGHeroInstance * hero)
	{
		hero->setSecSkillLevel(estatesSkill(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		hero->applyPerkSelection({ESTATES_SKILL, MERCHANT_PRINCE});
		ASSERT_TRUE(hero->hasActivePerk(ESTATES_SKILL, MERCHANT_PRINCE));
	}
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void startGame()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER)
			.town({5, 5, 0}, FactionID::CASTLE, PLAYER)
			.town({9, 5, 0}, FactionID::CASTLE, PLAYER)
			.town({13, 5, 0}, FactionID::CASTLE, PLAYER)
			.hero({30, 30, 0}, HeroTypeID(0), PLAYER)
			.hero({30, 26, 0}, HeroTypeID(1), PLAYER)
			.hero({30, 22, 0}, HeroTypeID(2), PLAYER);
		startWithMap(std::move(builder));

		marketTown = expectAt<CGTownInstance>({5, 5, 0});
		remoteTown = expectAt<CGTownInstance>({9, 5, 0});
		emptyTown = expectAt<CGTownInstance>({13, 5, 0});
		firstHero = findHeroAt({30, 30, 0});
		secondHero = findHeroAt({30, 26, 0});
		unskilledHero = findHeroAt({30, 22, 0});
		ASSERT_NE(marketTown, nullptr);
		ASSERT_NE(remoteTown, nullptr);
		ASSERT_NE(emptyTown, nullptr);
		ASSERT_NE(firstHero, nullptr);
		ASSERT_NE(secondHero, nullptr);
		ASSERT_NE(unskilledHero, nullptr);
		// addBuilding alone sets the gate but omits the PLAYER-propagated
		// Marketplace-count bonus. Use ordinary structure publication instead.
		GameHandlerTestServer server(gameState(), PLAYER);
		CGameHandler handler(server, gameState());
		ASSERT_TRUE(handler.buildStructure(marketTown->id, BuildingID::MARKETPLACE, true));
		ASSERT_TRUE(handler.buildStructure(remoteTown->id, BuildingID::MARKETPLACE, true));
		ASSERT_GT(marketTown->getMarketEfficiency(), 0);
	}

	static SecondarySkill estatesSkill()
	{
		const int decoded = SecondarySkill::decode(ESTATES_SKILL);
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	static std::pair<int, int> offer(const IMarket & market, GameResID sell, GameResID buy)
	{
		int given = 0;
		int received = 0;
		EXPECT_TRUE(market.getOffer(sell, buy, given, received, EMarketMode::RESOURCE_RESOURCE));
		return {given, received};
	}

	CGTownInstance * marketTown = nullptr;
	CGTownInstance * remoteTown = nullptr;
	CGTownInstance * emptyTown = nullptr;
	CGHeroInstance * firstHero = nullptr;
	CGHeroInstance * secondHero = nullptr;
	CGHeroInstance * unskilledHero = nullptr;
};
}

TEST_F(NewHorizonsMerchantPrinceTest, SelectedVisitorAddsTwoVirtualMarketsAndExactResourceOffer)
{
	startGame();
	const int ordinary = marketTown->getMarketEfficiency();
	ASSERT_GT(ordinary, 0);
	ASSERT_NO_FATAL_FAILURE(acquireMerchantPrince(firstHero));
	marketTown->setVisitingHero(firstHero);
	ASSERT_EQ(firstHero->getVisitedTown(), marketTown);
	EXPECT_EQ(marketTown->getMarketEfficiency(), ordinary + 2);
	const auto wood = GameResID(EGameResID::WOOD);
	const auto gems = GameResID(EGameResID::GEMS);
	EXPECT_EQ(offer(*marketTown, wood, gems),
		expectedResourceOffer(std::min((ordinary + 3.0) / 20.0, 0.5), wood, gems));
}

TEST_F(NewHorizonsMerchantPrinceTest, SelectedGarrisonResidentQualifies)
{
	startGame();
	const int ordinary = marketTown->getMarketEfficiency();
	ASSERT_NO_FATAL_FAILURE(acquireMerchantPrince(firstHero));
	marketTown->setGarrisonedHero(firstHero);
	ASSERT_EQ(firstHero->getVisitedTown(), marketTown);
	EXPECT_EQ(marketTown->getMarketEfficiency(), ordinary + 2);
}

TEST_F(NewHorizonsMerchantPrinceTest, TwoResidentHoldersApplyOnceAndDoNotImproveAnotherTown)
{
	startGame();
	const int ordinary = marketTown->getMarketEfficiency();
	const int remoteOrdinary = remoteTown->getMarketEfficiency();
	ASSERT_NO_FATAL_FAILURE(acquireMerchantPrince(firstHero));
	ASSERT_NO_FATAL_FAILURE(acquireMerchantPrince(secondHero));
	marketTown->setGarrisonedHero(firstHero);
	marketTown->setVisitingHero(secondHero);
	EXPECT_EQ(marketTown->getMarketEfficiency(), ordinary + 2);
	EXPECT_EQ(remoteTown->getMarketEfficiency(), remoteOrdinary);
}

TEST_F(NewHorizonsMerchantPrinceTest, DepartureAndUnselectedResidentKeepOrdinaryOffers)
{
	startGame();
	const int ordinary = marketTown->getMarketEfficiency();
	ASSERT_NO_FATAL_FAILURE(acquireMerchantPrince(firstHero));
	marketTown->setVisitingHero(firstHero);
	ASSERT_EQ(marketTown->getMarketEfficiency(), ordinary + 2);
	marketTown->setVisitingHero(nullptr);
	EXPECT_EQ(marketTown->getMarketEfficiency(), ordinary);
	marketTown->setVisitingHero(unskilledHero);
	EXPECT_FALSE(unskilledHero->hasActivePerk(ESTATES_SKILL, MERCHANT_PRINCE));
	EXPECT_EQ(marketTown->getMarketEfficiency(), ordinary);
}

TEST_F(NewHorizonsMerchantPrinceTest, AbsentSavedPerkRegistryDoesNotUseInstalledActiveDefaults)
{
	legacyPerks = true;
	startGame();
	const int ordinary = marketTown->getMarketEfficiency();
	firstHero->setSecSkillLevel(estatesSkill(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	marketTown->setVisitingHero(firstHero);
	EXPECT_FALSE(firstHero->hasActivePerk(ESTATES_SKILL, MERCHANT_PRINCE));
	EXPECT_EQ(marketTown->getMarketEfficiency(), ordinary);
}

TEST_F(NewHorizonsMerchantPrinceTest, ResidenceCannotCreateAMissingMarketplace)
{
	startGame();
	ASSERT_NO_FATAL_FAILURE(acquireMerchantPrince(firstHero));
	emptyTown->setVisitingHero(firstHero);
	ASSERT_FALSE(emptyTown->hasBuiltResourceMarketplace());
	EXPECT_EQ(emptyTown->getMarketEfficiency(), 0);
	EXPECT_FALSE(emptyTown->allowsTrade(EMarketMode::RESOURCE_RESOURCE));
}

TEST_F(NewHorizonsMerchantPrinceTest, OrdinaryRateCapAndResourceBrokerCompositionArePreserved)
{
	startGame();
	const int ordinary = marketTown->getMarketEfficiency();
	ASSERT_NO_FATAL_FAILURE(acquireMerchantPrince(firstHero));
	firstHero->setSecSkillLevel(estatesSkill(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	firstHero->applyPerkSelection({ESTATES_SKILL, RESOURCE_BROKER});
	ASSERT_TRUE(firstHero->hasActivePerk(ESTATES_SKILL, RESOURCE_BROKER));
	marketTown->setVisitingHero(firstHero);
	const auto wood = GameResID(EGameResID::WOOD);
	const auto gems = GameResID(EGameResID::GEMS);
	EXPECT_DOUBLE_EQ(marketTown->getResourceExchangeEffectiveness(wood, gems),
		std::min((ordinary + 3.0) / 20.0, 0.5) * 1.2);
	gameState()->getPlayerState(PLAYER)->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MARKETPLACE_ACCESS, BonusSource::OTHER, 100, BonusSourceID()));
	EXPECT_EQ(marketTown->getMarketEfficiency(), ordinary + 102);
	EXPECT_DOUBLE_EQ(marketTown->getMarketExchangeEffectiveness(), 0.5);
	EXPECT_DOUBLE_EQ(marketTown->getResourceExchangeEffectiveness(wood, gems), 0.6);
}

TEST_F(NewHorizonsMerchantPrinceTest, ResourceTraderAndAuthoritativeTradeUseImprovedTownQuote)
{
	startGame();
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	const auto wood = GameResID(EGameResID::WOOD);
	const auto crystal = GameResID(EGameResID::CRYSTAL);
	const auto ordinaryQuote = offer(*marketTown, wood, crystal);
	ASSERT_NO_FATAL_FAILURE(acquireMerchantPrince(firstHero));
	marketTown->setVisitingHero(firstHero);
	const auto quoted = offer(*marketTown, wood, crystal);
	ASSERT_GT(quoted.first, 0);
	ASSERT_LT(quoted.first, ordinaryQuote.first);
	handler.giveResources(PLAYER, -gameState()->getPlayerState(PLAYER)->resources);
	ResourceSet supply;
	supply[EGameResID::WOOD] = 100000;
	handler.giveResources(PLAYER, supply);
	const auto before = gameState()->getPlayerState(PLAYER)->resources;
	gameState()->actingPlayers.insert(PLAYER);
	TResources missingNow;
	missingNow[EGameResID::CRYSTAL] = 1;
	TResources income;
	TResources freeAfterMissingTotal;
	freeAfterMissingTotal[EGameResID::WOOD] = 100000;
	MockBuildAnalyzer analyzer;
	EXPECT_CALL(analyzer, isGoldPressureOverMax()).Times(0);
	ResourceTradeLoopbackClient client(handler);
	const auto callback = makeCallback(PLAYER, &client);
	ASSERT_TRUE(NK2AI::ResourceTrader::tradeHelper(NK2AI::ResourceTrader::EXPENDABLE_BULK_RATIO,
		*marketTown, missingNow, income, freeAfterMissingTotal, analyzer, *callback));
	ASSERT_EQ(client.submittedTrades.size(), 1u);
	const auto & submitted = client.submittedTrades.front();
	EXPECT_EQ(submitted.marketId, marketTown->id);
	EXPECT_EQ(submitted.mode, EMarketMode::RESOURCE_RESOURCE);
	ASSERT_EQ(submitted.val.size(), 1u);
	EXPECT_EQ(submitted.val.front(), static_cast<ui32>(quoted.first));
	const auto after = gameState()->getPlayerState(PLAYER)->resources;
	EXPECT_EQ(after[EGameResID::WOOD], before[EGameResID::WOOD] - quoted.first);
	EXPECT_EQ(after[EGameResID::CRYSTAL], before[EGameResID::CRYSTAL] + quoted.second);
}
