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
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/HeroActionAdmission.h"
#include "HeroQueueSelectorFixture.h"
#include "../../lib/battle/HeroCommandRevision.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../AI/BattleAI/BattleDamageValue.h"
#include "../../AI/BattleAI/HeroOrderCandidateRules.h"
#include <tuple>
#include "../../lib/GameSettings.h"
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

// This oracle restores the removed eager prefix with real public damage queries.
// It deliberately does not intercept or replace the global calculator service.
// Its 7x7 traversal is source characterization, not a runtime call counter.
class LazyChargeValueTest : public HeroCommandFixture
{
protected:
	using Value = heroOrderValue::DirectOrderValue;
	using Route = std::pair<HeroCommand, std::vector<uint32_t>>;
	using Row = std::tuple<HeroCommand, std::vector<uint32_t>, bool, uint32_t, uint32_t>;
	std::vector<const CStack *> friends;
	std::vector<const CStack *> enemies;
	CStack * protector = nullptr;
	CStack * spent = nullptr;
	CStack * flankTarget = nullptr;

	void prepareSevenBySeven()
	{
		startGame();
		startBattle();
		for(int row = 1; row <= 6; ++row)
		{
			auto * own = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"),
				BattleHex(3, row), row + 1);
			auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"),
				BattleHex(row == 1 ? 4 : 12, row), row + 2);
			ASSERT_NE(own, nullptr);
			ASSERT_NE(enemy, nullptr);
			if(row == 1)
			{
				protector = own;
				flankTarget = enemy;
			}
			if(row == 2)
				spent = own;
		}
		beginCombat();
		for(auto * stack : battle()->battleGetAllStacks(false))
		{
			if(stack->alive() && !stack->isGhost() && !stack->isTurret())
				(stack->unitSide() == BattleSide::ATTACKER ? friends : enemies).push_back(stack);
		}
		ASSERT_EQ(friends.size(), 7u);
		ASSERT_EQ(enemies.size(), 7u);
		for(const auto * stack : friends)
			ASSERT_TRUE(stack->isMeleeAttacker());
		for(const auto * stack : enemies)
			ASSERT_TRUE(stack->isMeleeAttacker());
		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = friends.front()->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
		// Match the established SecondWindOpportunity fixture's spent-state setup.
		ASSERT_NE(friends.front()->unitId(), spent->unitId());
		spent->movedThisRound = true;
		ASSERT_TRUE(spent->moved());
		ASSERT_TRUE(heroOrderCandidates::adjacentForProtect(protector, spent));
		for(const auto & route : positiveRoutes())
			ASSERT_TRUE(heroOrderCandidates::commandTargetIsLegal(*battle(),
				BattleSide::ATTACKER, route.first, route.second));
	}

	float eagerPrefix(const heroOrderValue::CanonicalOrderValue & value) const
	{
		float total = 0.0f;
		for(const auto * own : value.ownUnits)
		{
			float best = 0.0f;
			for(const auto * enemy : value.enemyUnits)
				best = std::max(best, value.meleeDamage(own, enemy));
			total += best;
		}
		return total;
	}

	Value read(const Route & route, bool eager) const
	{
		heroOrderValue::CanonicalOrderValue value(*battle(), BattleSide::ATTACKER,
			route.first, route.second);
		// Charge already retains its eager prefix at the original source position.
		// Do not accidentally run its matrix twice in the eager oracle.
		if(eager && route.first != HeroCommand::CHARGE)
			(void)eagerPrefix(value);
		return value.directValue(37);
	}

	static Row row(const Route & route, const Value & value)
	{
		return {route.first, route.second, value.direct.has_value(),
			std::bit_cast<uint32_t>(value.direct.value_or(0.0f)),
			std::bit_cast<uint32_t>(value.holdPercent)};
	}

	std::vector<Route> positiveRoutes() const
	{
		return {{HeroCommand::PROTECT, {protector->unitId(), spent->unitId()}},
			{HeroCommand::FLANK, {flankTarget->unitId()}},
			{HeroCommand::SECOND_WIND, {spent->unitId()}}};
	}

	std::vector<Route> routes() const
	{
		const auto own = friends.front()->unitId();
		const auto enemy = enemies.front()->unitId();
		return {{HeroCommand::RIPOSTE, {}}, {HeroCommand::HOLD_THE_LINE, {}},
			{HeroCommand::FOCUS_FIRE, {enemy}}, {HeroCommand::BRACE, {}},
			{HeroCommand::PROTECT, {protector->unitId(), spent->unitId()}},
			{HeroCommand::FLANK, {flankTarget->unitId()}},
			{HeroCommand::SECOND_WIND, {spent->unitId()}},
			{HeroCommand::CHARGE, {}}, {HeroCommand::FOCUS_FIRE, {}},
			{HeroCommand::PROTECT, {own, own}},
			{HeroCommand::FLANK, {std::numeric_limits<uint32_t>::max()}}};
	}

	auto authoritySnapshot()
	{
		return std::tuple{serialized(*gameState()), serialized(*gameHandler->randomizer),
			serialized(dynamic_cast<CRandomGenerator &>(*getRNG())),
			server.startedActions.size(), server.stackActivations.size(),
			server.orderStateUpdates.size(), server.attacks.size(), server.injuries.size(),
			server.battleLogLines.size()};
	}

	std::vector<Route> legalRoutes() const
	{
		std::vector<Route> result;
		for(const auto command : {HeroCommand::CHARGE, HeroCommand::HOLD_THE_LINE,
			HeroCommand::RIPOSTE, HeroCommand::BRACE})
			if(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, command))
				result.emplace_back(command, std::vector<uint32_t>{});
		for(const auto command : {HeroCommand::FOCUS_FIRE, HeroCommand::PROTECT,
			HeroCommand::FLANK, HeroCommand::SECOND_WIND})
			for(const auto & ids : heroOrderCandidates::orderTargetOptions(
				*battle(), BattleSide::ATTACKER, command))
				result.emplace_back(command, ids);
		return result;
	}

	void compareRoutes(bool eagerFirst)
	{
		const auto before = authoritySnapshot();
		const auto positive = positiveRoutes();
		for(const auto & route : routes())
		{
			SCOPED_TRACE(static_cast<int>(route.first));
			const auto legalBefore = heroOrderCandidates::commandTargetIsLegal(
				*battle(), BattleSide::ATTACKER, route.first, route.second);
			const auto first = read(route, eagerFirst);
			if(std::find(positive.begin(), positive.end(), route) != positive.end())
			{
				ASSERT_TRUE(legalBefore);
				ASSERT_TRUE(first.direct);
				ASSERT_GT(*first.direct, 0.0f);
			}
			const auto second = read(route, !eagerFirst);
			EXPECT_EQ(row(route, first), row(route, second));
			EXPECT_EQ(heroOrderCandidates::commandTargetIsLegal(*battle(),
				BattleSide::ATTACKER, route.first, route.second), legalBefore);
		}
		const auto legalBefore = legalRoutes();
		for(const auto & route : positive)
			ASSERT_NE(std::find(legalBefore.begin(), legalBefore.end(), route), legalBefore.end());
		for(const auto & route : legalBefore)
			EXPECT_EQ(row(route, read(route, false)), row(route, read(route, true)));
		EXPECT_EQ(legalRoutes(), legalBefore);
		EXPECT_EQ(authoritySnapshot(), before);
	}

	void checkChargeOracle()
	{
		const std::vector<uint32_t> ids;
		heroOrderValue::CanonicalOrderValue value(*battle(), BattleSide::ATTACKER,
			HeroCommand::CHARGE, ids);
		ASSERT_TRUE(std::any_of(value.ownUnits.begin(), value.ownUnits.end(), [](const auto * unit)
		{
			return unit->isMeleeAttacker() && unit->getMovementRange(0) >= 3;
		}));
		const auto prepared = battle()->battlePrepareHeroOrderState(
			BattleSide::ATTACKER, HeroCommand::CHARGE, ids);
		ASSERT_TRUE(prepared);
		ASSERT_EQ(prepared->warcastingBonusPercent, 0);
		ASSERT_EQ(prepared->divineMandateEfficiencyBonusPercent(), 0);
		const auto & formula = battle()->getHeroCommandRules()["commands"]["charge"]
			["effects"]["meleeDamagePercent"];
		const auto expected = eagerPrefix(value)
			* static_cast<float>(heroCommands::coefficient(formula, *attackerSideHero)) / 100.0f;
		const auto actual = value.directValue(std::nullopt);
		ASSERT_TRUE(actual.direct);
		EXPECT_EQ(std::bit_cast<uint32_t>(*actual.direct), std::bit_cast<uint32_t>(expected));
	}
};

