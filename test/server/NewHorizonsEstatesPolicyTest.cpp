/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "RevisedEstatesFixture.h"
#include "../../lib/mapObjects/NewHorizonsEstatesPolicy.h"
#include "../../lib/mapObjects/CGResource.h"
#include "../../lib/rewardable/Reward.h"
#include <limits>

namespace
{
using namespace estatesTest;

using NewHorizonsRevisedEstatesTest = estatesTest::RevisedFixture;
}

TEST(NewHorizonsEstatesPolicy, MissingMarkerIsLegacyAndUnsupportedMarkersFailClosed)
{
	JsonNode rules;
	rules.Struct();
	EXPECT_FALSE(newHorizonsEstates::usesRevisedPolicy(rules));
	EXPECT_NO_THROW(newHorizonsEstates::validateRevisedPolicy(rules));
	EXPECT_NO_THROW(newHorizonsEstates::validateRevisedSerialization(rules, false));
	rules["estatesPolicyVersion"].Integer() = 1;
	EXPECT_THROW(newHorizonsEstates::validateRevisedSerialization(rules, false), std::runtime_error);
	EXPECT_NO_THROW(newHorizonsEstates::validateRevisedSerialization(rules, true));
	for(const int version : {0, 2})
	{
		rules["estatesPolicyVersion"].Integer() = version;
		EXPECT_THROW(newHorizonsEstates::validateRevisedPolicy(rules), std::runtime_error);
	}
	rules["estatesPolicyVersion"].Bool() = true;
	EXPECT_FALSE(newHorizonsEstates::usesRevisedPolicy(rules));
	EXPECT_THROW(newHorizonsEstates::validateRevisedPolicy(rules), std::runtime_error);
}

TEST_F(NewHorizonsRevisedEstatesTest, TaxCollectorQuotesEveryPositiveResourceWithoutChangingCostsOrExperience)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(select(first, "taxCollector"));
	Rewardable::Reward reward;
	reward.heroExperience = 500;
	for(size_t resource = 0; resource < reward.resources.size(); ++resource)
		reward.resources[resource] = static_cast<TResource>(resource);
	const auto actual = reward.visitResources(first, Obj::TREASURE_CHEST);
	for(size_t resource = 0; resource < reward.resources.size(); ++resource)
		EXPECT_EQ(actual[resource], resource + resource / 2);
	EXPECT_EQ(reward.heroExperience, 500);
	EXPECT_EQ(reward.visitResources(first, std::nullopt), reward.resources);
	reward.resources[EGameResID::GOLD] = -75;
	EXPECT_EQ(reward.visitResources(first, Obj::TREASURE_CHEST)[EGameResID::GOLD], -75);
	reward.resources[EGameResID::GOLD] = 1000;
	Rewardable::Reward goldChoice;
	goldChoice.resources[EGameResID::GOLD] = 1000;
	const auto component = goldChoice.getDisplayedComponent(first, Obj::TREASURE_CHEST);
	EXPECT_EQ(reward.visitResources(first, Obj::TREASURE_CHEST)[EGameResID::GOLD], 1500);
	EXPECT_EQ(component.type, ComponentType::RESOURCE);
	EXPECT_EQ(component.value, 1500);
}

TEST_F(NewHorizonsRevisedEstatesTest, TaxCollectorOverflowRejectsWithoutMutatingTheBaseReward)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(select(first, "taxCollector"));
	ResourceSet base;
	base[EGameResID::GOLD] = std::numeric_limits<TResource>::max();
	EXPECT_THROW(newHorizonsEstates::visitReward(base, first), std::overflow_error);
	EXPECT_EQ(base[EGameResID::GOLD], std::numeric_limits<TResource>::max());
	EXPECT_EQ(newHorizonsEstates::visitReward(base, second), base);
}

TEST_F(NewHorizonsRevisedEstatesTest, SupervisorAddsOneOnlyToWoodAndOreAndNeverStacks)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	auto * owner = gameState()->getPlayerState(PLAYER);
	EXPECT_EQ(newHorizonsEstates::mineBaseProduction(2, GameResID::WOOD, owner), 2);
	ASSERT_NO_FATAL_FAILURE(select(first, "supervisor"));
	ASSERT_NO_FATAL_FAILURE(select(second, "supervisor"));
	EXPECT_EQ(newHorizonsEstates::mineBaseProduction(2, GameResID::WOOD, owner), 3);
	EXPECT_EQ(newHorizonsEstates::mineBaseProduction(7, GameResID::ORE, owner), 8);
	EXPECT_EQ(newHorizonsEstates::mineBaseProduction(1, GameResID::MERCURY, owner), 1);
	EXPECT_EQ(newHorizonsEstates::mineBaseProduction(2, GameResID::WOOD, nullptr), 2);
	EXPECT_THROW(newHorizonsEstates::mineBaseProduction(std::numeric_limits<uint32_t>::max(),
		GameResID::ORE, owner), std::overflow_error);
	EXPECT_THROW(newHorizonsEstates::mineBaseProduction(std::numeric_limits<TResource>::max(),
		GameResID::WOOD, owner), std::overflow_error);
}

