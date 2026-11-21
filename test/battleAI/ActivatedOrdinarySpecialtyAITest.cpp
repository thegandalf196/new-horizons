/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#if ENABLE_BATTLE_AI
#include "HeroQueueSelectorFixture.h"
#include "../ActivatedOrdinarySpecialties.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/modding/CModHandler.h"

namespace
{
using Specialist = ActivatedOrdinarySpecialty;

class SideCallback final : public CBattleCallback
{
public:
	explicit SideCallback(PlayerColor player) : CBattleCallback(player, nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction &) override {}
	void battleMakeUnitAction(const BattleID &, const BattleAction &) override {}
};

/// The battle AI chooses every creature action for both armies while a newly
/// activated extended ordinary specialty is in force; each choice must be accepted.
class ActivatedOrdinarySpecialtyAITest : public heroQueueTest::HeroQueueSelectorTest,
	public ::testing::WithParamInterface<Specialist>
{
protected:
	void SetUp() override
	{
		HeroQueueSelectorTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons test profile";
	}
	void mapLoaded(CMap * map) override
	{
		useRevisedOrders = true;
		HeroQueueSelectorTest::mapLoaded(map);
	}
	void armies()
	{
		const auto & row = GetParam();
		ASSERT_NO_FATAL_FAILURE(world());
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode(row.hero)));
		ASSERT_TRUE(newHorizonsHeroes::hasOrdinaryCreatureSpecialty(attackerSideHero));
		ASSERT_NO_FATAL_FAILURE(startBattle());
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		ASSERT_NE(addStack(BattleSide::ATTACKER, creatureByName(row.upgraded), BattleHex(1, 3), 12), nullptr);
		ASSERT_NE(addStack(BattleSide::ATTACKER, creatureByName(row.base), BattleHex(1, 7), 12), nullptr);
		ASSERT_NE(addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(2, 5), 2), nullptr);
		ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:halberdier"), BattleHex(14, 3), 30), nullptr);
		ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:marksman"), BattleHex(15, 7), 15), nullptr);
		ASSERT_NE(addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(13, 5), 3), nullptr);
		ASSERT_NO_FATAL_FAILURE(beginCombat());
	}
	/// The fixture battle has no battle query, so a decided battle is not ended for us.
	bool decided() const
	{
		if(gameState()->currentBattles.empty())
			return true;
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			if(battle()->battleGetUnitsIf([side](const battle::Unit * unit)
				{ return unit->unitSide() == side && unit->alive() && !unit->isGhost(); }).empty())
				return true;
		return false;
	}
};
}

TEST_P(ActivatedOrdinarySpecialtyAITest, BattleAIChoosesAcceptedActionsForBothArmies)
{
	ASSERT_NO_FATAL_FAILURE(armies());
	int specialistActions = 0;
	const auto upgraded = creatureByName(GetParam().upgraded);
	const auto base = creatureByName(GetParam().base);
	for(int activation = 0; activation < 24 && !decided(); ++activation)
	{
		const auto * unit = battle()->battleActiveUnit();
		ASSERT_NE(unit, nullptr);
		const auto side = unit->unitSide();
		const auto player = battle()->sideToPlayer(side);
		auto callback = std::make_shared<SideCallback>(player);
		callback->onBattleStarted(battle());
		auto environment = std::make_shared<heroQueueTest::QueueEnvironment>(gameState());
		ASSERT_TRUE(unit->alive()) << activation;
		const auto * stack = battle()->battleGetStackByID(unit->unitId());
		ASSERT_NE(stack, nullptr) << activation;
		BattleAction action;
		{
			BattleEvaluator evaluator(environment, callback, stack, player, BattleID(0), side, 1.0f, 2);
			ASSERT_NO_THROW(action = evaluator.selectStackAction(stack));
		}
		ASSERT_EQ(action.stackNumber, static_cast<int32_t>(unit->unitId())) << activation;
		if(side == BattleSide::ATTACKER && (unit->creatureId() == upgraded || unit->creatureId() == base))
			++specialistActions;
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), player, action))
			<< "activation " << activation << " action " << static_cast<int>(action.actionType);
	}
	EXPECT_GT(specialistActions, 0);
}

INSTANTIATE_TEST_SUITE_P(ActivatedExtendedOrdinary, ActivatedOrdinarySpecialtyAITest,
	::testing::ValuesIn(activatedOrdinarySpecialties()),
	[](const ::testing::TestParamInfo<Specialist> & info) { return std::string(info.param.hero).substr(5); });
#endif