TEST_F(LazyChargeValueTest, SevenBySevenFreshLazyFirst)
{
	ASSERT_NO_FATAL_FAILURE(prepareSevenBySeven());
	ASSERT_NO_FATAL_FAILURE(compareRoutes(false));
	ASSERT_NO_FATAL_FAILURE(checkChargeOracle());
}

TEST_F(LazyChargeValueTest, SevenBySevenFreshEagerFirstThenWarmAndBonusInvalidation)
{
	ASSERT_NO_FATAL_FAILURE(prepareSevenBySeven());
	ASSERT_NO_FATAL_FAILURE(compareRoutes(true));
	ASSERT_NO_FATAL_FAILURE(compareRoutes(false));
	auto bonus = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL,
		BonusSource::OTHER, 7, BonusSourceID(), BonusSubtypeID(PrimarySkill::ATTACK));
	auto * changed = battle()->getStack(friends.back()->unitId());
	changed->addNewBonus(bonus);
	ASSERT_NO_FATAL_FAILURE(compareRoutes(false));
	ASSERT_NO_FATAL_FAILURE(checkChargeOracle());
	changed->removeBonus(bonus);
	ASSERT_NO_FATAL_FAILURE(compareRoutes(true));
	ASSERT_NO_FATAL_FAILURE(checkChargeOracle());
}