TEST_F(NewHorizonsRevisedEstatesTest, MarketCapitolAndWeeklyAlchemistAreKingdomWideAndNonstacking)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	auto * owner = gameState()->getPlayerState(PLAYER);
	EXPECT_EQ(newHorizonsEstates::marketplaceBonus(*owner), 0);
	EXPECT_EQ(newHorizonsEstates::capitolLimit(*owner), 1);
	ASSERT_NO_FATAL_FAILURE(select(first, "merchantPrince", "architect", "alchemist"));
	ASSERT_NO_FATAL_FAILURE(select(second, "merchantPrince", "architect", "alchemist"));
	EXPECT_EQ(newHorizonsEstates::marketplaceBonus(*owner), 2);
	const auto weekly = newHorizonsEstates::weeklyAlchemistIncome(*owner);
	for(const auto resource : {EGameResID::MERCURY, EGameResID::SULFUR, EGameResID::CRYSTAL, EGameResID::GEMS})
		EXPECT_EQ(weekly[resource], 3);
	EXPECT_EQ(weekly[EGameResID::WOOD], 0);
	EXPECT_EQ(weekly[EGameResID::ORE], 0);
	ASSERT_NO_FATAL_FAILURE(select(first, "supervisor", "architect", "decentralization"));
	ASSERT_NO_FATAL_FAILURE(select(second, "supervisor", "architect", "decentralization"));
	EXPECT_EQ(newHorizonsEstates::capitolLimit(*owner), 2);
	EXPECT_TRUE(newHorizonsEstates::weeklyAlchemistIncome(*owner).empty());
}

TEST_F(NewHorizonsRevisedEstatesTest, RevisedAlchemistAndTaxCollectorDoNotRetainTheirHistoricalDailyGrants)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(select(first, "supervisor", "architect", "decentralization"));
	const auto before = first->dailyIncome();
	ASSERT_NO_FATAL_FAILURE(select(first, "taxCollector", "architect", "alchemist"));
	const auto after = first->dailyIncome();
	EXPECT_EQ(after, before);
}

TEST_F(NewHorizonsRevisedEstatesTest, ResourcePickupCommitsTheSameHeroAwareQuote)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(select(first, "taxCollector"));
	auto * pickup = expectAt<CGResource>({24, 24, 0});
	ASSERT_NE(pickup, nullptr);
	const auto quote = pickup->getVisitResources(first);
	EXPECT_EQ(quote[EGameResID::GOLD], 1500);
	EXPECT_EQ(pickup->getVisitResources(second)[EGameResID::GOLD], 1000);
	const auto before = gameState()->getPlayerState(PLAYER)->resources;
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	pickup->onHeroVisit(handler, first);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources - before, quote);
}

TEST_F(NewHorizonsRevisedEstatesTest, RevisedCatalogueCannotRelabelLegacyTiersOrRetiredIds)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	const auto rules = first->getPerkState().getRules();
	EXPECT_NO_THROW(newHorizonsEstates::validateRevisedPolicy(rules));
	for(const auto * invalidId : {"landSurveyor", "prosperity", "banking", "unknown"})
	{
		auto changed = rules;
		changed["skills"][ESTATES]["perks"].Vector().front()["id"].String() = ESTATES + "." + invalidId;
		EXPECT_THROW(newHorizonsEstates::validateRevisedPolicy(changed), std::runtime_error);
	}
	auto changed = rules;
	for(auto & row : changed["skills"][ESTATES]["perks"].Vector())
		if(row["id"].String() == ESTATES + ".mercenaryCompany")
			row["requires"].String() = "advanced";
	EXPECT_THROW(newHorizonsEstates::validateRevisedPolicy(changed), std::runtime_error);
}

TEST_F(NewHorizonsRevisedEstatesTest, SupervisorUsesActualMineIncomeAndPreviewThenStopsAfterHolderLoss)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_EQ(mine->producedResource, GameResID::WOOD);
	const auto before = mine->dailyIncome()[GameResID::WOOD];
	ASSERT_NO_FATAL_FAILURE(select(first, "supervisor"));
	EXPECT_EQ(mine->dailyIncome()[GameResID::WOOD], before + 1);
	EXPECT_EQ(mine->getProducedQuantity(), mine->dailyIncome()[GameResID::WOOD]);
	const_cast<newHorizonsHeroes::PerkState &>(first->getPerkState()).replaceSelected({});
	EXPECT_EQ(mine->dailyIncome()[GameResID::WOOD], before);
}

TEST_F(NewHorizonsRevisedEstatesTest, AlchemistUsesNormalWeeklyProductionWithoutAnInitialOrOrdinaryDayGrant)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(select(first, "taxCollector", "architect", "alchemist"));
	ASSERT_NO_FATAL_FAILURE(select(second, "taxCollector", "architect", "alchemist"));
	auto * owner = gameState()->getPlayerState(PLAYER);
	const auto before = owner->resources;
	GameHandlerTestServer server(gameState(), PLAYER);
	CGameHandler handler(server, gameState());
	gameState()->day = 0;
	handler.onNewTurn();
	for(const auto resource : {EGameResID::MERCURY, EGameResID::SULFUR, EGameResID::CRYSTAL, EGameResID::GEMS})
		EXPECT_EQ(owner->resources[resource], before[resource]);
	gameState()->day = 7;
	handler.onNewTurn();
	for(const auto resource : {EGameResID::MERCURY, EGameResID::SULFUR, EGameResID::CRYSTAL, EGameResID::GEMS})
		EXPECT_EQ(owner->resources[resource], before[resource] + 3);
	const auto afterWeekly = owner->resources;
	handler.onNewTurn();
	for(const auto resource : {EGameResID::MERCURY, EGameResID::SULFUR, EGameResID::CRYSTAL, EGameResID::GEMS})
		EXPECT_EQ(owner->resources[resource], afterWeekly[resource]);
}
