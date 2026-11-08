/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "../../lib/battle/NewHorizonsElementalAttunement.h"
#include "../../lib/battle/NewHorizonsBulwark.h"
#include "../../server/battles/BattleActivationEffects.h"
#include <array>
#include <tuple>
using namespace heroQueueTest;
class StoneboundPeriodicProjectionTest : public HeroQueueSelectorTest
{
protected:
	StoneboundPeriodicProjectionTest() { useRevisedOrders = true; }
	void prepare()
	{
		ASSERT_NO_FATAL_FAILURE(world());
		defenderSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:brissa")));
		defenderSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 48, ChangeValueMode::ABSOLUTE);
		const std::string skill = newHorizonsElementalAttunement::SKILL_ID;
		ASSERT_NO_FATAL_FAILURE(selectPerk(defenderSideHero, skill, skill + ".elementalAttunement", MasteryLevel::BASIC));
		ASSERT_NO_FATAL_FAILURE(selectPerk(defenderSideHero, skill, skill + ".adaptiveElement", MasteryLevel::ADVANCED));
		ASSERT_NO_FATAL_FAILURE(selectPerk(defenderSideHero, skill, skill + ".perfectConvergence", MasteryLevel::EXPERT));
		ASSERT_EQ(defenderSideHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), 60);
		ASSERT_NO_FATAL_FAILURE(arena());
		ASSERT_EQ(enemy->elementalAttunementPassives().physicalReductionBasisPoints, 1500);
		environment = std::make_shared<QueueEnvironment>(gameState());
		live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	}
	static auto values(const battle::CUnitState & unit)
	{
		return std::tuple{unit.getAvailableHealth(), unit.getCount(), unit.getFirstHPleft(),
			unit.physicalPoisonBaseDamage, unit.physicalPoisonActivationsRemaining, unit.physicalPoisonSpellOrigin()};
	}
	void install(int base, BattleSide origin)
	{
		auto state = enemy->acquireState();
		ASSERT_TRUE(newHorizonsBulwark::applyPhysicalPoison(state.get(), base, actor->unitId()));
		state->physicalPoisonSpellSide = origin;
		BattleUnitsChanged update; update.battleID = BattleID(0);
		update.changedStacks.emplace_back(enemy->unitId(), UnitChanges::EOperation::UPDATE);
		update.changedStacks.back().data = state->save();
		gameHandler->sendAndApply(update);
	}
	void compareTick(int64_t damage, int remaining)
	{
		const auto before = values(*enemy);
		const auto nativeBefore = enemy->acquireState()->save();
		const auto pool = battle()->getCommandResources(BattleSide::DEFENDER);
		const auto rng = wire(*gameHandler->randomizer);
		const auto fixtureRng = wire(dynamic_cast<CRandomGenerator &>(*getRNG()));
		auto root = std::make_shared<HypotheticBattle>(environment.get(), live);
		auto branch = std::make_shared<HypotheticBattle>(environment.get(), root);
		auto sibling = std::make_shared<HypotheticBattle>(environment.get(), root);
		EXPECT_EQ(branch->getElementalAttunementProfile(BattleSide::DEFENDER), newHorizonsElementalAttunement::Profile{});
		branch->nextTurn(enemy->unitId(), BattleUnitTurnReason::TURN_QUEUE);
		auto predicted = branch->getForUpdate(enemy->unitId());
		const auto expected = values(*predicted);
		EXPECT_EQ(predicted->getAvailableHealth(), std::get<0>(before) - damage);
		EXPECT_EQ(predicted->physicalPoisonActivationsRemaining, remaining);
		EXPECT_EQ(values(*root->getForUpdate(enemy->unitId())), before);
		EXPECT_EQ(values(*sibling->getForUpdate(enemy->unitId())), before);
		EXPECT_EQ(enemy->acquireState()->save(), nativeBefore);
		EXPECT_EQ(battle()->getCommandResources(BattleSide::DEFENDER), pool);
		EXPECT_EQ(wire(*gameHandler->randomizer), rng);
		EXPECT_EQ(wire(dynamic_cast<CRandomGenerator &>(*getRNG())), fixtureRng);
		// Retire every borrowed projection before advancing its native source.
		predicted.reset(); branch.reset(); sibling.reset(); root.reset();
		battleActivationEffects::apply(gameHandler.get(), *battle(), enemy, BattleUnitTurnReason::TURN_QUEUE);
		EXPECT_EQ(values(*enemy), expected);
		EXPECT_EQ(battle()->getCommandResources(BattleSide::DEFENDER), pool);
		EXPECT_EQ(wire(*gameHandler->randomizer), rng);
		EXPECT_EQ(wire(dynamic_cast<CRandomGenerator &>(*getRNG())), fixtureRng);
	}
};
TEST_F(StoneboundPeriodicProjectionTest, NativeAndProjectedHeroAndCreatureTicksUseTheSameSingleReduction)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(const auto origin : {BattleSide::NONE, BattleSide::ATTACKER})
	{
		ASSERT_NO_FATAL_FAILURE(install(20, origin));
		ASSERT_NO_FATAL_FAILURE(compareTick(17, 2));
		ASSERT_NO_FATAL_FAILURE(compareTick(25, 1));
		ASSERT_NO_FATAL_FAILURE(compareTick(34, 0));
		EXPECT_EQ(enemy->physicalPoisonBaseDamage, 0);
	}
}
TEST_F(StoneboundPeriodicProjectionTest, BothOriginsAgeProtectedZeroTicksAndExpireAfterThreeActivations)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(const auto origin : {BattleSide::NONE, BattleSide::ATTACKER})
	{
		ASSERT_NO_FATAL_FAILURE(install(1, origin));
		ASSERT_NO_FATAL_FAILURE(compareTick(0, 2));
		ASSERT_NO_FATAL_FAILURE(compareTick(0, 1));
		ASSERT_NO_FATAL_FAILURE(compareTick(1, 0));
		EXPECT_EQ(enemy->physicalPoisonBaseDamage, 0);
	}
}
