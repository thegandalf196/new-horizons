/* Part of VCMI engine; GNU General Public License v2.0 or later; see license.txt */
#include "StdInc.h"
#include "NewHorizonsEstatesFixture.h"
#include "../../lib/entities/hero/NewHorizonsLeadership.h"

namespace
{
class NewHorizonsEstatesMercenaryTest : public estatesTest::Fixture {};
using namespace estatesTest;
}

TEST_F(NewHorizonsEstatesMercenaryTest, WeekStartSnapshotsLevelsAcrossActiveHoldersAndAllOwnedMines)
{
    ASSERT_NO_FATAL_FAILURE(startGame());
    ASSERT_NO_FATAL_FAILURE(select(first, "taxCollector", "mercenaryCompany"));
    ASSERT_NO_FATAL_FAILURE(select(second, "taxCollector", "mercenaryCompany"));
    first->level = 3;
    second->level = 7;
    town->setGarrisonedHero(second);
    GameHandlerTestServer server(gameState(), PLAYER);
    CGameHandler handler(server, gameState());
    handler.onNewTurn();
    EXPECT_EQ(rogues(mine), 10);
    EXPECT_EQ(rogues(otherMine), 10);
    EXPECT_EQ(mine->getMercenaryLastWeek(), 0);
    handler.onNewTurn();
    EXPECT_EQ(rogues(mine), 10);
    first->level = 4;
    while(gameState()->day < 7)
        handler.onNewTurn();
    handler.onNewTurn();
    EXPECT_EQ(rogues(mine), 21);
    EXPECT_EQ(rogues(otherMine), 21);
    EXPECT_EQ(mine->getMercenaryLastWeek(), 1);
    EXPECT_EQ(mine->getPendingMercenaries(), 0);
}

TEST_F(NewHorizonsEstatesMercenaryTest, FullSevenGuardsBankGrantAndArmyChangeFulfilsWithoutLoss)
{
    ASSERT_NO_FATAL_FAILURE(startGame());
    const char * guards[] = {"core:pikeman", "core:archer", "core:griffin", "core:swordsman", "core:monk", "core:cavalier", "core:angel"};
    for(int slot = 0; slot < 7; ++slot)
        ASSERT_TRUE(mine->setCreature(SlotID(slot), creature(guards[slot]), slot + 1));
    GameHandlerTestServer server(gameState(), PLAYER);
    CGameHandler handler(server, gameState());
    mine->grantWeeklyMercenaries(handler, 0, 10);
    EXPECT_EQ(mine->stacksCount(), 7);
    EXPECT_EQ(rogues(mine), 0);
    EXPECT_EQ(mine->getPendingMercenaries(), 10);
    for(int slot = 0; slot < 7; ++slot)
        EXPECT_EQ(mine->getStackCount(SlotID(slot)), slot + 1);
    mine->grantWeeklyMercenaries(handler, 0, 10);
    EXPECT_EQ(mine->getPendingMercenaries(), 10);
    mine->grantWeeklyMercenaries(handler, 1, 4);
    EXPECT_EQ(mine->getPendingMercenaries(), 14);
    ASSERT_TRUE(handler.eraseStack(StackLocation(mine->id, SlotID(6))));
    EXPECT_EQ(mine->getPendingMercenaries(), 0);
    EXPECT_EQ(rogues(mine), 14);
    for(int slot = 0; slot < 6; ++slot)
        EXPECT_EQ(mine->getStackCount(SlotID(slot)), slot + 1);
    mine->grantWeeklyMercenaries(handler, 2, 5);
    EXPECT_EQ(rogues(mine), 19);
    EXPECT_EQ(mine->stacksCount(), 7);
}

