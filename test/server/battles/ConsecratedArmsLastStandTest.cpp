/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/battle/ConsecratedArmsOrder.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/CUnitState.h"
#include "../../../lib/battle/NewHorizonsArmorer.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../server/battles/BattleLastStandDamage.h"
#include "../../../server/battles/BattleConsecratedArms.h"

namespace
{
class ConsecratedArmsLastStandTest : public HeroCommandFixture
{
protected:
	CStack * target = nullptr;
	const battle::Unit * source = nullptr;
	void prepare()
	{
		useRevisedOrders = true;
		startGame();
		const auto armorer = SecondarySkill(SecondarySkill::decode(newHorizonsArmorer::SKILL_ID));
		for(const auto & [rank, suffix] : {std::pair{MasteryLevel::BASIC, "pavise"},
			std::pair{MasteryLevel::ADVANCED, "veteran"}, std::pair{MasteryLevel::EXPERT, "lastStand"}})
		{
			defenderSideHero->setSecSkillLevel(armorer, rank, ChangeValueMode::ABSOLUTE);
			defenderSideHero->applyPerkSelection({newHorizonsArmorer::SKILL_ID,
				std::string(newHorizonsArmorer::SKILL_ID) + "." + suffix});
		}
		ASSERT_TRUE(newHorizonsArmorer::hasLastStand(defenderSideHero));
		startBattle();
		target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 100);
		beginCombat();
		source = battle()->battleActiveUnit();
		ASSERT_NE(source, nullptr);
	}
	void grant(consecratedArms::Prayer prayer)
	{
		auto packet = consecratedArms::orderEffects(*battle(), BattleSide::DEFENDER,
			{target->unitId()}, {prayer}, consecratedArms::Policy{1}, 200);
		gameHandler->sendAndApply(packet);
	}
	bool preflight(int64_t incoming, const battle::DamageContext & context,
		std::shared_ptr<battle::CUnitState> & state, BattleStackAttacked & hit)
	{
		BattleAttackInfo attack(source, target, 0, false);
		BattleAttack packet;
		std::array<bool, 2> used{};
		hit.stackAttacked = target->unitId();
		hit.damageAmount = incoming;
		state = target->acquireState();
		return battleLastStandDamage::prepare(*battle(), attack, packet, hit, *state, used, false, context);
	}
};
}

TEST_F(ConsecratedArmsLastStandTest, VowSurvivalDoesNotSpendLastStand)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	grant(consecratedArms::Prayer::UNBROKEN_VOW);
	battleConsecratedArms::AttackContext event;
	const auto context = battleConsecratedArms::damageContext(event, *target, true);
	BattleStackAttacked hit;
	std::shared_ptr<battle::CUnitState> state;
	EXPECT_FALSE(preflight(1000000, context, state, hit));
	EXPECT_EQ(hit.armorerLastStandSide, BattleSide::NONE);
	EXPECT_FALSE(state->armorerLastStandDefending);
	EXPECT_EQ(context.vow->remainingCasualties, 60);
	EXPECT_FALSE(battle()->armorerLastStandUsed(BattleSide::DEFENDER));
}

TEST_F(ConsecratedArmsLastStandTest, RedemptionSurvivalDoesNotSpendLastStand)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	grant(consecratedArms::Prayer::VICARIOUS_REDEMPTION);
	auto granted = target->acquireState();
	granted->gainRedemptionShield(target->getAvailableHealth(), 5000, false);
	BattleUnitsChanged update;
	update.battleID = battle()->getBattleID();
	UnitChanges changed(target->unitId(), UnitChanges::EOperation::UPDATE);
	changed.data = granted->save();
	update.changedStacks.push_back(std::move(changed));
	gameHandler->sendAndApply(update);
	ASSERT_GT(target->getRedemptionShieldHitPoints(), 0);
	const int64_t incoming = target->getAvailableHealth() + target->getRedemptionShieldHitPoints() - 1;
	const battle::DamageContext context{battle::DamageClass::PHYSICAL, battle::DamageOrigin::CREATURE_STRIKE};
	BattleStackAttacked hit;
	std::shared_ptr<battle::CUnitState> state;
	EXPECT_FALSE(preflight(incoming, context, state, hit));
	EXPECT_EQ(hit.armorerLastStandSide, BattleSide::NONE);
	EXPECT_FALSE(state->armorerLastStandDefending);
}

TEST_F(ConsecratedArmsLastStandTest, ActuallyLethalOverflowStillTriggersAndPreservesOneHealth)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const battle::DamageContext context{battle::DamageClass::PHYSICAL, battle::DamageOrigin::CREATURE_STRIKE};
	BattleStackAttacked hit;
	std::shared_ptr<battle::CUnitState> state;
	EXPECT_TRUE(preflight(1000000, context, state, hit));
	EXPECT_EQ(hit.armorerLastStandSide, BattleSide::DEFENDER);
	EXPECT_EQ(hit.damageAmount, target->getAvailableHealth() - 1);
	EXPECT_TRUE(state->armorerLastStandDefending);
}
