/*
 * LegacyHeroOrderProjectionTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "HeroQueueBonusSnapshot.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/BonusCustomTypes.h"
#include "../../AI/BattleAI/HeroActionAdmission.h"
#include "../../lib/battle/CPlayerBattleCallback.h"

class LegacyHeroOrderProjectionTest : public heroQueueTest::HeroQueueSelectorTest
{
protected:
	virtual int version() const { return 2; }

	void mapLoaded(CMap * map) override
	{
		HeroQueueSelectorTest::mapLoaded(map);
		auto rules = JsonNode(JsonPath::builtin("config/newHorizonsCombatV2"))["combat"]["heroCommands"];
		rules["rulesetVersion"].Integer() = version();
		if(version() == 1)
			rules["commands"].Struct().erase("focusFire");
		if(version() == 3)
			for(const auto * name : {"advance", "aggressive", "defensive"})
				rules["commands"].Struct().erase(name);
		heroCommands::validateRules(rules);
		map->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, rules);
	}

	void prepare(bool shooter = false)
	{
		ASSERT_NO_FATAL_FAILURE(startGame());
		ASSERT_NO_FATAL_FAILURE(startBattle());
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		active = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 100);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:angel"),
			shooter ? BattleHex(80) : BattleHex(71), 100);
		if(shooter)
		{
			effect(active, Bonus(BonusDuration::ONE_BATTLE, BonusType::SHOOTER, BonusSource::OTHER, 1, BonusSourceID()));
			effect(active, Bonus(BonusDuration::ONE_BATTLE, BonusType::SHOTS, BonusSource::OTHER, 10, BonusSourceID()));
		}
		ASSERT_NO_FATAL_FAILURE(activate(active));
		ASSERT_FALSE(heroCommands::isCanonicalRules(battle()->getHeroCommandRules()));
		callback = std::make_shared<heroQueueTest::QueueCallback>();
		callback->onBattleStarted(battle());
		live = callback->getBattle(BattleID(0));
		environment = std::make_shared<heroQueueTest::QueueEnvironment>(gameState());
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	}


	void captureLegacyOracle(const CBattleInfoCallback & view, HeroCommand command, std::string & result)
	{
		result = state(view);
		if(command == HeroCommand::FOCUS_FIRE)
			return;
		const auto grants = heroCommands::bonuses(view.getBattle()->getHeroCommandRules(), command,
			*view.battleGetFightingHero(BattleSide::ATTACKER));
		ASSERT_EQ(grants.size(), 1);
		const auto & grant = grants.front();
		ASSERT_TRUE(grant.stacking.empty());
		ASSERT_EQ(grant.valType, BonusValueType::ADDITIVE_VALUE);
		ASSERT_EQ(grant.type, command == HeroCommand::CHARGE
			? BonusType::PERCENTAGE_DAMAGE_BOOST : BonusType::GENERAL_DAMAGE_REDUCTION);
		ASSERT_EQ(grant.subtype, BonusSubtypeID(command == HeroCommand::CHARGE
			? BonusCustomSubtype::damageTypeMelee : BonusCustomSubtype::damageTypeAll));
		ASSERT_FALSE(grant.limiter || grant.updater || grant.propagator || grant.propagationUpdater || grant.parameters);
		BonusList single;
		single.push_back(std::make_shared<Bonus>(grant));
		const auto expected = heroQueueTest::bonusSnapshots(single).front();
		std::vector<int64_t> effective;
		const auto units = view.battleGetAllUnits(false);
		for(const auto * unit : units)
		{
			auto snapshots = heroQueueTest::bonusSnapshots(*unit->getAllBonuses(Selector::all));
			const auto original = heroQueueTest::wire(snapshots);
			const bool recipient = view.battleGetOwner(unit) == PlayerColor(0)
				&& unit->alive() && !unit->isTurret() && !unit->hasBonusOfType(BonusType::SIEGE_WEAPON);
			ASSERT_EQ(std::ranges::count(snapshots, expected), recipient ? 1 : 0);
			if(recipient)
			{
				// Only this single plain additive Order grant may change position.
				// Keep every other opaque bonus byte, duplicate and relative order.
				std::erase(snapshots, expected);
				const auto offset = result.find(original);
				ASSERT_NE(offset, std::string::npos);
				result.replace(offset, original.size(), heroQueueTest::wire(snapshots));
			}
			effective.insert(effective.end(), {unit->unitId(), unit->getAttack(false), unit->getDefense(false),
				unit->valOfBonuses(BonusType::PERCENTAGE_DAMAGE_BOOST),
				unit->valOfBonuses(BonusType::GENERAL_DAMAGE_REDUCTION)});
			for(const auto * other : units)
			{
				if(view.battleGetOwner(unit) == view.battleGetOwner(other))
					continue;
				const auto damage = view.calculateDmgRange(BattleAttackInfo(unit, other, 0, false));
				effective.push_back(damage.damage.min);
				effective.push_back(damage.damage.max);
			}
		}
		result += heroQueueTest::wire(effective);
	}

	void projectAndCompare(HeroCommand command)
	{
		ASSERT_NO_FATAL_FAILURE(prepare(command == HeroCommand::FOCUS_FIRE));
		PossibleSpellcast candidate;
		candidate.command = command;
		if(command == HeroCommand::FOCUS_FIRE)
			candidate.commandTargets = {enemy->unitId()};
		const auto original = observation();
		const auto mark = command == HeroCommand::FOCUS_FIRE
			? child->battlePrepareFocusFireState(BattleSide::ATTACKER, enemy->unitId()) : std::nullopt;
		const auto admission = heroActionProjection::admitCandidate(*child, BattleSide::ATTACKER, candidate);
		ASSERT_TRUE(admission.admitted);
		ASSERT_TRUE(admission.legacyOrder);
		ASSERT_TRUE(admission.orderAllowance);
		EXPECT_FALSE(admission.orderEffects);
		EXPECT_FALSE(child->projectAcceptedHeroOrder(BattleSide::ATTACKER, *admission.orderAllowance));
		ASSERT_TRUE(child->projectAcceptedHeroOrder(BattleSide::ATTACKER,
			command, candidate.commandTargets, *admission.orderAllowance));
		EXPECT_TRUE(child->getHeroOrderStates(BattleSide::ATTACKER).empty());
		EXPECT_EQ(child->getFocusFireState(BattleSide::ATTACKER), mark);
		EXPECT_EQ(child->getHeroActionAllowances(BattleSide::ATTACKER), admission.orderAllowance->allowancesAfter);
		const auto projected = state(*child);
		std::string projectedOracle;
		ASSERT_NO_FATAL_FAILURE(captureLegacyOracle(*child, command, projectedOracle));
		EXPECT_FALSE(child->projectAcceptedHeroOrder(BattleSide::ATTACKER,
			command, candidate.commandTargets, *admission.orderAllowance));
		EXPECT_EQ(state(*child), projected);
		EXPECT_EQ(observation(), original); // Live/parent/sibling/mana/RNG/traffic remain untouched.
		child.reset();
		sibling.reset();
		parent.reset();
		live.reset();
		callback.reset();
		auto action = BattleAction::makeHeroCommand(BattleSide::ATTACKER, command);
		for(const auto id : candidate.commandTargets)
			action.target.push_back({static_cast<int32_t>(id), BattleHex::INVALID});
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		std::string nativeOracle;
		ASSERT_NO_FATAL_FAILURE(captureLegacyOracle(*battle(), command, nativeOracle));
		EXPECT_EQ(nativeOracle, projectedOracle);
		EXPECT_EQ(battle()->getFocusFireState(BattleSide::ATTACKER), mark);
		EXPECT_TRUE(battle()->getHeroOrderStates(BattleSide::ATTACKER).empty());
	}

	void chooseAndAccept()
	{
		ASSERT_NO_FATAL_FAILURE(prepare());
		ASSERT_FALSE(attackerSideHero->hasSpellbook());
		const auto original = observation();
		{
			BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0),
				BattleSide::ATTACKER, 1.0f, 2);
			evaluator.selectStackAction(active);
			ASSERT_TRUE(evaluator.attemptCastingSpell(active)); // Runs the shared post-Order continuation.
		}
		ASSERT_EQ(callback->heroRequests.size(), 1);
		const auto action = callback->heroRequests.front();
		EXPECT_EQ(action.actionType, EActionType::HERO_COMMAND);
		EXPECT_TRUE(action.command == HeroCommand::CHARGE || action.command == HeroCommand::HOLD_THE_LINE);
		EXPECT_EQ(observation(), original);
		child.reset();
		sibling.reset();
		parent.reset();
		live.reset();
		callback.reset();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		EXPECT_TRUE(battle()->getHeroOrderStates(BattleSide::ATTACKER).empty());
	}
};

class LegacyHeroOrderV1Test : public LegacyHeroOrderProjectionTest
{
	int version() const override { return 1; }
};

class LegacyHeroOrderEarlyV3Test : public LegacyHeroOrderProjectionTest
{
	int version() const override { return 3; }
};

TEST_F(LegacyHeroOrderProjectionTest, ChargeKeepsSavedBonusAndOnePaymentWithoutCanonicalOrder)
{
	ASSERT_NO_FATAL_FAILURE(projectAndCompare(HeroCommand::CHARGE));
}

TEST_F(LegacyHeroOrderProjectionTest, HoldKeepsSavedBonusAndOnePaymentWithoutCanonicalOrder)
{
	ASSERT_NO_FATAL_FAILURE(projectAndCompare(HeroCommand::HOLD_THE_LINE));
}

TEST_F(LegacyHeroOrderProjectionTest, FocusFireKeepsLegacyMarkWithoutCanonicalOrder)
{
	ASSERT_NO_FATAL_FAILURE(projectAndCompare(HeroCommand::FOCUS_FIRE));
}

TEST_F(LegacyHeroOrderProjectionTest, ActualEvaluatorRetainsBeneficialSavedRuleOrder)
{
	ASSERT_NO_FATAL_FAILURE(chooseAndAccept());
}

TEST_F(LegacyHeroOrderV1Test, ChargeKeepsOriginalSavedRuleBonus)
{
	ASSERT_NO_FATAL_FAILURE(projectAndCompare(HeroCommand::CHARGE));
}

TEST_F(LegacyHeroOrderV1Test, HoldKeepsOriginalSavedRuleBonus)
{
	ASSERT_NO_FATAL_FAILURE(projectAndCompare(HeroCommand::HOLD_THE_LINE));
}

TEST_F(LegacyHeroOrderV1Test, ActualEvaluatorRetainsOriginalSavedRuleOrders)
{
	ASSERT_NO_FATAL_FAILURE(chooseAndAccept());
}

TEST_F(LegacyHeroOrderEarlyV3Test, ChargeDoesNotInventCanonicalEightOrderRules)
{
	ASSERT_NO_FATAL_FAILURE(projectAndCompare(HeroCommand::CHARGE));
}

TEST_F(LegacyHeroOrderEarlyV3Test, HoldDoesNotInventCanonicalEightOrderRules)
{
	ASSERT_NO_FATAL_FAILURE(projectAndCompare(HeroCommand::HOLD_THE_LINE));
}

TEST_F(LegacyHeroOrderEarlyV3Test, FocusFireRemainsLegacyInThreeOrderSnapshot)
{
	ASSERT_NO_FATAL_FAILURE(projectAndCompare(HeroCommand::FOCUS_FIRE));
}

TEST_F(LegacyHeroOrderEarlyV3Test, ActualEvaluatorRetainsThreeOrderSnapshot)
{
	ASSERT_NO_FATAL_FAILURE(chooseAndAccept());
}

TEST_F(LegacyHeroOrderProjectionTest, InvalidLegacyTargetRefusesBeforeCleanupAndKeepsLegitimateAllowance)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	const auto prepared = child->prepareHeroOrderAllowance(BattleSide::ATTACKER);
	ASSERT_TRUE(prepared);
	const auto before = state(*child);
	EXPECT_FALSE(child->beginProjectedLegacyHeroOrder(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE,
		{active->unitId()}, *prepared));
	EXPECT_EQ(state(*child), before);
	ASSERT_TRUE(child->beginProjectedLegacyHeroOrder(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE,
		{enemy->unitId()}, *prepared));
	EXPECT_FALSE(child->projectAcceptedHeroOrder(BattleSide::ATTACKER, HeroCommand::CHARGE, {}, *prepared));
	EXPECT_FALSE(sibling->projectAcceptedHeroOrder(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE,
		{enemy->unitId()}, *prepared));
	EXPECT_TRUE(child->projectAcceptedHeroOrder(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE,
		{enemy->unitId()}, *prepared));
}
