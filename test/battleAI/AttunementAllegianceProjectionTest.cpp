/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "../../AI/BattleAI/ElementalAttunementTerminalValue.h"
#include "../../lib/battle/NewHorizonsElementalAttunement.h"

using namespace heroQueueTest;

class AttunementAllegianceProjectionTest : public HeroQueueSelectorTest
{
protected:
	AttunementAllegianceProjectionTest() { useRevisedOrders = true; }
	void prepare()
	{
		ASSERT_NO_FATAL_FAILURE(world());
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:brissa")));
		const auto skill = SecondarySkill(SecondarySkill::decode(newHorizonsElementalAttunement::SKILL_ID));
		ASSERT_TRUE(skill.hasValue());
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(arena());
	}
	void branches(PlayerColor observer = PlayerColor(0))
	{
		environment = std::make_shared<QueueEnvironment>(gameState());
		live = std::make_shared<CPlayerBattleCallback>(battle(), observer);
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	}
	static Bonus control()
	{
		return Bonus(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	}
};

TEST_F(AttunementAllegianceProjectionTest, NewlyAttunedOriginalAllyUnderEnemyControlHasNegativeTerminalMarginal)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * fire = addStack(BattleSide::ATTACKER, creatureByName("core:fireElemental"), BattleHex(6, 3), 100);
	ASSERT_NE(fire, nullptr);
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	DamageCache original;
	original.buildDamageCache(parent, BattleSide::ATTACKER);
	child->addUnitBonus(fire->unitId(), {control()});
	child->setElementalAttunementMask(BattleSide::ATTACKER, 2);
	EXPECT_EQ(child->battleGetOwner(child->battleGetUnitByID(fire->unitId())), PlayerColor(1));
	EXPECT_TRUE(child->battleGetUnitByID(fire->unitId())->elementalAttunementPassives().attuned);
	const auto harm = elementalAttunementAI::terminalMarginal(child, BattleSide::ATTACKER, 0, original);
	EXPECT_LT(harm, 0.0f);
	EXPECT_FLOAT_EQ(elementalAttunementAI::terminalMarginal(child, BattleSide::ATTACKER, 0, original, 3.0f, 2.0f), 2.0f * harm);
	EXPECT_EQ(elementalAttunementAI::terminalMarginal(child, BattleSide::ATTACKER, 2, original), 0.0f);
	EXPECT_EQ(observation(), outside);
}

TEST_F(AttunementAllegianceProjectionTest, ControlledEnemyNeverReceivesOriginalCasterAttunementOrMarginalCredit)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * fire = addStack(BattleSide::DEFENDER, creatureByName("core:fireElemental"), BattleHex(10, 3), 100);
	ASSERT_NE(fire, nullptr);
	ASSERT_NO_FATAL_FAILURE(branches());
	const auto outside = observation();
	DamageCache original;
	original.buildDamageCache(parent, BattleSide::ATTACKER);
	child->addUnitBonus(fire->unitId(), {control()});
	child->setElementalAttunementMask(BattleSide::ATTACKER, 2);
	EXPECT_EQ(child->battleGetOwner(child->battleGetUnitByID(fire->unitId())), PlayerColor(0));
	EXPECT_FALSE(child->battleGetUnitByID(fire->unitId())->elementalAttunementPassives().attuned);
	EXPECT_EQ(elementalAttunementAI::terminalMarginal(child, BattleSide::ATTACKER, 0, original), 0.0f);
	EXPECT_EQ(child->getElementalAttunementProfile(BattleSide::DEFENDER), newHorizonsElementalAttunement::Profile{});
	EXPECT_EQ(observation(), outside);
}