TEST_F(CanonicalOrderValueTest, RevisedFeintUsesOnlyExpectedEnemyRetaliation)
{
	useRevisedOrders = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const std::vector<uint32_t> noTargets;
	heroOrderValue::CanonicalOrderValue value(*battle(), BattleSide::ATTACKER, HeroCommand::FEINT, noTargets);
	const auto prepared = battle()->battlePrepareHeroOrderState(BattleSide::ATTACKER, HeroCommand::FEINT, {});
	ASSERT_TRUE(prepared && prepared->revision);
	float expected = 0.0f;
	for(const auto * own : value.ownUnits)
	{
		if(!own->willMove(0) || !own->isMeleeAttacker())
			continue;
		float best = 0.0f;
		for(const auto * enemy : value.enemyUnits)
		{
			DamageEstimation counter;
			battle()->battleEstimateDamage(BattleAttackInfo(own, enemy, 0, false), &counter);
			best = std::max(best, battleDamageValue::averageOrderDamage(counter));
		}
		expected += best;
	}
	const auto actual = value.directValue(std::nullopt).direct;
	ASSERT_TRUE(actual);
	EXPECT_FLOAT_EQ(*actual, expected * prepared->revision->feintReductionPercent / 100.0f);
}

TEST_F(CanonicalOrderValueTest, RevisedVanguardAdmissionUsesCapturedEligibleRecipients)
{
	useRevisedOrders = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const std::vector<uint32_t> noTargets;
	heroOrderValue::CanonicalOrderValue value(*battle(), BattleSide::ATTACKER, HeroCommand::VANGUARD, noTargets);
	const auto prepared = battle()->battlePrepareHeroOrderState(BattleSide::ATTACKER, HeroCommand::VANGUARD, {});
	ASSERT_TRUE(prepared && prepared->revision);
	const auto actual = value.directValue(std::nullopt).direct;
	ASSERT_TRUE(actual);
	EXPECT_GT(*actual, 0.0f);
	EXPECT_FLOAT_EQ(*actual, static_cast<float>(prepared->revision->recipientUnitIds.size()
		* prepared->revision->vanguardStatBonus));
}

