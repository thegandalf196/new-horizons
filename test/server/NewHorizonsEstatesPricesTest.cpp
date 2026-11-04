/* Part of VCMI engine; GNU General Public License v2.0 or later; see license.txt */
#include "StdInc.h"
#include "NewHorizonsEstatesFixture.h"
#include "../../lib/entities/building/CBuilding.h"
#include "../../lib/entities/faction/CTownHandler.h"

namespace
{
class NewHorizonsEstatesPricesTest : public estatesTest::Fixture {};
// A local authored-price fixture, restored even when an assertion returns early.
class ScopedRecruitmentCost
{
    ResourceSet & cost;
    ResourceSet original;
public:
    ScopedRecruitmentCost(CreatureID id, const ResourceSet & price)
        : cost(const_cast<ResourceSet &>(id.toCreature()->getFullRecruitCost())), original(cost)
    {
        cost = price;
    }
    ~ScopedRecruitmentCost() { cost = original; }
};
using namespace estatesTest;
}

TEST(NewHorizonsEstatesPrices, AggregateCeilingChargesNineRareResourcesForTenUnits)
{
    ResourceSet unit;
    unit[EGameResID::GOLD] = 13;
    unit[EGameResID::GEMS] = 1;
    const auto discounted = newHorizonsEstates::recruitmentCost(unit, 10, true);
    EXPECT_EQ(discounted[EGameResID::GOLD], 117);
    EXPECT_EQ(discounted[EGameResID::GEMS], 9);
    EXPECT_EQ(discounted[EGameResID::WOOD], 0);
    EXPECT_EQ(newHorizonsEstates::affordableRecruitment(unit, discounted, 20, true), 10);
    EXPECT_EQ(newHorizonsEstates::recruitmentCost(unit, 1, true)[EGameResID::GOLD], 12);
    EXPECT_EQ(newHorizonsEstates::recruitmentCost(unit, 1, true)[EGameResID::GEMS], 1);
    EXPECT_EQ(newHorizonsEstates::affordableRecruitment(ResourceSet(), ResourceSet(), 200, true), 200);
    EXPECT_THROW(newHorizonsEstates::recruitmentCost(unit, -1, true), std::runtime_error);
}

TEST(NewHorizonsEstatesPrices, ArchitectDiscountsOnlyGoldWoodAndOreWithCeiling)
{
    ResourceSet base;
    for(const auto resource : {EGameResID::GOLD, EGameResID::WOOD, EGameResID::ORE,
        EGameResID::MERCURY, EGameResID::SULFUR, EGameResID::CRYSTAL, EGameResID::GEMS})
        base[resource] = 7;
    auto actual = newHorizonsEstates::buildingCost(base, true);
    for(const auto resource : {EGameResID::GOLD, EGameResID::WOOD, EGameResID::ORE})
        EXPECT_EQ(actual[resource], 6);
    for(const auto resource : {EGameResID::MERCURY, EGameResID::SULFUR, EGameResID::CRYSTAL, EGameResID::GEMS})
        EXPECT_EQ(actual[resource], 7);
    EXPECT_EQ(newHorizonsEstates::buildingCost(base, false), base);
    EXPECT_TRUE(newHorizonsEstates::buildingCost(ResourceSet(), true).empty());
}

TEST_F(NewHorizonsEstatesPricesTest, ConscriptionQuotesDebitAndAffordabilityUseActualRecipient)
{
    ASSERT_NO_FATAL_FAILURE(startGame());
    ASSERT_NO_FATAL_FAILURE(select(first, "taxCollector", "conscription"));
    town->setVisitingHero(first);
    const auto recruit = creature("core:pikeman");
    ResourceSet authoredPrice;
    authoredPrice[EGameResID::GOLD] = 13;
    authoredPrice[EGameResID::GEMS] = 1;
    ScopedRecruitmentCost costScope(recruit, authoredPrice);
    town->creatures = {{100, {recruit}}};
    GameHandlerTestServer server(gameState(), PLAYER);
    CGameHandler handler(server, gameState());
    const auto quote = town->getTotalRecruitmentCost(recruit, 10, first);
    EXPECT_EQ(quote[EGameResID::GEMS], 9);
    EXPECT_EQ(quote[EGameResID::GOLD], 117);
    ASSERT_EQ(newHorizonsEstates::recruitmentBuyer(town->getUpperArmy()), nullptr);
    EXPECT_EQ(town->getTotalRecruitmentCost(recruit, 10), town->getRecruitmentCost(recruit) * 10);
    EXPECT_NE(town->getTotalRecruitmentCost(recruit, 10, second), quote);
    setResources(handler, quote);
    EXPECT_EQ(town->getRecruitmentAmount(recruit, quote, 100, first), 10);
    const auto originalArmy = first->getStackCount(SlotID(0));
    ASSERT_TRUE(handler.recruitCreatures(town->id, first->id, recruit, 10, 0, PLAYER));
    EXPECT_TRUE(gameState()->getPlayerState(PLAYER)->resources.empty());
    EXPECT_EQ(first->getStackCount(SlotID(0)), originalArmy + 10);
    EXPECT_EQ(town->creatures[0].first, 90u);
    const auto stock = town->creatures;
    const auto count = first->getStackCount(SlotID(0));
    EXPECT_FALSE(handler.recruitCreatures(town->id, first->id, recruit, 1, 0, PLAYER));
    EXPECT_EQ(town->creatures, stock);
    EXPECT_EQ(first->getStackCount(SlotID(0)), count);
    EXPECT_TRUE(gameState()->getPlayerState(PLAYER)->resources.empty());
    town->setGarrisonedHero(second);
    EXPECT_EQ(newHorizonsEstates::recruitmentBuyer(town->getUpperArmy()), second);
}

