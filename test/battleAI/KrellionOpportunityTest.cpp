/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "../server/battles/NewHorizonsMachineSpecialtyFixture.h"
#include "../../AI/BattleAI/AttackControllerProjection.h"
#include "../../AI/BattleAI/SpecialtyProcValue.h"
class KrellionOpportunityTest : public NewHorizonsMachineSpecialtyFixture {};
TEST_F(KrellionOpportunityTest, UniformRecipientMarginalsDoNotStackRepeatedBuffsOrSpendResources)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:krellion", 0, false));
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(const auto * state = dynamic_cast<const battle::CUnitState *>(unit))
		{ (void)state->counterAttacks.available(); (void)state->shots.available(); (void)state->casts.available(); }
	heroQueueTest::QueueEnvironment environment(gameState());
	const std::shared_ptr<CBattleInfoCallback> authority(gameState(), battle());
	auto parent = std::make_shared<HypotheticBattle>(&environment, authority);
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	const auto * source = child->battleGetUnitByID(actor->unitId());
	const auto * target = child->battleGetUnitByID(victim->unitId());
	const auto receipt = attackControllerAI::capture(*child, BattleAttackInfo(source, target, 0, false), target->getPosition(), 0);
	ASSERT_TRUE(receipt.bloodlust);
	const auto pool = newHorizonsAttackControllers::bloodlustRecipients(*child, *source);
	ASSERT_GT(pool.size(), 1u);
	EXPECT_NE(std::ranges::find(pool, source->unitId()), pool.end());
	const auto native = heroQueueTest::wire(*gameState());
	const auto random = heroQueueTest::wire(*dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator()));
	const auto abilityRandom = heroQueueTest::wire(*gameHandler->randomizer);
	const auto command = child->getCommandResources(BattleSide::ATTACKER);
	AttackPossibility once(source->getPosition(), target->getPosition(), BattleAttackInfo(source, target, 0, false));
	once.attackerState = child->getForUpdate(source->unitId());
	auto twice = once;
	DamageCache cache;
	specialtyProcAI::Opportunities one, two;
	one.observeBloodlust(child, receipt);
	two.observeBloodlust(child, receipt); two.observeBloodlust(child, receipt);
	one.complete(once, child, cache); two.complete(twice, child, cache);
	ASSERT_GT(once.defenderDamageReduce, 0);
	EXPECT_NEAR(twice.defenderDamageReduce,
		once.defenderDamageReduce * (2 - 1.0 / pool.size()), 1e-3);
	EXPECT_LT(twice.defenderDamageReduce, 2 * once.defenderDamageReduce);
	const auto paid = once.defenderDamageReduce;
	one.complete(once, child, cache); EXPECT_EQ(once.defenderDamageReduce, paid);
	EXPECT_EQ(child->getCommandResources(BattleSide::ATTACKER), command);
	for(const auto id : pool)
	{
		EXPECT_EQ(heroQueueTest::wire(child->getLocalSpellEffects(id)), heroQueueTest::wire(parent->getLocalSpellEffects(id)));
		EXPECT_EQ(heroQueueTest::wire(sibling->getLocalSpellEffects(id)), heroQueueTest::wire(parent->getLocalSpellEffects(id)));
	}
	EXPECT_EQ(heroQueueTest::wire(*gameState()), native);
	EXPECT_EQ(heroQueueTest::wire(*dynamic_cast<CRandomGenerator *>(&gameHandler->getRandomGenerator())), random);
	EXPECT_EQ(heroQueueTest::wire(*gameHandler->randomizer), abilityRandom);
}
TEST_F(KrellionOpportunityTest, ActualAcceptedPrimaryAndOrdinaryRetaliationShareEligibilityButCollateralDoesNot)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:krellion", 0, false));
	heroQueueTest::QueueEnvironment environment(gameState());
	const std::shared_ptr<CBattleInfoCallback> authority(gameState(), battle());
	auto branch = std::make_shared<HypotheticBattle>(&environment, authority);
	const auto * source = branch->battleGetUnitByID(actor->unitId());
	const auto * target = branch->battleGetUnitByID(victim->unitId());
	BattleAttackInfo hit(source, target, 0, false);
	EXPECT_TRUE(attackControllerAI::capture(*branch, hit, target->getPosition(), 0).bloodlust);
	hit.retaliation = true;
	EXPECT_TRUE(attackControllerAI::capture(*branch, hit, target->getPosition(), 0).bloodlust);
	hit.secondaryAttack = true;
	EXPECT_FALSE(attackControllerAI::capture(*branch, hit, target->getPosition(), 0).bloodlust);
	hit.secondaryAttack = false; hit.shooting = true;
	EXPECT_FALSE(attackControllerAI::capture(*branch, hit, target->getPosition(), 0).bloodlust);
}