// Revised-only final utility is a finite stat forecast, not the historical raw
// Order admission prior. Existing direct-value and historical tests stay above.
class RevisedArmyOrderValueTest : public heroQueueTest::HeroQueueSelectorTest
{
protected:
	static constexpr auto side = BattleSide::ATTACKER;
	void prepare(const char * ownKey = "core:pikeman")
	{
		useRevisedOrders = true;
		ASSERT_NO_FATAL_FAILURE(world());
		for(auto * hero : {attackerSideHero, defenderSideHero})
		{
			hero->removeAllSpells();
			if(hero->hasSpellbook()) hero->removeArtifact(ArtifactPosition::SPELLBOOK);
		}
		const auto capacity = attackerSideHero->getLeadershipCapacity();
		ASSERT_TRUE(capacity);
		attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::LEADERSHIP, BonusSource::OTHER, static_cast<int>(2250 - capacity->capacity), BonusSourceID()));
		ASSERT_NO_FATAL_FAILURE(startBattle());
		BattleUnitsChanged clear;
		clear.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			clear.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(clear);
		active = addStack(side, creatureByName(ownKey), BattleHex(7, 5), 30);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(8, 5), 30);
		ASSERT_NE(active, nullptr);
		ASSERT_NE(enemy, nullptr);
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		ASSERT_NO_FATAL_FAILURE(activate(active));
		effect(active, Bonus(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE,
			BonusSource::OTHER, 100, BonusSourceID(), BonusSubtypeID(), BonusValueType::BASE_NUMBER));
		callback = std::make_shared<heroQueueTest::QueueCallback>();
		callback->onBattleStarted(battle());
		live = callback->getBattle(BattleID(0));
		environment = std::make_shared<heroQueueTest::QueueEnvironment>(gameState());
	}

	void expectVanguardUnavailableWithoutMutation()
	{
		const auto before = state(*live);
		const auto rng = heroQueueTest::wire(*gameHandler->randomizer);
		EXPECT_FALSE(live->battlePrepareHeroOrderState(side, HeroCommand::VANGUARD, {}));
		EXPECT_FALSE(live->battleCanUseHeroCommand(side, HeroCommand::VANGUARD));
		EXPECT_FALSE(issue(HeroCommand::VANGUARD));
		EXPECT_EQ(state(*live), before);
		EXPECT_EQ(heroQueueTest::wire(*gameHandler->randomizer), rng);
	}

	std::shared_ptr<HypotheticBattle> project(const std::shared_ptr<CBattleInfoCallback> & source,
		HeroCommand command)
	{
		auto result = std::make_shared<HypotheticBattle>(environment.get(), source);
		PossibleSpellcast candidate;
		candidate.command = command;
		const auto admission = heroActionProjection::admitCandidate(*result, side, candidate);
		EXPECT_TRUE(admission.admitted);
		if(!admission.admitted || !admission.orderEffects || !admission.orderAllowance)
			return {};
		EXPECT_TRUE(result->projectAcceptedHeroOrder(side, *admission.orderEffects, *admission.orderAllowance));
		return result;
	}
};

TEST_F(RevisedArmyOrderValueTest, FeintPricesActualRetaliationAndPreservesNativeStateAndRng)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto coldEnemy = enemy->acquireState()->save();
	ASSERT_FALSE(coldEnemy["state"]["counterAttacks"].Struct().contains("totalCache"));
	const auto source = state(*live);
	const auto rng = heroQueueTest::wire(*gameHandler->randomizer);
	auto projected = project(live, HeroCommand::FEINT);
	ASSERT_TRUE(projected);
	auto reference = std::make_shared<HypotheticBattle>(environment.get(), live);
	for(const auto * unit : live->battleGetAllUnits(false))
	{
		reference->getForUpdate(unit->unitId());
		projected->getForUpdate(unit->unitId());
	}
	DamageEstimation beforeCounter, afterCounter;
	reference->battleEstimateDamage(BattleAttackInfo(reference->battleGetUnitByID(active->unitId()),
		reference->battleGetUnitByID(enemy->unitId()), 0, false), &beforeCounter);
	projected->battleEstimateDamage(BattleAttackInfo(projected->battleGetUnitByID(active->unitId()),
		projected->battleGetUnitByID(enemy->unitId()), 0, false), &afterCounter);
	ASSERT_GT(battleDamageValue::averageOrderDamage(beforeCounter), battleDamageValue::averageOrderDamage(afterCounter));
	DamageCache fixed;
	const float expected = AttackPossibility::calculateDamageReduce(reference->battleGetUnitByID(enemy->unitId()),
		reference->battleGetUnitByID(active->unitId()), battleDamageValue::averageOrderDamage(beforeCounter), fixed, reference)
		- AttackPossibility::calculateDamageReduce(reference->battleGetUnitByID(enemy->unitId()),
			reference->battleGetUnitByID(active->unitId()), battleDamageValue::averageOrderDamage(afterCounter), fixed, reference);
	ASSERT_GT(expected, 0.0f);
	EXPECT_FLOAT_EQ(heroOrderValue::revisedArmyUtility(live, projected, side, HeroCommand::FEINT), expected);
	EXPECT_EQ(state(*live), source);
	EXPECT_EQ(enemy->acquireState()->save(), coldEnemy);
	EXPECT_EQ(heroQueueTest::wire(*gameHandler->randomizer), rng);
}

