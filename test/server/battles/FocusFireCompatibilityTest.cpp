/*
 * FocusFireCompatibilityTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "FocusFireFixture.h"
#include "BattleStartSnapshotFixture.h"
#include "../../../AI/BattleAI/HypotheticBattle.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/gameState/ReplayLog.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"

namespace
{
class FocusCompatibilityEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit FocusCompatibilityEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

class FocusFireCompatibilityTest : public FocusFireFixture
{
protected:
	bool canonical = false;
	bool unsupported = false;
	std::shared_ptr<FocusCompatibilityEnvironment> environment;
	std::shared_ptr<CPlayerBattleCallback> live;

	void mapLoaded(CMap * map) override
	{
		if(canonical)
			HeroCommandFixture::mapLoaded(map);
		else
			FocusFireFixture::mapLoaded(map);
		if(unsupported)
		{
			auto rules = JsonNode(JsonPath::builtin("config/newHorizonsCombatV2"))["combat"]["heroCommands"];
			rules["rulesetVersion"].Integer() = heroCommands::RULESET_VERSION;
			rules["commands"].Struct().erase("focusFire");
			heroCommands::validateRules(rules);
			map->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, rules);
		}
	}

	void prepare()
	{
		ASSERT_NO_FATAL_FAILURE(startGame());
		ASSERT_NO_FATAL_FAILURE(startBattle());
		shooter = addShooter(BattleSide::ATTACKER, BattleHex(leftHex));
		target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 4), 100);
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		activate(shooter);
		environment = std::make_shared<FocusCompatibilityEnvironment>(gameState());
		live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	}

	std::shared_ptr<HypotheticBattle> detached()
	{
		return std::make_shared<HypotheticBattle>(environment.get(), live);
	}

	void expectPremium(const CBattleInfoCallback & view, int expected)
	{
		const auto * own = view.battleGetUnitByID(shooter->unitId());
		const auto * enemy = view.battleGetUnitByID(target->unitId());
		EXPECT_EQ(view.battleTargetedRangedCommandPercent(own, enemy, true), expected);
		EXPECT_EQ(view.battleTargetedRangedCommandPercent(own, enemy, true, true), 0);
		EXPECT_EQ(view.battleTargetedRangedCommandPercent(own, enemy, false), 0);
	}
};

TEST_F(FocusFireCompatibilityTest, AcceptedLegacyMarkWorksLiveAndDetachedWithoutAnOrder)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	ASSERT_TRUE(battle()->getHeroOrderStates(BattleSide::ATTACKER).empty());
	ASSERT_NO_THROW(battle()->validateFocusFireStates());
	EXPECT_TRUE(live->battleIsFocusFireTargetActive(BattleSide::ATTACKER));
	ASSERT_NO_FATAL_FAILURE(expectPremium(*live, 30));
	auto child = detached();
	ASSERT_TRUE(child->getHeroOrderStates(BattleSide::ATTACKER).empty());
	ASSERT_NO_FATAL_FAILURE(expectPremium(*child, 30));
	child->nextRound();
	ASSERT_NO_FATAL_FAILURE(expectPremium(*child, 0));
	ASSERT_NO_FATAL_FAILURE(expectPremium(*live, 30));
}

TEST_F(FocusFireCompatibilityTest, LegacySnapshotMembershipAndControllerStillGateThePrimary)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	auto * late = addShooter(BattleSide::ATTACKER, BattleHex(leftHex + 3));
	EXPECT_EQ(live->battleTargetedRangedCommandPercent(late, target, true), 0);
	auto child = detached();
	const Bonus control(BonusDuration::ONE_BATTLE, BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	child->addUnitBonus(target->unitId(), {control});
	EXPECT_FALSE(child->battleIsFocusFireTargetActive(BattleSide::ATTACKER));
	ASSERT_NO_FATAL_FAILURE(expectPremium(*child, 0));
	child->removeUnitBonus(target->unitId(), {control});
	ASSERT_NO_FATAL_FAILURE(expectPremium(*child, 30));
	ASSERT_NO_FATAL_FAILURE(expectPremium(*live, 30));
}

TEST_F(FocusFireCompatibilityTest, LegacyMarkRejectsPastAndFutureRoundWithoutCanonicalCarry)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	auto & mark = battle()->getSide(BattleSide::ATTACKER).focusFire;
	ASSERT_TRUE(mark);
	const auto original = *mark;
	for(const int delta : {-1, 1})
	{
		mark->issuedRound = original.issuedRound + delta;
		EXPECT_FALSE(live->battleIsFocusFireTargetActive(BattleSide::ATTACKER));
		ASSERT_NO_FATAL_FAILURE(expectPremium(*live, 0));
		EXPECT_THROW(battle()->validateFocusFireStates(), std::runtime_error);
	}
	mark = original;
	EXPECT_NO_THROW(battle()->validateFocusFireStates());
}

TEST_F(FocusFireCompatibilityTest, UnsupportedSavedRulesNeverActivateAnInjectedMark)
{
	unsupported = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	FocusFireState mark;
	mark.issuedRound = battle()->battleGetRound();
	mark.targetUnitId = target->unitId();
	mark.recipientUnitIds = {shooter->unitId()};
	mark.rangedDamagePercent = 30;
	auto & side = battle()->getSide(BattleSide::ATTACKER);
	side.focusFire = mark;
	side.activeOrder = HeroCommand::FOCUS_FIRE;
	side.heroCommandUsed = true;
	EXPECT_FALSE(live->battleIsFocusFireTargetActive(BattleSide::ATTACKER));
	ASSERT_NO_FATAL_FAILURE(expectPremium(*live, 0));
	EXPECT_THROW(battle()->validateFocusFireStates(), std::runtime_error);
}

TEST_F(FocusFireCompatibilityTest, CanonicalMarkStillRequiresItsAcceptedOrder)
{
	canonical = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	const auto mark = live->battleGetFocusFireState(BattleSide::ATTACKER);
	ASSERT_TRUE(mark);
	ASSERT_FALSE(battle()->getHeroOrderStates(BattleSide::ATTACKER).empty());
	ASSERT_NO_FATAL_FAILURE(expectPremium(*live, mark->rangedDamagePercent));
	auto child = detached();
	child->setHeroOrderStates(BattleSide::ATTACKER, {});
	EXPECT_FALSE(child->battleIsFocusFireTargetActive(BattleSide::ATTACKER));
	ASSERT_NO_FATAL_FAILURE(expectPremium(*child, 0));
	ASSERT_NO_FATAL_FAILURE(expectPremium(*live, mark->rangedDamagePercent));
	EXPECT_NO_THROW(battle()->validateFocusFireStates());
}

TEST_F(FocusFireCompatibilityTest, MarkedLegacyBattleStartRoundTripsWithoutInventingAnOrder)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	const auto mark = live->battleGetFocusFireState(BattleSide::ATTACKER);
	ASSERT_TRUE(mark);
	const auto rules = battle()->getHeroCommandRules();
	// Full in-memory BattleStart descriptor roundtrip through the maintained
	// fixture; not an ordinary ongoing-combat save/resume support claim.
	BattleStart outgoing;
	outgoing.battleID = BattleID(0);
	outgoing.info = battleStartFixture::snapshot(*battle(), gameState().get());
	CMemorySerializer wire;
	wire.oser & outgoing;
	wire.iser.cb = gameState().get();
	BattleStart incoming;
	wire.iser & incoming;
	ASSERT_NE(incoming.info, nullptr);
	EXPECT_EQ(incoming.info->getHeroCommandRules(), rules);
	EXPECT_EQ(incoming.info->battleGetFocusFireState(BattleSide::ATTACKER), mark);
	EXPECT_TRUE(incoming.info->getHeroOrderStates(BattleSide::ATTACKER).empty());
	EXPECT_NO_THROW(incoming.info->validateFocusFireStates());
	EXPECT_NO_THROW(battle()->validateFocusFireStates());
}

TEST_F(FocusFireCompatibilityTest, LegacyValidatorStillRejectsMissingMarkReferences)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	auto & mark = battle()->getSide(BattleSide::ATTACKER).focusFire;
	ASSERT_TRUE(mark);
	const auto original = *mark;
	mark->targetUnitId = static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
	EXPECT_THROW(battle()->validateFocusFireStates(), std::runtime_error);
	mark = original;
	mark->recipientUnitIds = {static_cast<uint32_t>(std::numeric_limits<int32_t>::max())};
	EXPECT_THROW(battle()->validateFocusFireStates(), std::runtime_error);
	mark = original;
	EXPECT_NO_THROW(battle()->validateFocusFireStates());
}

TEST_F(FocusFireCompatibilityTest, AcceptedLegacyCommandKeepsMarkerBudgetAndDetachedQueriesConsistent)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto budget = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	EXPECT_EQ(battle()->getActiveOrder(BattleSide::ATTACKER), HeroCommand::FOCUS_FIRE);
	EXPECT_TRUE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
	EXPECT_NE(battle()->getHeroActionAllowances(BattleSide::ATTACKER), budget);
	EXPECT_TRUE(battle()->getHeroOrderStates(BattleSide::ATTACKER).empty());
	ASSERT_TRUE(battle()->battleGetFocusFireState(BattleSide::ATTACKER));
	ASSERT_NO_THROW(battle()->validateFocusFireStates());
	auto child = detached();
	EXPECT_EQ(child->getActiveOrder(BattleSide::ATTACKER), HeroCommand::FOCUS_FIRE);
	EXPECT_TRUE(child->getHeroOrderStates(BattleSide::ATTACKER).empty());
	ASSERT_NO_FATAL_FAILURE(expectPremium(*child, 30));
	ASSERT_NO_FATAL_FAILURE(expectPremium(*live, 30));
}

TEST_F(FocusFireCompatibilityTest, ReplayCodecStartActionUsesTheSameLegacyReceiverTransition)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	StartAction outgoing;
	outgoing.battleID = BattleID(0);
	outgoing.ba = focusAction(target->unitId());
	outgoing.focusFire = battle()->battlePrepareFocusFireState(BattleSide::ATTACKER, target->unitId());
	ASSERT_TRUE(outgoing.focusFire);
	const auto budget = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	const auto bytes = ReplayPackSerializer::write(outgoing);
	auto packet = ReplayPackSerializer::read(bytes, gameState().get());
	auto * decoded = dynamic_cast<StartAction *>(packet.get());
	ASSERT_NE(decoded, nullptr);
	EXPECT_EQ(ReplayPackSerializer::write(*decoded), bytes);
	ASSERT_NO_THROW(gameState()->apply(*decoded));
	EXPECT_EQ(battle()->getActiveOrder(BattleSide::ATTACKER), HeroCommand::FOCUS_FIRE);
	EXPECT_EQ(battle()->battleGetFocusFireState(BattleSide::ATTACKER), outgoing.focusFire);
	EXPECT_TRUE(battle()->getHeroOrderStates(BattleSide::ATTACKER).empty());
	EXPECT_TRUE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
	EXPECT_NE(battle()->getHeroActionAllowances(BattleSide::ATTACKER), budget);
	EXPECT_FALSE(battle()->battleCanBeginHeroCommand(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE));
	EXPECT_NO_THROW(battle()->validateFocusFireStates());
	ASSERT_NO_FATAL_FAILURE(expectPremium(*live, 30));
}

TEST_F(FocusFireCompatibilityTest, LegacyValidationReasonsAreCheckedFromAnAcceptedValidBaseline)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	// A past context round must still be shape-valid (issuedRound >= 1).
	ASSERT_NO_FATAL_FAILURE(advanceRound());
	ASSERT_NO_FATAL_FAILURE(activate(shooter));
	ASSERT_GT(battle()->battleGetRound(), 1);
	ASSERT_TRUE(submit(focusAction(target->unitId())));
	ASSERT_NO_THROW(battle()->validateFocusFireStates());
	auto & side = battle()->getSide(BattleSide::ATTACKER);
	const auto original = side;
	ASSERT_TRUE(original.focusFire);
	ASSERT_EQ(original.focusFire->issuedRound, battle()->battleGetRound());
	ASSERT_GT(original.focusFire->issuedRound, 1);
	const auto expectFailure = [&](const char * expected)
	{
		try
		{
			battle()->validateFocusFireStates();
			ADD_FAILURE() << "Corrupted Focus Fire context was accepted";
		}
		catch(const std::runtime_error & error)
		{
			EXPECT_STREQ(error.what(), expected);
		}
	};
	for(const int invalidRound : {-1, 0})
	{
		side.focusFire->issuedRound = invalidRound;
		expectFailure("Invalid New Horizons Focus Fire state shape");
		side = original;
		ASSERT_NO_THROW(battle()->validateFocusFireStates());
	}
	for(const int delta : {-1, 1})
	{
		side.focusFire->issuedRound = original.focusFire->issuedRound + delta;
		ASSERT_NO_THROW(side.focusFire->validateShape());
		expectFailure("Invalid New Horizons Focus Fire battle context");
		side = original;
		ASSERT_NO_THROW(battle()->validateFocusFireStates());
	}
	side.focusFire->targetUnitId = static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
	expectFailure("Invalid New Horizons Focus Fire battle context");
	side = original;
	ASSERT_NO_THROW(battle()->validateFocusFireStates());
	side.focusFire->recipientUnitIds = {static_cast<uint32_t>(std::numeric_limits<int32_t>::max())};
	expectFailure("Invalid New Horizons Focus Fire recipient reference");
	side = original;
	ASSERT_NO_THROW(battle()->validateFocusFireStates());
	side.activeOrder = HeroCommand::CHARGE;
	expectFailure("Inconsistent New Horizons Focus Fire order state");
	side = original;
	EXPECT_NO_THROW(battle()->validateFocusFireStates());
	EXPECT_EQ(side.focusFire, original.focusFire);
}

TEST_F(FocusFireCompatibilityTest, AcceptedLegacyChargeKeepsItsMarkerWithoutInventingCanonicalOrders)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(submit(BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE)));
	EXPECT_EQ(battle()->getActiveOrder(BattleSide::ATTACKER), HeroCommand::CHARGE);
	EXPECT_TRUE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
	EXPECT_TRUE(battle()->getHeroOrderStates(BattleSide::ATTACKER).empty());
	EXPECT_FALSE(battle()->battleGetFocusFireState(BattleSide::ATTACKER));
	EXPECT_NO_THROW(battle()->validateFocusFireStates());
}

TEST_F(FocusFireCompatibilityTest, AcceptedLegacyHoldTheLineKeepsItsMarkerWithoutInventingCanonicalOrders)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(submit(BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE)));
	EXPECT_EQ(battle()->getActiveOrder(BattleSide::ATTACKER), HeroCommand::HOLD_THE_LINE);
	EXPECT_TRUE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
	EXPECT_TRUE(battle()->getHeroOrderStates(BattleSide::ATTACKER).empty());
	EXPECT_FALSE(battle()->battleGetFocusFireState(BattleSide::ATTACKER));
	EXPECT_NO_THROW(battle()->validateFocusFireStates());
}
