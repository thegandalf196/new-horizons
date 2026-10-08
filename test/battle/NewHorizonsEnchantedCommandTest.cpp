/*
 * NewHorizonsEnchantedCommandTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/battle/NewHorizonsEnchantedCommand.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/json/JsonNode.h"

#include "mock/mock_BonusBearer.h"
#include "mock/mock_battle_IBattleState.h"
#include "mock/mock_battle_Unit.h"

using namespace testing;

namespace
{
class EnchantedCommandBattle : public BattleStateMock
{
public:
	JsonNode commandRules{JsonPath::builtin("config/newHorizonsCombat")};
	const JsonNode & getHeroCommandRules() const override { return commandRules["combat"]["heroCommands"]; }
};

class EnchantedCommandCallback : public CBattleInfoCallback
{
public:
	const IBattleInfo * state = nullptr;
	const IBattleInfo * getBattle() const override { return state; }
	std::optional<PlayerColor> getPlayerID() const override { return std::nullopt; }
};

class EnchantedCommandUnit : public UnitMock
{
public:
	BonusBearerMock bonuses;
	void configure(uint32_t id, bool shooter, PlayerColor owner = PlayerColor(0))
	{
		ON_CALL(*this, unitId()).WillByDefault(Return(id));
		ON_CALL(*this, unitOwner()).WillByDefault(Return(owner));
		ON_CALL(*this, unitSide()).WillByDefault(Return(owner == PlayerColor(0) ? BattleSide::ATTACKER : BattleSide::DEFENDER));
		ON_CALL(*this, unitSlot()).WillByDefault(Return(SlotID(0)));
		ON_CALL(*this, alive()).WillByDefault(Return(true));
		ON_CALL(*this, isGhost()).WillByDefault(Return(false));
		ON_CALL(*this, isShooter()).WillByDefault(Return(shooter));
		ON_CALL(*this, getAllBonuses(_, _)).WillByDefault(Invoke(&bonuses, &BonusBearerMock::getAllBonuses));
		ON_CALL(*this, getTreeVersion()).WillByDefault(Invoke(&bonuses, &BonusBearerMock::getTreeVersion));
	}
};

class NewHorizonsEnchantedCommandTest : public Test
{
protected:
	NiceMock<EnchantedCommandBattle> battle;
	EnchantedCommandCallback callback;
	std::vector<std::unique_ptr<NiceMock<EnchantedCommandUnit>>> units;
	void SetUp() override
	{
		callback.state = &battle;
		ON_CALL(battle, getRound()).WillByDefault(Return(1));
		ON_CALL(battle, getSidePlayer(_)).WillByDefault([](BattleSide side) { return PlayerColor(side == BattleSide::ATTACKER ? 0 : 1); });
		ON_CALL(battle, getUnitsIf(_)).WillByDefault([this](const battle::UnitFilter & filter)
		{
			battle::Units result;
			for(const auto & unit : units)
				if(filter(unit.get()))
					result.push_back(unit.get());
			return result;
		});
		add(10, false);
		add(20, true);
		add(30, false);
		add(40, true, PlayerColor(1));
	}
	EnchantedCommandUnit & add(uint32_t id, bool shooter, PlayerColor owner = PlayerColor(0))
	{
		auto unit = std::make_unique<NiceMock<EnchantedCommandUnit>>();
		unit->configure(id, shooter, owner);
		auto & result = *unit;
		units.push_back(std::move(unit));
		return result;
	}
	HeroOrderState order(HeroCommand command) const
	{
		HeroOrderState result;
		result.command = command;
		result.issuedRound = 1;
		result.warcastingBonusPercent = 20;
		return result;
	}
	std::vector<uint32_t> recipients(const HeroOrderState & state) const
	{
		return newHorizonsEnchantedCommand::recipientIds(callback, BattleSide::ATTACKER, state);
	}
};
}

TEST_F(NewHorizonsEnchantedCommandTest, ProtectAndSecondWindHaveOnlySelectedRecipients)
{
	auto protect = order(HeroCommand::PROTECT);
	protect.primaryTargetUnitId = 10;
	protect.secondaryTargetUnitId = 20;
	EXPECT_EQ(recipients(protect), (std::vector<uint32_t>{10, 20}));
	auto wind = order(HeroCommand::SECOND_WIND);
	wind.primaryTargetUnitId = 20;
	EXPECT_EQ(recipients(wind), (std::vector<uint32_t>{20}));
}

TEST_F(NewHorizonsEnchantedCommandTest, EnemySelectionsDoNotReceiveMoraleAndShootersCanChargeOrFlank)
{
	auto focus = order(HeroCommand::FOCUS_FIRE);
	focus.primaryTargetUnitId = 40;
	EXPECT_EQ(recipients(focus), (std::vector<uint32_t>{20}));
	for(const auto command : {HeroCommand::CHARGE, HeroCommand::FLANK, HeroCommand::RIPOSTE, HeroCommand::BRACE})
	{
		auto state = order(command);
		state.primaryTargetUnitId = 40;
		EXPECT_EQ(recipients(state), (std::vector<uint32_t>{10, 20, 30}));
	}
}

TEST_F(NewHorizonsEnchantedCommandTest, HoldUsesCapturedAnchorsAndDeduplicates)
{
	auto hold = order(HeroCommand::HOLD_THE_LINE);
	hold.anchors = {{20, 100}, {20, 100}, {40, 110}};
	EXPECT_EQ(recipients(hold), (std::vector<uint32_t>{20}));
}

TEST_F(NewHorizonsEnchantedCommandTest, DeadGhostAndNonCreatureRecipientsAreExcluded)
{
	ON_CALL(*units[0], alive()).WillByDefault(Return(false));
	ON_CALL(*units[1], isGhost()).WillByDefault(Return(true));
	ON_CALL(*units[2], unitSlot()).WillByDefault(Return(SlotID::COMMANDER_SLOT_PLACEHOLDER));
	EXPECT_TRUE(recipients(order(HeroCommand::RIPOSTE)).empty());
}

TEST_F(NewHorizonsEnchantedCommandTest, InvalidContextAndMissingHeroFailClosed)
{
	auto state = order(HeroCommand::CHARGE);
	EXPECT_TRUE(newHorizonsEnchantedCommand::recipientIds(callback, BattleSide::NONE, state).empty());
	EXPECT_TRUE(newHorizonsEnchantedCommand::recipientIds(callback, static_cast<BattleSide>(127), state).empty());
	state.issuedRound = 2;
	EXPECT_TRUE(recipients(state).empty());
	EXPECT_FALSE(newHorizonsEnchantedCommand::eligible(JsonNode(), nullptr, state));
	callback.state = nullptr;
	EXPECT_TRUE(recipients(state).empty());
}

TEST_F(NewHorizonsEnchantedCommandTest, GrantUsesNativeMoraleAndGenuineActivationDuration)
{
	const auto bonus = newHorizonsEnchantedCommand::moraleBonus();
	EXPECT_EQ(bonus.type, BonusType::MORALE);
	EXPECT_EQ(bonus.val, 1);
	EXPECT_EQ(bonus.duration, BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION);
	EXPECT_TRUE(newHorizonsEnchantedCommand::isMoraleBonus(&bonus));
	EXPECT_TRUE(newHorizonsEnchantedCommand::moraleBonusSelector()(&bonus));
	EXPECT_FALSE(newHorizonsEnchantedCommand::isMoraleBonus(nullptr));
	auto changed = bonus;
	changed.val = 2;
	EXPECT_FALSE(newHorizonsEnchantedCommand::isMoraleBonus(&changed));
}
