/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "../../server/CGameHandler.h"
#include "../../lib/gameState/CGameState.h"
#include "../../AI/BattleAI/FocusMagicProjection.h"
#include "../../lib/battle/NewHorizonsFocusMagic.h"
namespace heroQueueTest
{
class FocusMagicProjectionTest : public HeroQueueSelectorTest
{
protected:
	void prepare()
	{
		useRevisedOrders = true;
		ASSERT_NO_FATAL_FAILURE(world());
		ASSERT_NO_FATAL_FAILURE(arena());
	}
	void branches()
	{
		environment = std::make_shared<QueueEnvironment>(gameState());
		live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	}
};
TEST_F(FocusMagicProjectionTest, ThreeProjectedRangedStrikesConsumeHardChargesOnlyInChild)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = SpellID(SpellID::decode(newHorizonsFocusMagic::SPELL));
	ASSERT_TRUE(spell.hasValue());
	const auto bonuses = newHorizonsFocusMagic::enchantment(*actor, spell);
	effect(actor, bonuses.front());
	effect(actor, bonuses.back());
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	auto unit = child->getForUpdate(actor->unitId());
	ASSERT_EQ(newHorizonsFocusMagic::charges(*unit), 3);
	focusMagicAI::afterAttack(*unit, false, false);
	EXPECT_EQ(newHorizonsFocusMagic::charges(*unit), 3);
	for(int remaining = 2; remaining >= 0; --remaining)
	{
		focusMagicAI::afterAttack(*unit, true, false);
		EXPECT_EQ(newHorizonsFocusMagic::charges(*unit), remaining);
	}
	EXPECT_FALSE(newHorizonsFocusMagic::active(*unit));
	EXPECT_FALSE(unit->isShooter());
	EXPECT_EQ(newHorizonsFocusMagic::charges(*parent->getForUpdate(actor->unitId())), 3);
	EXPECT_EQ(observation(), outside);
}
}

namespace heroQueueTest
{
TEST_F(FocusMagicProjectionTest, DirectVisibleCallbackWithActualEnvironmentMatchesWrappedWorld)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = SpellID(SpellID::decode(newHorizonsFocusMagic::SPELL));
	ASSERT_TRUE(spell.hasValue());
	for(const auto & bonus : newHorizonsFocusMagic::enchantment(*active, spell)) effect(active, bonus);
	ASSERT_TRUE(newHorizonsFocusMagic::converted(*active));
	ASSERT_EQ(newHorizonsFocusMagic::charges(*active), 3);
	ASSERT_EQ(active->shots.available(), 3);
	ASSERT_TRUE(active->canShoot());
	environment = std::make_shared<QueueEnvironment>(gameState());
	live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	ASSERT_TRUE(live->battleCanShoot(active, enemy->getPosition()));
	ASSERT_EQ(AttackPossibility::getAttackCount(*active, true, *live), 1);
	for(const auto * unit : battle()->battleGetAllStacks(false)) (void)unit->counterAttacks.total();
	const auto before = gameState()->saveToMemory();
	const auto random = wire(*gameHandler->randomizer);
	DamageCache directCache;
	const auto direct = AttackPossibility::evaluate(BattleAttackInfo(active, enemy, 0, true),
		active->getPosition(), directCache, live, false, std::nullopt, nullptr, false,
		BattleHex::INVALID, environment.get());
	ASSERT_NE(direct.effectPreview, nullptr);
	ASSERT_NE(direct.queryWorld, nullptr);
	ASSERT_NE(direct.attackerState, nullptr);
	// A matching pair of empty forecasts cannot establish charge consumption.
	// Require one accepted damaging primary shot before comparing both paths.
	ASSERT_EQ(direct.fortuneStrikes.size(), 1u);
	EXPECT_TRUE(direct.fortuneStrikes.front().shooting);
	const auto & hits = direct.fortuneStrikes.front().resolvedHits;
	ASSERT_EQ(hits.size(), 1u);
	EXPECT_EQ(hits.front().first, enemy->unitId());
	EXPECT_GT(hits.front().second, 0);
	EXPECT_EQ(newHorizonsFocusMagic::charges(*direct.attackerState), 2);
	EXPECT_EQ(direct.attackerState->shots.available(), 2);
	EXPECT_EQ(newHorizonsFocusMagic::charges(*active), 3);

	auto wrapped = std::make_shared<HypotheticBattle>(environment.get(), live);
	const auto source = wrapped->getForUpdate(active->unitId());
	const auto target = wrapped->getForUpdate(enemy->unitId());
	ASSERT_EQ(newHorizonsFocusMagic::charges(*source), 3);
	ASSERT_EQ(source->shots.available(), 3);
	DamageCache wrappedCache;
	const auto expected = AttackPossibility::evaluate(BattleAttackInfo(source.get(), target.get(), 0, true),
		source->getPosition(), wrappedCache, wrapped);
	ASSERT_NE(expected.attackerState, nullptr);
	EXPECT_EQ(newHorizonsFocusMagic::charges(*expected.attackerState), 2);
	EXPECT_EQ(expected.attackerState->shots.available(), 2);
	EXPECT_EQ(wire(direct.attackerState->save()), wire(expected.attackerState->save()));
	EXPECT_FLOAT_EQ(direct.attackValue(), expected.attackValue());
	EXPECT_EQ(newHorizonsFocusMagic::charges(*source), 3);
	EXPECT_EQ(gameState()->saveToMemory(), before);
	EXPECT_EQ(wire(*gameHandler->randomizer), random);
}
}