TEST_F(AttunementAllegianceProjectionTest, ExactUnpublishedScratchRetainsEffectsButMismatchedProviderCannotProbe)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * water = addStack(BattleSide::ATTACKER, creatureByName("core:waterElemental"), BattleHex(6, 3), 10);
	ASSERT_NE(water, nullptr);
	battle()->setElementalAttunementMask(BattleSide::ATTACKER, 4);
	ASSERT_NO_FATAL_FAILURE(branches(PlayerColor(1)));
	const auto outside = observation();
	const auto source = child->getForUpdate(water->unitId());
	const auto scratch = std::make_shared<StackWithBonuses>(child.get(), source.get());
	ASSERT_NE(child->battleGetUnitByID(source->unitId()), scratch.get());
	EXPECT_EQ(scratch->elementalAttunementPassives(), source->elementalAttunementPassives());
	EXPECT_EQ(scratch->getAvailableHealth(), source->getAvailableHealth());
	EXPECT_EQ(scratch->getCount(), source->getCount());
	// acquireState retains the immediate projected provider, rather than a
	// direct native provider. Both detached layers must preserve that lineage.
	const auto snapshot = scratch->acquireState();
	const auto nested = snapshot->acquireState();
	for(const auto & detached : {snapshot, nested})
	{
		EXPECT_EQ(detached->elementalAttunementPassives(), source->elementalAttunementPassives());
		EXPECT_EQ(detached->getAvailableHealth(), source->getAvailableHealth());
		EXPECT_EQ(detached->getCount(), source->getCount());
		EXPECT_EQ(detached->counterAttacks.available(), source->counterAttacks.available());
	}
	EXPECT_EQ(parent->getForUpdate(water->unitId())->getAvailableHealth(), source->getAvailableHealth());
	EXPECT_EQ(sibling->getForUpdate(water->unitId())->getAvailableHealth(), source->getAvailableHealth());
	battle::CUnitStateDetached wrongProvider(source.get(), enemy);
	EXPECT_THROW(wrongProvider.localInit(child.get()), std::invalid_argument);
	EXPECT_EQ(child->getElementalAttunementProfile(BattleSide::ATTACKER), newHorizonsElementalAttunement::Profile{});
	EXPECT_EQ(observation(), outside);
}

TEST_F(AttunementAllegianceProjectionTest, AlreadyFormedUnknownSourceCopiesBeforeAndAfterItsNaturalExpiry)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * water = addStack(BattleSide::ATTACKER, creatureByName("core:waterElemental"), BattleHex(6, 3), 10);
	ASSERT_NE(water, nullptr);
	battle()->setElementalAttunementMask(BattleSide::ATTACKER, 4);
	water->beginBattleForm(creatureByName("core:pikeman"), 1);
	ASSERT_TRUE(water->hasBattleForm());
	ASSERT_NO_FATAL_FAILURE(branches(PlayerColor(1)));
	const auto outside = observation();
	for(const auto & branch : {parent, child, sibling})
	{
		const auto copy = branch->getForUpdate(water->unitId());
		ASSERT_TRUE(copy->hasBattleForm());
		EXPECT_EQ(copy->getMaxHealth(), water->getMaxHealth());
		EXPECT_EQ(copy->getAvailableHealth(), water->getAvailableHealth());
		EXPECT_EQ(copy->getCount(), water->getCount());
		const auto snapshot = copy->acquireState();
		EXPECT_EQ(snapshot->elementalAttunementPassives(), water->elementalAttunementPassives());
		EXPECT_EQ(snapshot->getAvailableHealth(), water->getAvailableHealth());
		EXPECT_EQ(branch->getElementalAttunementProfile(BattleSide::ATTACKER), newHorizonsElementalAttunement::Profile{});
	}
	ASSERT_NO_THROW(child->nextRound());
	const auto restored = child->getForUpdate(water->unitId());
	EXPECT_FALSE(restored->hasBattleForm());
	const auto restoredSnapshot = restored->acquireState();
	EXPECT_EQ(restoredSnapshot->elementalAttunementPassives(), restored->elementalAttunementPassives());
	EXPECT_EQ(restoredSnapshot->getAvailableHealth(), restored->getAvailableHealth());
	EXPECT_TRUE(parent->getForUpdate(water->unitId())->hasBattleForm());
	EXPECT_TRUE(sibling->getForUpdate(water->unitId())->hasBattleForm());
	EXPECT_EQ(observation(), outside);
}
