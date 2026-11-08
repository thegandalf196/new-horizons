/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "../server/battles/NewHorizonsMachineSpecialtyFixture.h"
#include "../../AI/BattleAI/AttackControllerProjection.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../server/battles/BattleProcessor.h"
using heroQueueTest::QueueEnvironment;
using heroQueueTest::wire;

class AttackControllerProjectionTest : public NewHorizonsMachineSpecialtyFixture
{
protected:
	std::shared_ptr<QueueEnvironment> environment;
	std::shared_ptr<CPlayerBattleCallback> live;
	std::shared_ptr<HypotheticBattle> parent, child, sibling;
	void branches()
	{
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(const auto * state = dynamic_cast<const battle::CUnitState *>(unit))
			{ (void)state->counterAttacks.available(); (void)state->shots.available(); (void)state->casts.available(); }
		environment = std::make_shared<QueueEnvironment>(gameState());
		live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	}
	std::string outside()
	{
		std::string result = wire(*gameState()) + wire(*gameHandler->randomizer);
		result += wire(*dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator()));
		for(const auto * view : {parent.get(), sibling.get()})
			for(const auto * unit : view->battleGetAllUnits(false)) result += wire(unit->acquireState()->save());
		return result;
	}
};

TEST_F(AttackControllerProjectionTest, FieryShotAreaHealthAndSelectedReplayAgreeWithoutHeroResourcesOrHistory)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:torosar", 0, false));
	auto * collateral = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(8, 6), 1000);
	ASSERT_NE(collateral, nullptr);
	forceActorFirst(); beginCombat();
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto before = collateral->getAvailableHealth();
	const auto native = outside();
	const auto command = child->getCommandResources(BattleSide::ATTACKER);
	const auto mask = child->getElementalAttunementMask(BattleSide::ATTACKER);
	const auto history = child->getCastSpells(BattleSide::ATTACKER);
	const auto source = child->getForUpdate(actor->unitId());
	const auto target = child->getForUpdate(victim->unitId());
	DamageCache cache;
	const auto preview = AttackPossibility::evaluate(BattleAttackInfo(source.get(), target.get(), 0, true), source->getPosition(), cache, child);
	ASSERT_NE(preview.effectPreview, nullptr);
	ASSERT_FALSE(preview.fortuneStrikes.empty());
	EXPECT_TRUE(preview.fortuneStrikes.front().attackController.fireball);
	EXPECT_LT(preview.effectPreview->battleGetUnitByID(collateral->unitId())->getAvailableHealth(), before);
	auto replay = std::make_shared<HypotheticBattle>(environment.get(), parent);
	BattleExchangeVariant exchange; exchange.trackAttack(preview, replay, cache);
	for(const auto id : {actor->unitId(), victim->unitId(), collateral->unitId()})
		EXPECT_EQ(replay->battleGetUnitByID(id)->getAvailableHealth(), preview.effectPreview->battleGetUnitByID(id)->getAvailableHealth());
	EXPECT_EQ(preview.effectPreview->getCommandResources(BattleSide::ATTACKER), command);
	EXPECT_EQ(preview.effectPreview->getElementalAttunementMask(BattleSide::ATTACKER), mask);
	EXPECT_EQ(preview.effectPreview->getCastSpells(BattleSide::ATTACKER), history);
	EXPECT_EQ(outside(), native);
}

TEST_F(AttackControllerProjectionTest, LethalPrimaryRetainsExactRearImpactAndHiddenPolicyDoesNotInventFireball)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:torosar", 0, false));
	auto * primary = addStack(BattleSide::DEFENDER, creatureByName("core:centaur"), BattleHex(10, 7), 1);
	ASSERT_NE(primary, nullptr);
	BattleHex rear;
	for(const auto hex : primary->getHexes()) if(hex != primary->getPosition()) rear = hex;
	ASSERT_TRUE(rear.isValid());
	forceActorFirst(); beginCombat();
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto source = child->getForUpdate(actor->unitId());
	const auto target = child->getForUpdate(primary->unitId());
	const BattleAttackInfo hit(source.get(), target.get(), 0, true);
	const auto receipt = attackControllerAI::capture(*child, hit, rear, 100);
	ASSERT_TRUE(receipt.fireball); EXPECT_EQ(receipt.impact, rear);
	int64_t lethal = target->getAvailableHealth(); target->damage(lethal);
	EXPECT_FALSE(target->alive());
	EXPECT_NO_THROW(attackControllerAI::apply(*child, receipt));
	auto hidden = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1));
	auto hiddenBranch = std::make_shared<HypotheticBattle>(environment.get(), hidden);
	const auto * hiddenActor = hiddenBranch->battleGetUnitByID(actor->unitId());
	const auto * hiddenTarget = hiddenBranch->battleGetUnitByID(primary->unitId());
	EXPECT_FALSE(attackControllerAI::capture(*hiddenBranch, BattleAttackInfo(hiddenActor, hiddenTarget, 0, true), rear, 100).fireball);
}

