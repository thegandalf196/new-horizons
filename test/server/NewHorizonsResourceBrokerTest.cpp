/*
 * NewHorizonsResourceBrokerTest.cpp, part of VCMI engine
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
constexpr auto TAX_COLLECTOR = "new-horizons:estates.taxCollector";
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

class ExternalResourceMarket final : public IMarket
{
public:
	ExternalResourceMarket() : IMarket(nullptr) {}
	ObjectInstanceID getObjInstanceID() const override { return ObjectInstanceID(999); }
	int getMarketEfficiency() const override { return 1; }
	double getMarketExchangeEffectiveness() const override { return 0.35; }
	std::set<EMarketMode> availableModes() const override { return {EMarketMode::RESOURCE_RESOURCE}; }
};

std::pair<int, int> expectedResourceOffer(double effectiveness, GameResID sell, GameResID buy)
{
	const double sellPrice = sell.toResource()->getPrice();
	const double buyPrice = buy.toResource()->getPrice() / effectiveness;
	if(sellPrice > buyPrice)
		return {1, static_cast<int>(std::ceil(sellPrice / buyPrice))};
	return {static_cast<int>((buyPrice / sellPrice) + 0.5), 1};
}

class NewHorizonsResourceBrokerTest : public TinyMapGameTest
{
protected:
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
		marketTown->addBuilding(BuildingID::MARKETPLACE);
		remoteTown->addBuilding(BuildingID::MARKETPLACE);
	}

	static SecondarySkill estatesSkill()
	{
		const int decoded = SecondarySkill::decode(ESTATES_SKILL);
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	static bool selectOfferedPerk(CGameHandler & handler, CGHeroInstance * hero,
		std::string_view perkId, MasteryLevel::Type requiredRank)
	{
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};
		for(uint64_t seed = 0; seed < 10000; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			for(size_t index = 0; index < offer.size(); ++index)
			{
				if(offer[index].selection.perkId != perkId)
					continue;
				if(offer[index].requiredRank != requiredRank)
					return false;
				handler.levelUpHero(hero, offer, index, seed, false);
				return true;
			}
		}
		return false;
	}

	void acquireResourceBroker(CGameHandler & handler, CGHeroInstance * hero)
	{
		const auto estates = estatesSkill();
		handler.changeSecSkill(hero, estates, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
		handler.levelUpHero(hero, estates, false);
		ASSERT_EQ(hero->getSecSkillLevel(estates), MasteryLevel::BASIC);
		ASSERT_TRUE(selectOfferedPerk(handler, hero, TAX_COLLECTOR, MasteryLevel::BASIC));
		handler.levelUpHero(hero, estates, false);
		ASSERT_EQ(hero->getSecSkillLevel(estates), MasteryLevel::ADVANCED);
		ASSERT_TRUE(selectOfferedPerk(handler, hero, RESOURCE_BROKER, MasteryLevel::ADVANCED));
		ASSERT_TRUE(hero->hasActivePerk(ESTATES_SKILL, RESOURCE_BROKER));
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

TEST_F(NewHorizonsResourceBrokerTest, AdvancedOfferImprovesTownQuoteAndNullHeroTradeUsesIt)
{
	startGame();
	ASSERT_TRUE(marketTown->allowsTrade(EMarketMode::RESOURCE_RESOURCE));

	GameHandlerTestServer server(gameState());
	CGameHandler handler(server, gameState());
	const auto wood = GameResID(EGameResID::WOOD);
	const auto gems = GameResID(EGameResID::GEMS);
	const double ordinaryEffectiveness = marketTown->getMarketExchangeEffectiveness();
	const auto ordinaryExpected = expectedResourceOffer(ordinaryEffectiveness, wood, gems);
	EXPECT_DOUBLE_EQ(marketTown->getResourceExchangeEffectiveness(wood, gems), ordinaryEffectiveness);
	EXPECT_EQ(offer(*marketTown, wood, gems), ordinaryExpected);

	// This is a real Advanced Estates choice, not direct perk-state injection.
	acquireResourceBroker(handler, firstHero);
	marketTown->setVisitingHero(firstHero);
	ASSERT_EQ(firstHero->getVisitedTown(), marketTown);

	const double brokerEffectiveness = ordinaryEffectiveness * 1.2;
	EXPECT_DOUBLE_EQ(marketTown->getResourceExchangeEffectiveness(wood, gems), brokerEffectiveness);
	EXPECT_EQ(offer(*marketTown, wood, gems), expectedResourceOffer(brokerEffectiveness, wood, gems));
	EXPECT_LT(offer(*marketTown, wood, gems).first, ordinaryExpected.first);

	// Resource trading has no hero argument: a resident broker changes the town's
	// Marketplace offer itself, and the authoritative server uses that same offer.
	const auto quoted = offer(*marketTown, wood, gems);
	ResourceSet initial;
	initial[EGameResID::WOOD] = quoted.first * 3;
	handler.giveResources(PLAYER, initial);
	const auto before = gameState()->getPlayerState(PLAYER)->resources;
	ASSERT_TRUE(handler.tradeResources(marketTown, quoted.first * 3, PLAYER, wood, gems));
	const auto after = gameState()->getPlayerState(PLAYER)->resources;
	EXPECT_EQ(after[EGameResID::WOOD], before[EGameResID::WOOD] - quoted.first * 3);
	EXPECT_EQ(after[EGameResID::GEMS], before[EGameResID::GEMS] + quoted.second * 3);

	// A garrison resident qualifies too. Adding a second active holder does not
	// multiply the town's single +20% effectiveness bonus.
	marketTown->setVisitingHero(nullptr);
	marketTown->setGarrisonedHero(firstHero);
	EXPECT_EQ(firstHero->getVisitedTown(), marketTown);
	EXPECT_DOUBLE_EQ(marketTown->getResourceExchangeEffectiveness(wood, gems), brokerEffectiveness);
	acquireResourceBroker(handler, secondHero);
	marketTown->setVisitingHero(secondHero);
	EXPECT_DOUBLE_EQ(marketTown->getResourceExchangeEffectiveness(wood, gems), brokerEffectiveness);
	EXPECT_EQ(offer(*marketTown, wood, gems), expectedResourceOffer(brokerEffectiveness, wood, gems));
}

TEST_F(NewHorizonsResourceBrokerTest, ResidenceDirectionAndMarketGuardsKeepTheOrdinaryRate)
{
	startGame();
	GameHandlerTestServer server(gameState());
	CGameHandler handler(server, gameState());
	acquireResourceBroker(handler, firstHero);
	const auto wood = GameResID(EGameResID::WOOD);
	const auto ore = GameResID(EGameResID::ORE);
	const auto mercury = GameResID(EGameResID::MERCURY);
	const auto gold = GameResID(EGameResID::GOLD);
	const auto gems = GameResID(EGameResID::GEMS);
	const double ordinary = marketTown->getMarketExchangeEffectiveness();
	const double improved = ordinary * 1.2;

	// A resident gets the discount; after departure, an eligible hero elsewhere
	// cannot improve this town's quote.
	marketTown->setVisitingHero(firstHero);
	EXPECT_DOUBLE_EQ(marketTown->getResourceExchangeEffectiveness(wood, gems), improved);
	marketTown->setVisitingHero(nullptr);
	EXPECT_EQ(firstHero->getVisitedTown(), nullptr);
	EXPECT_DOUBLE_EQ(marketTown->getResourceExchangeEffectiveness(wood, gems), ordinary);
	remoteTown->setVisitingHero(firstHero);
	EXPECT_EQ(firstHero->getVisitedTown(), remoteTown);
	EXPECT_DOUBLE_EQ(marketTown->getResourceExchangeEffectiveness(wood, gems), ordinary);
	EXPECT_DOUBLE_EQ(remoteTown->getResourceExchangeEffectiveness(wood, gems), improved);
	remoteTown->setVisitingHero(nullptr);

	// A resident without the Advanced perk does not qualify; the Broker is local
	// to the town it visits, and a town without a Marketplace stays ordinary.
	marketTown->setVisitingHero(unskilledHero);
	EXPECT_DOUBLE_EQ(marketTown->getResourceExchangeEffectiveness(wood, gems), ordinary);
	emptyTown->setVisitingHero(firstHero);
	EXPECT_FALSE(emptyTown->allowsTrade(EMarketMode::RESOURCE_RESOURCE));
	EXPECT_DOUBLE_EQ(emptyTown->getResourceExchangeEffectiveness(wood, gems), emptyTown->getMarketExchangeEffectiveness());
	emptyTown->setVisitingHero(nullptr);

	// The bonus only covers Wood/Ore -> rare resources, not the reverse or Gold.
	marketTown->setVisitingHero(firstHero);
	EXPECT_DOUBLE_EQ(marketTown->getResourceExchangeEffectiveness(wood, mercury), improved);
	EXPECT_DOUBLE_EQ(marketTown->getResourceExchangeEffectiveness(ore, gems), improved);
	EXPECT_DOUBLE_EQ(marketTown->getResourceExchangeEffectiveness(gems, wood), ordinary);
	EXPECT_DOUBLE_EQ(marketTown->getResourceExchangeEffectiveness(wood, gold), ordinary);
	EXPECT_DOUBLE_EQ(marketTown->getResourceExchangeEffectiveness(gold, gems), ordinary);

	ExternalResourceMarket externalMarket;
	EXPECT_DOUBLE_EQ(externalMarket.getResourceExchangeEffectiveness(wood, gems),
		externalMarket.getMarketExchangeEffectiveness());
	EXPECT_DOUBLE_EQ(externalMarket.getResourceExchangeEffectiveness(wood, gems), 0.35);
}

TEST_F(NewHorizonsResourceBrokerTest, ResourceTraderRequestUsesTheTownDiscountAndServerHonorsIt)
{
	startGame();
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	acquireResourceBroker(handler, firstHero);
	marketTown->setVisitingHero(firstHero);

	const auto wood = GameResID(EGameResID::WOOD);
	const auto crystal = GameResID(EGameResID::CRYSTAL);
	const auto quoted = offer(*marketTown, wood, crystal);
	ASSERT_GT(quoted.first, 0);
	ASSERT_GT(quoted.second, 0);
	ASSERT_LT(quoted.first, expectedResourceOffer(marketTown->getMarketExchangeEffectiveness(), wood, crystal).first);

	ResourceSet reset = -gameState()->getPlayerState(PLAYER)->resources;
	handler.giveResources(PLAYER, reset);
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

	EXPECT_TRUE(NK2AI::ResourceTrader::tradeHelper(NK2AI::ResourceTrader::EXPENDABLE_BULK_RATIO,
		*marketTown, missingNow, income, freeAfterMissingTotal, analyzer, *callback));
	ASSERT_EQ(client.submittedTrades.size(), 1u);
	const auto & submitted = client.submittedTrades.front();
	EXPECT_EQ(submitted.marketId, marketTown->id);
	EXPECT_EQ(submitted.mode, EMarketMode::RESOURCE_RESOURCE);
	ASSERT_EQ(submitted.r1.size(), 1u);
	ASSERT_EQ(submitted.r2.size(), 1u);
	ASSERT_EQ(submitted.val.size(), 1u);
	EXPECT_EQ(submitted.r1.front().as<GameResID>(), wood);
	EXPECT_EQ(submitted.r2.front().as<GameResID>(), crystal);
	EXPECT_EQ(submitted.val.front(), static_cast<ui32>(quoted.first));
	EXPECT_EQ(submitted.heroId, ObjectInstanceID());
	const auto after = gameState()->getPlayerState(PLAYER)->resources;
	EXPECT_EQ(after[EGameResID::WOOD], before[EGameResID::WOOD] - quoted.first);
	EXPECT_EQ(after[EGameResID::CRYSTAL], before[EGameResID::CRYSTAL] + quoted.second);
}