TEST_F(NewHorizonsEstatesPricesTest, RemoteHolderCannotDiscountAnotherHeroesPurchaseAndExternalTierOneStaysFree)
{
    ASSERT_NO_FATAL_FAILURE(startGame());
    ASSERT_NO_FATAL_FAILURE(select(first, "taxCollector", "conscription"));
    town->setVisitingHero(second);
    const auto recruit = creature("core:pikeman");
    town->creatures = {{100, {recruit}}};
    GameHandlerTestServer server(gameState(), PLAYER);
    CGameHandler handler(server, gameState());
    const auto ordinary = town->getTotalRecruitmentCost(recruit, 10, second);
    setResources(handler, town->getTotalRecruitmentCost(recruit, 10, first));
    const auto before = gameState()->getPlayerState(PLAYER)->resources;
    EXPECT_FALSE(handler.recruitCreatures(town->id, second->id, recruit, 10, 0, PLAYER));
    EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources, before);
    setResources(handler, ordinary);
    ASSERT_TRUE(handler.recruitCreatures(town->id, second->id, recruit, 10, 0, PLAYER));
    EXPECT_TRUE(gameState()->getPlayerState(PLAYER)->resources.empty());
    EXPECT_TRUE(dwelling->getTotalRecruitmentCost(recruit, 10, first).empty());
}

TEST_F(NewHorizonsEstatesPricesTest, ArchitectResidenceControlsActualPaymentAffordabilityAndDeparture)
{
    ASSERT_NO_FATAL_FAILURE(startGame());
    ASSERT_NO_FATAL_FAILURE(select(first, "taxCollector", "architect"));
    ASSERT_NO_FATAL_FAILURE(select(second, "taxCollector", "architect"));
    GameHandlerTestServer server(gameState(), PLAYER);
    CGameHandler handler(server, gameState());
    const auto building = BuildingID(BuildingID::TOWN_HALL);
    const auto base = town->getBuildingCost(building);
    town->setVisitingHero(first);
    const auto quote = town->getBuildingCost(building);
    EXPECT_EQ(quote, newHorizonsEstates::buildingCost(base, true));
    town->setGarrisonedHero(second);
    EXPECT_EQ(town->getBuildingCost(building), quote);
    town->setVisitingHero(nullptr);
    EXPECT_EQ(town->getBuildingCost(building), quote);
    town->setGarrisonedHero(nullptr);
    EXPECT_EQ(town->getBuildingCost(building), base);
    town->setVisitingHero(first);
    setResources(handler, quote);
    EXPECT_EQ(handler.gameState().canBuildStructure(town, building), EBuildingState::ALLOWED);
    ASSERT_TRUE(handler.buildStructure(town->id, building));
    EXPECT_TRUE(town->hasBuilt(building));
    EXPECT_TRUE(gameState()->getPlayerState(PLAYER)->resources.empty());
}

TEST_F(NewHorizonsEstatesPricesTest, ArchitectLeavesRareComponentsAndRejectsInsufficientPaymentAtomically)
{
    ASSERT_NO_FATAL_FAILURE(startGame());
    ASSERT_NO_FATAL_FAILURE(select(first, "taxCollector", "architect"));
    town->setVisitingHero(first);
    GameHandlerTestServer server(gameState(), PLAYER);
    CGameHandler handler(server, gameState());
    if(!town->hasBuilt(BuildingID::MAGES_GUILD_1))
        ASSERT_TRUE(handler.buildStructure(town->id, BuildingID::MAGES_GUILD_1, true));
    const auto building = BuildingID(BuildingID::MAGES_GUILD_2);
    const auto raw = town->getTown()->buildings.at(building)->resources;
    const auto quote = town->getBuildingCost(building);
    for(const auto rare : {EGameResID::MERCURY, EGameResID::SULFUR, EGameResID::CRYSTAL, EGameResID::GEMS})
        EXPECT_EQ(quote[rare], raw[rare]);
    ASSERT_GT(quote[EGameResID::GEMS], 0);
    auto insufficient = quote;
    --insufficient[EGameResID::GEMS];
    setResources(handler, insufficient);
    EXPECT_EQ(handler.gameState().canBuildStructure(town, building), EBuildingState::NO_RESOURCES);
    EXPECT_FALSE(handler.buildStructure(town->id, building));
    EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources, insufficient);
    EXPECT_FALSE(town->hasBuilt(building));
    setResources(handler, quote);
    ASSERT_TRUE(handler.buildStructure(town->id, building));
    EXPECT_TRUE(gameState()->getPlayerState(PLAYER)->resources.empty());
}