TEST_F(AttackControllerProjectionTest, CrippleUsesSharedEarlyPlanAndSurvivesSelectedReplayWithoutStacking)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:gerwulf", 0, false));
	forceActorFirst(); beginCombat();
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto native = outside();
	const int speed = child->battleGetUnitByID(victim->unitId())->getMovementRange();
	DamageCache cache;
	const auto source = child->getForUpdate(actor->unitId()), target = child->getForUpdate(victim->unitId());
	const auto preview = AttackPossibility::evaluate(BattleAttackInfo(source.get(), target.get(), 0, true), source->getPosition(), cache, child);
	ASSERT_NE(preview.effectPreview, nullptr);
	const auto * after = preview.effectPreview->battleGetUnitByID(victim->unitId());
	ASSERT_EQ(newHorizonsAttackControllers::crippleRemoval(*after).size(), 2u);
	EXPECT_EQ(after->getMovementRange(), std::max(0, speed - 3));
	auto replay = std::make_shared<HypotheticBattle>(environment.get(), parent);
	BattleExchangeVariant exchange; exchange.trackAttack(preview, replay, cache);
	EXPECT_EQ(replay->battleGetUnitByID(victim->unitId())->getMovementRange(), after->getMovementRange());
	const auto * repeatedSource = preview.effectPreview->battleGetUnitByID(actor->unitId());
	const auto repeated = attackControllerAI::capture(*preview.effectPreview,
		BattleAttackInfo(repeatedSource, after, 0, true), after->getPosition(), 1);
	attackControllerAI::applyCripple(*preview.effectPreview, repeated);
	EXPECT_EQ(newHorizonsAttackControllers::crippleRemoval(*after).size(), 2u);
	EXPECT_EQ(after->getMovementRange(), std::max(0, speed - 3));
	EXPECT_EQ(outside(), native);
}

TEST_F(AttackControllerProjectionTest, AcceptedNativeFixedShotMatchesAreaForecastAfterBorrowedViewsAreRetired)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:torosar", 0, false));
	auto * collateral = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(8, 6), 1000);
	ASSERT_NE(collateral, nullptr);
	forceActorFirst(); forceMaximumDamage(actor); beginCombat();
	ASSERT_NO_FATAL_FAILURE(branches());
	std::map<uint32_t, int64_t> expected;
	{
		DamageCache cache;
		const auto source = child->getForUpdate(actor->unitId()), target = child->getForUpdate(victim->unitId());
		const auto preview = AttackPossibility::evaluate(BattleAttackInfo(source.get(), target.get(), 0, true), source->getPosition(), cache, child);
		ASSERT_NE(preview.effectPreview, nullptr);
		for(const auto * unit : preview.effectPreview->battleGetAllUnits(false))
			expected[unit->unitId()] = unit->getAvailableHealth();
	}
	child.reset(); sibling.reset(); parent.reset(); live.reset(); environment.reset();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto command = battle()->getBattle()->getCommandResources(BattleSide::ATTACKER);
	const auto history = battle()->getBattle()->getCastSpells(BattleSide::ATTACKER);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeShotAttack(actor, victim)));
	for(const auto & [id, health] : expected)
	{
		const auto * unit = battle()->battleGetUnitByID(id); ASSERT_NE(unit, nullptr);
		EXPECT_EQ(unit->getAvailableHealth(), health);
	}
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->getBattle()->getCommandResources(BattleSide::ATTACKER), command);
	EXPECT_EQ(battle()->getBattle()->getCastSpells(BattleSide::ATTACKER), history);
}

TEST_F(AttackControllerProjectionTest, HeavyBatteryUsesSharedPhysicalDamageAndRetainsOrdinaryDistancePenalty)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:christian", 0, false));
	forceActorFirst(); forceMaximumDamage(actor); beginCombat();
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto source = child->getForUpdate(actor->unitId()), target = child->getForUpdate(victim->unitId());
	ASSERT_EQ(newHorizonsAttackControllers::ballistaPhysicalDamagePercent(*child, *source), 200);
	const BattleAttackInfo near(source.get(), target.get(), 0, true);
	DamageCache nearCache;
	const auto nearDamage = nearCache.getDamage(source.get(), target.get(), child);
	EXPECT_EQ(nearDamage, child->battleExpectedLuckDamage(near));
	child->moveUnit(target->unitId(), BattleHex(15, 9));
	const BattleAttackInfo far(source.get(), target.get(), 0, true);
	ASSERT_GT(BattleHex::getDistance(source->getPosition(), target->getPosition()), 10);
	DamageCache farCache;
	const auto farDamage = farCache.getDamage(source.get(), target.get(), child);
	EXPECT_EQ(farDamage, child->battleExpectedLuckDamage(far));
	EXPECT_LT(farDamage, nearDamage);
}
