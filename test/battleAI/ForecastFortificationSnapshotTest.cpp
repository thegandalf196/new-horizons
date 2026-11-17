/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
namespace
{
class ForecastFortificationSnapshotTest : public ProjectedTargetHitFixture {};
}
TEST_F(ForecastFortificationSnapshotTest, ChildKeepsItsParentsVisibleObstacleSnapshot)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto obstacle = std::make_shared<CObstacleInstance>();
	obstacle->ID = 0;
	obstacle->uniqueID = 231;
	obstacle->pos = BattleHex(4, 0);
	obstacle->obstacleType = CObstacleInstance::USUAL;
	battle()->obstacles.push_back(obstacle);
	resetChild();
	ASSERT_EQ(child->getAllObstacles().size(), 1u);
	auto nested = std::make_shared<HypotheticBattle>(environment.get(), child);
	ASSERT_EQ(nested->getAllObstacles().size(), 1u);
	EXPECT_EQ(nested->getAllObstacles().front()->uniqueID, obstacle->uniqueID);
	EXPECT_EQ(nested->getAllObstacles().front()->pos, obstacle->pos);
	EXPECT_EQ(child->getAllObstacles().size(), 1u);
	EXPECT_EQ(battle()->obstacles.size(), 1u);
}

TEST_F(ForecastFortificationSnapshotTest, ChildKeepsItsParentsProjectedWallHpAndOpenGate)
{
	ASSERT_NO_FATAL_FAILURE(startGame(true));
	auto * town = findFirst<CGTownInstance>();
	ASSERT_NE(town, nullptr);
	town->addBuilding(BuildingID::FORT);
	town->addBuilding(BuildingID::CITADEL);
	town->addBuilding(BuildingID::CASTLE);
	ASSERT_NO_FATAL_FAILURE(startBattle(town));
	environment = std::make_unique<projectedHitTest::Environment>(gameState());
	live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	resetChild();
	const auto original = child->getWallStructuralHP(EWallPart::GATE);
	ASSERT_GT(original, 1);
	child->setWallStructuralHP(EWallPart::GATE, original - 1);
	child->openGateForReturn();
	auto nested = std::make_shared<HypotheticBattle>(environment.get(), child);
	EXPECT_EQ(nested->getWallStructuralHP(EWallPart::GATE), original - 1);
	EXPECT_EQ(nested->getWallState(EWallPart::GATE), child->getWallState(EWallPart::GATE));
	EXPECT_EQ(nested->getGateState(), EGateState::OPENED);
	EXPECT_EQ(child->getWallStructuralHP(EWallPart::GATE), original - 1);
}