TEST_F(RevisedArmyOrderValueTest, FeintIsWorthZeroWhenTheVisibleEnemyCannotRetaliate)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	effect(enemy, Bonus(BonusDuration::ONE_BATTLE, BonusType::NO_RETALIATION, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_FALSE(enemy->ableToRetaliate());
	auto projected = project(live, HeroCommand::FEINT);
	ASSERT_TRUE(projected);
	EXPECT_FLOAT_EQ(heroOrderValue::revisedArmyUtility(live, projected, side, HeroCommand::FEINT), 0.0f);
}

TEST_F(RevisedArmyOrderValueTest, VanguardValuesEveryComponentAndExcludesUndeadRecipients)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * undead = addStack(side, creatureByName("core:skeleton"), BattleHex(4, 5), 1);
	ASSERT_NE(undead, nullptr);
	auto projected = project(live, HeroCommand::VANGUARD);
	ASSERT_TRUE(projected);
	ASSERT_FALSE(heroCommands::activeVanguardBonuses(projected->battleGetUnitByID(undead->unitId())));
	const auto * grant = projected->battleGetUnitByID(active->unitId());
	ASSERT_TRUE(heroCommands::activeVanguardBonuses(grant));
	ASSERT_GT(projected->battleGetMorale(grant), live->battleGetMorale(active));
	ASSERT_GT(grant->getInitiative(), active->getInitiative());
	const auto full = heroOrderValue::revisedArmyUtility(live, projected, side, HeroCommand::VANGUARD);
	ASSERT_GT(full, 0.0f);
	for(const auto & component : std::vector<CSelector>{
		Selector::typeSubtype(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::ATTACK)),
		Selector::typeSubtype(BonusType::PRIMARY_SKILL, BonusSubtypeID(PrimarySkill::DEFENSE)),
		Selector::type()(BonusType::MORALE), Selector::type()(BonusType::STACKS_INITIATIVE)})
	{
		auto partial = std::make_shared<HypotheticBattle>(environment.get(), projected);
		partial->getForUpdate(active->unitId())->removeUnitBonus(
			CSelector(heroCommands::isVanguardBonus).And(component));
		EXPECT_LT(heroOrderValue::revisedArmyUtility(live, partial, side, HeroCommand::VANGUARD), full);
	}
}

TEST_F(RevisedArmyOrderValueTest, VanguardIdenticalRefreshHasZeroValueAndShorterGrantHasOnlyAddedLifetime)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto first = project(live, HeroCommand::VANGUARD);
	ASSERT_TRUE(first);
	const auto fullGrant = heroCommands::activeVanguardBonuses(first->battleGetUnitByID(active->unitId()));
	ASSERT_TRUE(fullGrant);
	ASSERT_EQ(fullGrant->roundsRemaining, 3);
	auto existing = std::make_shared<HypotheticBattle>(environment.get(), live);
	std::vector<Bonus> bonuses;
	const auto grantedBonuses = first->battleGetUnitByID(active->unitId())->getBonuses(CSelector(heroCommands::isVanguardBonus));
	for(const auto & bonus : *grantedBonuses)
		bonuses.push_back(*bonus);
	existing->getForUpdate(active->unitId())->addUnitBonus(bonuses);
	auto same = project(existing, HeroCommand::VANGUARD);
	ASSERT_TRUE(same);
	EXPECT_NEAR(heroOrderValue::revisedArmyUtility(existing, same, side, HeroCommand::VANGUARD), 0.0f, 0.0001f);
	for(auto & bonus : bonuses) bonus.turnsRemain = 1;
	// HERO_COMMAND updates do not shorten captured snapshots: replace this
	// query fixture's old grant explicitly, retaining the native grant payload.
	existing->getForUpdate(active->unitId())->removeUnitBonus(CSelector(heroCommands::isVanguardBonus));
	existing->getForUpdate(active->unitId())->addUnitBonus(bonuses);
	const auto remaining = heroCommands::activeVanguardBonuses(existing->battleGetUnitByID(active->unitId()));
	ASSERT_TRUE(remaining);
	ASSERT_EQ(remaining->roundsRemaining, 1);
	auto renewed = project(existing, HeroCommand::VANGUARD);
	ASSERT_TRUE(renewed);
	const auto refreshed = heroCommands::activeVanguardBonuses(renewed->battleGetUnitByID(active->unitId()));
	ASSERT_TRUE(refreshed);
	ASSERT_EQ(refreshed->roundsRemaining, 3);
	const auto freshValue = heroOrderValue::revisedArmyUtility(live, first, side, HeroCommand::VANGUARD);
	EXPECT_NEAR(heroOrderValue::revisedArmyUtility(existing, renewed, side, HeroCommand::VANGUARD),
		freshValue * 2.0f / 3.0f, std::max(0.0001f, freshValue * 0.0001f));
}

