/* Part of VCMI engine; GNU General Public License v2.0 or later; see license.txt */
#include "StdInc.h"
#include "NewHorizonsEstatesFixture.h"

namespace
{
class NewHorizonsEstatesIncomeTest : public estatesTest::Fixture {};
using namespace estatesTest;
}

TEST_F(NewHorizonsEstatesIncomeTest, ProsperityAndAlchemistProduceOnlyTheirAuthoredResourcesEveryDay)
{
    ASSERT_NO_FATAL_FAILURE(startGame());
    const auto before = first->dailyIncome();
    ASSERT_NO_FATAL_FAILURE(select(first, "prosperity", "stewardship", "alchemist"));
    auto delta = first->dailyIncome() - before;
    EXPECT_EQ(delta[EGameResID::WOOD], 1);
    EXPECT_EQ(delta[EGameResID::ORE], 1);
    for(const auto rare : {EGameResID::MERCURY, EGameResID::SULFUR, EGameResID::CRYSTAL, EGameResID::GEMS})
        EXPECT_EQ(delta[rare], 1);
    EXPECT_EQ(delta[EGameResID::GOLD], 500);
    town->setGarrisonedHero(first);
    EXPECT_EQ(first->dailyIncome() - before, delta);
    GameHandlerTestServer server(gameState(), PLAYER);
    CGameHandler handler(server, gameState());
    handler.onNewTurn();
    ResourceSet expected;
    for(const auto * object : gameState()->getPlayerState(PLAYER)->getOwnedObjects())
        expected += object->asOwnable()->dailyIncome();
    const auto treasury = gameState()->getPlayerState(PLAYER)->resources;
    handler.onNewTurn();
    EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources, treasury + expected);
}

TEST_F(NewHorizonsEstatesIncomeTest, ResourceProducersStackAcrossHoldersAndPersistCurrentSave)
{
    ASSERT_NO_FATAL_FAILURE(startGame());
    ASSERT_NO_FATAL_FAILURE(select(first, "prosperity", "stewardship", "alchemist"));
    ASSERT_NO_FATAL_FAILURE(select(second, "prosperity", "stewardship", "alchemist"));
    town->setGarrisonedHero(second);
    const auto firstQuote = first->dailyIncome();
    const auto secondQuote = second->dailyIncome();
    const auto saved = gameState()->saveToMemory();
    auto restored = std::make_shared<CGameState>();
    restored->preInit(LIBRARY);
    restored->loadFromMemory(saved);
    EXPECT_EQ(restored->getHero(first->id)->dailyIncome(), firstQuote);
    EXPECT_EQ(restored->getHero(second->id)->dailyIncome(), secondQuote);
    GameHandlerTestServer server(restored, PLAYER);
    CGameHandler handler(server, restored);
    handler.onNewTurn();
    const auto treasury = restored->getPlayerState(PLAYER)->resources;
    handler.onNewTurn();
    for(const auto rare : {EGameResID::MERCURY, EGameResID::SULFUR, EGameResID::CRYSTAL, EGameResID::GEMS})
        EXPECT_EQ(restored->getPlayerState(PLAYER)->resources[rare] - treasury[rare], 2);
}

TEST_F(NewHorizonsEstatesIncomeTest, PlannedResourcePerkCannotBeSelectedAndOrdinaryGenerationUsesHandicap)
{
    planned = "new-horizons:estates.prosperity";
    incomePercent = 50;
    ASSERT_NO_FATAL_FAILURE(startGame());
    const auto skill = SecondarySkill(SecondarySkill::decode(ESTATES));
    first->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
    EXPECT_THROW(first->applyPerkSelection({ESTATES, planned}), std::runtime_error);
    EXPECT_EQ(first->dailyIncome()[EGameResID::WOOD], 0);
    EXPECT_EQ(first->dailyIncome()[EGameResID::ORE], 0);
    EXPECT_EQ(first->dailyIncome()[EGameResID::GOLD], 63);
}

TEST_F(NewHorizonsEstatesIncomeTest, ResourceCapIsPreservedAndSaveLoadDoesNotRepeatInitialGrant)
{
    ASSERT_NO_FATAL_FAILURE(startGame());
    ASSERT_NO_FATAL_FAILURE(select(first));
    GameHandlerTestServer server(gameState(), PLAYER);
    CGameHandler handler(server, gameState());
    handler.onNewTurn();
    gameState()->getPlayerState(PLAYER)->resources[EGameResID::WOOD] = GameConstants::PLAYER_RESOURCES_CAP;
    const auto saved = gameState()->saveToMemory();
    auto restored = std::make_shared<CGameState>();
    restored->preInit(LIBRARY);
    restored->loadFromMemory(saved);
    GameHandlerTestServer restoredServer(restored, PLAYER);
    CGameHandler restoredHandler(restoredServer, restored);
    const auto oreBefore = restored->getPlayerState(PLAYER)->resources[EGameResID::ORE];
    restoredHandler.onNewTurn();
    EXPECT_EQ(restored->getPlayerState(PLAYER)->resources[EGameResID::WOOD], GameConstants::PLAYER_RESOURCES_CAP);
    EXPECT_EQ(restored->getPlayerState(PLAYER)->resources[EGameResID::ORE], oreBefore + 1);
}
