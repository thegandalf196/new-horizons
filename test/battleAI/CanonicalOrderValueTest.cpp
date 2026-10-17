/*
 * CanonicalOrderValueTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/CanonicalOrderValue.h"
#include "../../AI/BattleAI/BattleDamageValue.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/serializer/CMemorySerializer.h"

#include <bit>
#include <limits>

namespace
{
template<typename T> std::vector<std::byte> serialized(const T & value)
{
	CMemorySerializer memory;
	memory.oser & value;
	return memory.extractBuffer();
}
}

TEST(BattleDamageValueTest, KeepsIntegerMidpointBeforeFloatConversionAndNonnegativeClamp)
{
	DamageEstimation estimate;
	estimate.damage.min = 10;
	estimate.damage.max = 11;
	EXPECT_EQ(battleDamageValue::averageOrderDamage(estimate), 10.0f);
	estimate.damage.min = -9;
	estimate.damage.max = 3;
	EXPECT_EQ(battleDamageValue::averageOrderDamage(estimate), 0.0f);
	estimate.damage.min = 16777216;
	estimate.damage.max = 16777218;
	EXPECT_EQ(std::bit_cast<uint32_t>(battleDamageValue::averageOrderDamage(estimate)),
		std::bit_cast<uint32_t>(static_cast<float>(int64_t(16777217))));
}

class CanonicalOrderValueTest : public HeroCommandFixture
{
protected:
	CStack * shooter = nullptr;
	CStack * target = nullptr;
	void prepare()
	{
		ASSERT_NO_FATAL_FAILURE(prepareCommands());
		shooter = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 10);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(75), 10);
		ASSERT_NE(shooter, nullptr);
		ASSERT_NE(target, nullptr);
		shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::SHOOTER, BonusSource::OTHER, 1, BonusSourceID()));
		shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::SHOTS, BonusSource::OTHER, 10, BonusSourceID()));
		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = shooter->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
	}
};

TEST_F(CanonicalOrderValueTest, FocusFireRetainsSnapshotCoefficientAndRawDamageDomain)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_FALSE(heroCommands::hasCombinedArms(attackerSideHero));
	const std::vector<uint32_t> ids{target->unitId()};
	heroOrderValue::CanonicalOrderValue value(*battle(), BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE, ids);
	float expected = 0.0f;
	for(const auto * unit : value.ownUnits)
		if(unit->willMove(0) && unit->isShooter() && !unit->isTurret()
			&& !unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
			&& battle()->battleCanShoot(unit, target->getPosition()))
		{
			const auto damage = battle()->battleEstimateDamage(BattleAttackInfo(unit, target, 0, true));
			const auto midpoint = static_cast<float>(std::max<int64_t>(0, damage.damage.min + damage.damage.max) / 2);
			expected += midpoint * 37.0f / 100.0f;
		}
	ASSERT_GT(expected, 0.0f);
	const auto actual = value.directValue(37).direct;
	ASSERT_TRUE(actual);
	EXPECT_EQ(std::bit_cast<uint32_t>(*actual), std::bit_cast<uint32_t>(expected));
}

TEST_F(CanonicalOrderValueTest, HoldRemainsSeparateFromAValidZeroDirectEstimate)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const std::vector<uint32_t> noTargets;
	heroOrderValue::CanonicalOrderValue hold(*battle(), BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE, noTargets);
	EXPECT_FALSE(hold.directValue(std::nullopt).direct);
	const std::vector<uint32_t> missing{std::numeric_limits<uint32_t>::max()};
	heroOrderValue::CanonicalOrderValue invalid(*battle(), BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE, missing);
	const auto zero = invalid.directValue(37).direct;
	ASSERT_TRUE(zero);
	EXPECT_EQ(*zero, 0.0f);
}

TEST_F(CanonicalOrderValueTest, AllDirectRoutesLeaveLedgerOrdersRngAndTrafficExact)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto ledger = serialized(battle()->getHeroActionAllowances(BattleSide::ATTACKER));
	const auto orders = serialized(battle()->getHeroOrderStates(BattleSide::ATTACKER));
	const auto rng = serialized(*gameHandler->randomizer);
	const auto traffic = server.startedActions.size();
	for(const auto command : {HeroCommand::CHARGE, HeroCommand::HOLD_THE_LINE, HeroCommand::RIPOSTE,
		HeroCommand::BRACE, HeroCommand::PROTECT, HeroCommand::FLANK, HeroCommand::SECOND_WIND, HeroCommand::FOCUS_FIRE})
	{
		const auto ids = command == HeroCommand::PROTECT
			? std::vector<uint32_t>{shooter->unitId(), shooter->unitId()}
			: std::vector<uint32_t>{target->unitId()};
		heroOrderValue::CanonicalOrderValue value(*battle(), BattleSide::ATTACKER, command, ids);
		const auto result = value.directValue(37).direct;
		EXPECT_EQ(result.has_value(), command != HeroCommand::HOLD_THE_LINE);
	}
	EXPECT_EQ(serialized(battle()->getHeroActionAllowances(BattleSide::ATTACKER)), ledger);
	EXPECT_EQ(serialized(battle()->getHeroOrderStates(BattleSide::ATTACKER)), orders);
	EXPECT_EQ(serialized(*gameHandler->randomizer), rng);
	EXPECT_EQ(server.startedActions.size(), traffic);
}
