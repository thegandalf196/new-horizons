/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../mock/mock_battle_Unit.h"
#include "../../../lib/battle/NewHorizonsSoulHarvest.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/bonuses/BonusList.h"
namespace
{
using namespace ::testing;
class SoulHarvestOptionalContentTest : public BattleTestFixture {};
TEST_F(SoulHarvestOptionalContentTest, CorePhysicalCreatureNeverNeedsOptionalGhostIdentifier)
{
	// Run unchanged in separate NH-on and NH-off processes. Never remove a
	// registry entry or mutate LIBRARY to simulate missing optional content.
	startGame(); startBattle();
	auto * actor = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 1);
	auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 1);
	ASSERT_NE(actor, nullptr); ASSERT_NE(victim, nullptr);
	ASSERT_TRUE(actor->alive()); ASSERT_TRUE(victim->alive());
	EXPECT_FALSE(newHorizonsSoulHarvest::qualifies(*battle(), *actor, *victim, true));
	EXPECT_FALSE(newHorizonsSoulHarvest::qualifies(*battle(), *actor, *victim, false));
}
TEST(SoulHarvestOptionalIdentityTest, MissingMetadataRejectsBeforeIdentityOrBattleQueries)
{
	NiceMock<UnitMock> actor, victim;
	const auto empty = std::make_shared<BonusList>();
	ON_CALL(actor, getAllBonuses(_, _)).WillByDefault(Return(empty));
	ON_CALL(victim, getAllBonuses(_, _)).WillByDefault(Return(empty));
	ON_CALL(actor, alive()).WillByDefault(Return(true));
	ON_CALL(actor, unitSlot()).WillByDefault(Return(SlotID(0)));
	ON_CALL(victim, unitSlot()).WillByDefault(Return(SlotID(0)));
	ON_CALL(victim, alive()).WillByDefault(Return(true));
	EXPECT_CALL(actor, unitType()).WillOnce(Return(nullptr));
	EXPECT_CALL(actor, creatureId()).Times(0);
	const CPlayerBattleCallback absentBattle(nullptr, PlayerColor(0));
	EXPECT_FALSE(newHorizonsSoulHarvest::qualifies(absentBattle, actor, victim, true));
}
TEST(SoulHarvestOptionalIdentityTest, NonphysicalRejectionKeepsMetadataQueryDeferred)
{
	NiceMock<UnitMock> actor, victim;
	EXPECT_CALL(actor, unitType()).Times(0);
	EXPECT_CALL(actor, creatureId()).Times(0);
	EXPECT_CALL(actor, alive()).Times(0);
	const CPlayerBattleCallback absentBattle(nullptr, PlayerColor(0));
	EXPECT_FALSE(newHorizonsSoulHarvest::qualifies(absentBattle, actor, victim, false));
}
}