TEST_F(NewHorizonsEstatesMercenaryTest, PendingCurrentSaveRoundtripAndOwnerDisposalKeepWeeklyGuard)
{
    ASSERT_NO_FATAL_FAILURE(startGame());
    for(int slot = 0; slot < 7; ++slot)
        ASSERT_TRUE(mine->setCreature(SlotID(slot), CreatureID(slot), 1));
    GameHandlerTestServer server(gameState(), PLAYER);
    CGameHandler handler(server, gameState());
    mine->grantWeeklyMercenaries(handler, 2, 7);
    const auto saved = gameState()->saveToMemory();
    CGameState restored;
    restored.preInit(LIBRARY);
    restored.loadFromMemory(saved);
    const auto * copied = dynamic_cast<const CGMine *>(restored.getObjInstance(mine->id));
    ASSERT_NE(copied, nullptr);
    EXPECT_EQ(copied->getPendingMercenaries(), 7);
    EXPECT_EQ(copied->getMercenaryLastWeek(), 2);
    handler.setOwner(mine, PlayerColor::NEUTRAL);
    EXPECT_EQ(mine->getPendingMercenaries(), 0);
    handler.setOwner(mine, PLAYER);
    mine->grantWeeklyMercenaries(handler, 2, 7);
    EXPECT_EQ(mine->getPendingMercenaries(), 0);
    EXPECT_EQ(rogues(mine), 0);
    mine->grantWeeklyMercenaries(handler, 3, 8);
    EXPECT_EQ(mine->getPendingMercenaries(), 8);
}

TEST_F(NewHorizonsEstatesMercenaryTest, CollectionAboveLeadershipCapacityRemainsLegalAndUsesEfficiency)
{
    ASSERT_NO_FATAL_FAILURE(startGame());
    GameHandlerTestServer server(gameState(), PLAYER);
    CGameHandler handler(server, gameState());
    const auto rogue = creature("core:rogue");
    const auto readout = newHorizonsHeroes::leadershipStackReadout(first, rogue, 1);
    ASSERT_TRUE(readout.active);
    ASSERT_GT(readout.capacity, 0);
    EXPECT_FALSE(first->getLeadershipSlotCapacity(rogue));
    const auto oversized = readout.capacity + 10;
    ASSERT_TRUE(first->setCreature(SlotID(1), rogue, oversized));
    mine->grantWeeklyMercenaries(handler, 0, 5);
    ASSERT_EQ(rogues(mine), 5);
    ASSERT_TRUE(handler.moveStack(StackLocation(mine->id, SlotID(0)), StackLocation(first->id, SlotID(1)), 5));
    EXPECT_EQ(rogues(mine), 0);
    EXPECT_EQ(first->getStackCount(SlotID(1)), oversized + 5);
    const auto after = newHorizonsHeroes::leadershipStackReadout(first, rogue, oversized + 5);
    EXPECT_EQ(after.capacity, readout.capacity);
    EXPECT_LT(after.efficiency, 1.0);
    EXPECT_GT(after.efficiency, 0.0);
}

TEST_F(NewHorizonsEstatesMercenaryTest, WrongTypedAndNegativeReplicatedDebtReject)
{
    ASSERT_NO_FATAL_FAILURE(startGame());
    SetObjectProperty packet;
    packet.id = mine->id;
    packet.what = ObjProperty::NEW_HORIZONS_MINE_MERCENARY_PENDING;
    packet.identifier = NumericID(-1);
    EXPECT_THROW(CMemorySerializer::deepCopy<CPackForClient>(packet), std::runtime_error);
    packet.identifier = PLAYER;
    EXPECT_THROW(CMemorySerializer::deepCopy<CPackForClient>(packet), std::runtime_error);
    mine->setProperty(ObjProperty::NEW_HORIZONS_MINE_MERCENARY_WEEK, NumericID(0));
    packet.identifier = NumericID(11);
    const auto copied = CMemorySerializer::deepCopy<CPackForClient>(packet);
    gameState()->apply(*copied);
    EXPECT_EQ(mine->getPendingMercenaries(), 11);
}
