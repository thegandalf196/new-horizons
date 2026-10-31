/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../server/CGameHandler.h"
#include "../../../lib/battle/BodyHealthLoss.h"
#include <limits>
#include <tuple>
#include <stdexcept>
namespace
{
TEST(BodyHealthLossTest, StandardReceiptBoundsDoNotOverflowOrCountTemporaryPool)
{
	using battle::bodyHealthLoss;
	EXPECT_EQ(bodyHealthLoss(0, 0), 0);
	EXPECT_EQ(bodyHealthLoss(-1, 0), 0);
	EXPECT_EQ(bodyHealthLoss(std::numeric_limits<int64_t>::min(), 0), 0);
	EXPECT_EQ(bodyHealthLoss(1, 1), 0);
	EXPECT_EQ(bodyHealthLoss(10, 3), 7);
	EXPECT_EQ(bodyHealthLoss(std::numeric_limits<int64_t>::max(), 0), std::numeric_limits<int64_t>::max());
	EXPECT_EQ(bodyHealthLoss(std::numeric_limits<int64_t>::max(), std::numeric_limits<int64_t>::max()), 0);
	EXPECT_THROW(bodyHealthLoss(1, -1), std::invalid_argument);
}
class BodyHealthLossNativeTest : public BattleTestFixture, public ::testing::WithParamInterface<std::tuple<bool, bool>> {};
TEST_P(BodyHealthLossNativeTest, PreparedDamageMatchesActualBodyLedgerWithGuardianTemporaryPoolAndForm)
{
	startGame();
	startBattle();
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 2);
	ASSERT_NE(target, nullptr);
	beginCombat();
	auto state = target->acquireState();
	const bool form = std::get<0>(GetParam());
	const bool physical = std::get<1>(GetParam());
	const auto sourceHealth = state->health.getCreatureHealthAvailable();
	if(form) state->beginBattleForm(creatureByName("core:peasant"), 2);
	ASSERT_EQ(state->hasBattleForm(), form);
	state->health.addTemporaryHitPoints(6);
	state->guardianSpiritHitPoints = 10;
	state->guardianSpiritRoundsRemaining = 1;
	const auto bodyBefore = state->health.getCreatureHealthAvailable();
	const auto temporaryBefore = state->health.getTemporaryHitPoints();
	BattleStackAttacked hit;
	hit.damageAmount = 20;
	CStack::prepareAttacked(hit, gameHandler->getRandomGenerator(), state, false, false,
		physical ? battle::DamageProvenance::PHYSICAL_CREATURE : battle::DamageProvenance::SPELL);
	ASSERT_FALSE(hit.killed());
	ASSERT_FALSE(hit.willRebirth());
	const auto bodyLoss = battle::bodyHealthLoss(hit.damageAmount, temporaryBefore);
	EXPECT_EQ(hit.damageAmount, physical ? 10 : 20);
	EXPECT_EQ(bodyLoss, physical ? 4 : 14);
	EXPECT_EQ(state->health.getTemporaryHitPoints(), 0);
	EXPECT_EQ(state->guardianSpiritHitPoints, physical ? 0 : 10);
	EXPECT_EQ(bodyBefore - state->health.getCreatureHealthAvailable(), bodyLoss);
	if(form)
	{
		state->endBattleForm();
		EXPECT_EQ(sourceHealth - state->health.getCreatureHealthAvailable(), bodyLoss);
	}
	EXPECT_EQ(target->health.getCreatureHealthAvailable(), sourceHealth);
	EXPECT_EQ(target->health.getTemporaryHitPoints(), 0);
	EXPECT_FALSE(target->hasBattleForm());
}
INSTANTIATE_TEST_SUITE_P(StandardAndDualFormPhysicalAndMagical, BodyHealthLossNativeTest,
	::testing::Combine(::testing::Bool(), ::testing::Bool()));
class BodyHealthLossRebirthTest : public BattleTestFixture {};
TEST_F(BodyHealthLossRebirthTest, RecreatedBodyDoesNotEraseTheActualOldBodyLossFact)
{
	startGame();
	startBattle();
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 2);
	ASSERT_NE(target, nullptr);
	beginCombat();
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::REBIRTH, BonusSource::OTHER, 100, BonusSourceID()));
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::CASTS, BonusSource::OTHER, 1, BonusSourceID()));
	auto state = target->acquireState();
	const auto bodyBefore = state->health.getCreatureHealthAvailable();
	state->health.addTemporaryHitPoints(6);
	const auto temporaryBefore = state->health.getTemporaryHitPoints();
	BattleStackAttacked hit;
	hit.damageAmount = 1000;
	CStack::prepareAttacked(hit, gameHandler->getRandomGenerator(), state);
	ASSERT_TRUE(hit.killed());
	ASSERT_TRUE(hit.willRebirth());
	EXPECT_EQ(state->health.getCreatureHealthAvailable(), bodyBefore);
	EXPECT_EQ(battle::bodyHealthLoss(hit.damageAmount, temporaryBefore), bodyBefore);
}
}