TEST_F(RevisedArmyOrderValueTest, HistoricalAndRetainedOrdersCannotEnterRevisedFinalScoring)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(const auto command : {HeroCommand::CHARGE, HeroCommand::HOLD_THE_LINE, HeroCommand::FOCUS_FIRE,
		HeroCommand::RIPOSTE, HeroCommand::BRACE, HeroCommand::SECOND_WIND, HeroCommand::PROTECT, HeroCommand::FLANK})
		EXPECT_FALSE(heroOrderValue::usesRevisedArmyHeuristic(*live, command));
	EXPECT_TRUE(heroOrderValue::usesRevisedArmyHeuristic(*live, HeroCommand::FEINT));
	EXPECT_TRUE(heroOrderValue::usesRevisedArmyHeuristic(*live, HeroCommand::VANGUARD));
}

TEST_F(CanonicalOrderValueTest, HistoricalCapturedRulesRejectTheModernFinalUtilityPath)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_FALSE(heroCommands::isRevisedRules(battle()->getHeroCommandRules()));
	EXPECT_FALSE(heroOrderValue::usesRevisedArmyHeuristic(*battle(), HeroCommand::FEINT));
	EXPECT_FALSE(heroOrderValue::usesRevisedArmyHeuristic(*battle(), HeroCommand::VANGUARD));
}

TEST_F(RevisedArmyOrderValueTest, VanguardCannotBeIssuedForAnAllUndeadArmy)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:skeleton"));
	ASSERT_TRUE(active->hasBonusOfType(BonusType::UNDEAD));
	ASSERT_NO_FATAL_FAILURE(expectVanguardUnavailableWithoutMutation());
}

TEST_F(RevisedArmyOrderValueTest, VanguardCannotBeIssuedForAnAllMechanicalArmy)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	effect(active, Bonus(BonusDuration::ONE_BATTLE, BonusType::MECHANICAL, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(active->hasBonusOfType(BonusType::MECHANICAL));
	ASSERT_NO_FATAL_FAILURE(expectVanguardUnavailableWithoutMutation());
}

TEST_F(RevisedArmyOrderValueTest, VanguardCannotTargetStoppedLivingUnitsWhileAnUndeadAllyIsActive)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * control = addStack(side, creatureByName("core:skeleton"), BattleHex(6, 5), 1);
	ASSERT_NE(control, nullptr);
	ASSERT_NO_FATAL_FAILURE(activate(control));
	Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	stopped.parameters = std::make_shared<BonusParameters>(static_cast<int32_t>(side));
	battle()->addOrUpdateUnitBonus(active, stopped, true);
	ASSERT_TRUE(active->isTimeStopped());
	ASSERT_FALSE(control->isTimeStopped());
	ASSERT_FALSE(heroCommands::isVanguardCreature(active));
	ASSERT_FALSE(heroCommands::isVanguardCreature(control));
	ASSERT_NO_FATAL_FAILURE(expectVanguardUnavailableWithoutMutation());
}
