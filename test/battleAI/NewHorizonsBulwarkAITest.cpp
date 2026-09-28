/*
 * NewHorizonsBulwarkAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../server/battles/BattleTestFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/battle/NewHorizonsBulwark.h"
#include "../../lib/CStack.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../server/CGameHandler.h"

namespace
{
class BulwarkAIEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit BulwarkAIEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class BulwarkAICallback final : public CBattleCallback
{
public:
	explicit BulwarkAICallback(PlayerColor player = PlayerColor(0)) : CBattleCallback(player, nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction &) override {}
};

class NewHorizonsBulwarkAITest : public BattleTestFixture
{
protected:
	std::shared_ptr<BulwarkAIEnvironment> environment;
	std::shared_ptr<BulwarkAICallback> callback;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void enableBulwark(CGHeroInstance * hero, MasteryLevel::Type rank,
		std::initializer_list<std::string_view> perkIds = {})
	{
		const std::string skillId(newHorizonsBulwark::SKILL_ID);
		const int decoded = SecondarySkill::decode(skillId);
		ASSERT_GE(decoded, 0);
		hero->setSecSkillLevel(SecondarySkill(decoded), rank, ChangeValueMode::ABSOLUTE);
		const auto & perks = hero->getPerkState().rules["skills"][skillId]["perks"].Vector();
		const auto tierOf = [](const JsonNode & perk)
		{
			const auto requirement = perk["requires"].String();
			if(requirement == "basic")
				return 1;
			if(requirement == "advanced")
				return 2;
			if(requirement == "expert")
				return 3;
			return 0;
		};
		for(const auto perkId : perkIds)
		{
			const auto target = std::find_if(perks.begin(), perks.end(), [perkId](const JsonNode & perk)
			{
				return perk["id"].String() == perkId;
			});
			if(target == perks.end() || (*target)["effect"]["status"].String() != "active")
			{
				ADD_FAILURE() << "Requested Bulwark test perk is not active: " << perkId;
				continue;
			}

			for(int requiredTier = 1; requiredTier < tierOf(*target); ++requiredTier)
			{
				const bool alreadySelected = std::any_of(hero->getPerkState().selected.begin(),
					hero->getPerkState().selected.end(), [&](const auto & selection)
				{
					if(selection.skillId != skillId)
						return false;
					const auto selected = std::find_if(perks.begin(), perks.end(), [&](const JsonNode & perk)
					{
						return perk["id"].String() == selection.perkId;
					});
					return selected != perks.end() && tierOf(*selected) == requiredTier;
				});
				if(alreadySelected)
					continue;

				const auto prerequisite = std::find_if(perks.begin(), perks.end(), [requiredTier, &tierOf](const JsonNode & perk)
				{
					return tierOf(perk) == requiredTier && perk["effect"]["status"].String() == "active";
				});
				if(prerequisite == perks.end())
				{
					ADD_FAILURE() << "No active Bulwark prerequisite at tier " << requiredTier;
					break;
				}
				hero->applyPerkSelection({skillId, (*prerequisite)["id"].String()});
			}

			hero->applyPerkSelection({skillId, std::string(perkId)});
		}
		EXPECT_EQ(newHorizonsBulwark::rank(hero), static_cast<int>(rank));
		for(const auto perkId : perkIds)
			EXPECT_TRUE(hero->hasActivePerk(skillId, std::string(perkId)));
	}

	void removeOtherStacks(std::initializer_list<CStack *> kept)
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(!std::ranges::any_of(kept, [unit](const CStack * keptUnit)
				{ return keptUnit && keptUnit->unitId() == unit->unitId(); }))
				remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
	}

	void initializeAI(PlayerColor player = PlayerColor(0))
	{
		callback = std::make_shared<BulwarkAICallback>(player);
		callback->onBattleStarted(battle());
		environment = std::make_shared<BulwarkAIEnvironment>(gameState());
	}

	std::shared_ptr<HypotheticBattle> simulation() const
	{
		return std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	}

	BattleAction choose(CStack * stack)
	{
		BattleEvaluator evaluator(environment, callback, stack, PlayerColor(0), BattleID(0),
			BattleSide::ATTACKER, 1.0f, 2);
		return evaluator.selectStackAction(stack);
	}
};
}

TEST_F(NewHorizonsBulwarkAITest, MirebornSwampReductionMakesDefendWorthwhileWithoutMutatingLiveState)
{
	startGame();
	enableBulwark(attackerSideHero, MasteryLevel::BASIC,
		{newHorizonsBulwark::MIREBORN_ID});
	gameState()->getTile(int3(4, 4, 0))->terrainType = ETerrainId::SWAMP;
	startBattle();
	beginCombat();
	auto * protectedStack = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 20);
	auto * shooter = addStack(BattleSide::DEFENDER, creatureByName("core:titan"), BattleHex(12, 5), 3);
	ASSERT_NE(protectedStack, nullptr);
	ASSERT_NE(shooter, nullptr);
	removeOtherStacks({protectedStack, shooter});
	const Bonus immobilized(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
		BonusSource::OTHER, -protectedStack->getMovementRange(0), BonusSourceID());
	protectedStack->addNewBonus(std::make_shared<Bonus>(immobilized));
	ASSERT_EQ(protectedStack->getMovementRange(0), 0);
	battle()->activeStack = protectedStack->unitId();
	initializeAI();

	const auto projectedShotDamage = [&](TerrainId terrain)
	{
		battle()->terrainType = terrain;
		auto view = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		HypotheticBattle preview(environment.get(), view);
		auto projectedTarget = preview.getForUpdate(protectedStack->unitId());
		projectedTarget->defending = true;
		const auto * projectedShooter = preview.battleGetUnitByID(shooter->unitId());
		BattleAttackInfo shot(projectedShooter, projectedTarget.get(), 0, true);
		const auto estimate = preview.battleEstimateDamage(shot).damage;
		return (estimate.min + estimate.max) / 2;
	};
	const auto grassDamage = projectedShotDamage(ETerrainId::GRASS);
	const auto swampDamage = projectedShotDamage(ETerrainId::SWAMP);
	EXPECT_LT(swampDamage, grassDamage)
		<< "Mireborn's extra 5 percentage points must enter the same read-only damage forecast";
	battle()->terrainType = ETerrainId::SWAMP;
	const auto healthBefore = protectedStack->getAvailableHealth();
	const auto action = choose(protectedStack);
	EXPECT_EQ(action.actionType, EActionType::DEFEND);
	EXPECT_FALSE(protectedStack->defended()) << "AI projection must not apply Defend to the live stack";
	EXPECT_EQ(protectedStack->getAvailableHealth(), healthBefore);
	EXPECT_FALSE(protectedStack->bulwarkPreemptiveUsed);
}

TEST_F(NewHorizonsBulwarkAITest, SharedCoverMakesDefendWorthwhileForAnAdjacentDefendingAlly)
{
	startGame();
	enableBulwark(attackerSideHero, MasteryLevel::ADVANCED,
		{newHorizonsBulwark::SHARED_COVER_ID});
	startBattle();
	beginCombat();
	auto * activeStack = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), BattleHex(3, 5), 1);
	auto * protectedAlly = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), BattleHex(4, 5), 100);
	auto * secondAdjacentStack = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), BattleHex(5, 5), 100);
	auto * shooter = addStack(BattleSide::DEFENDER,
		creatureByName("core:titan"), BattleHex(12, 5), 10);
	ASSERT_NE(activeStack, nullptr);
	ASSERT_NE(protectedAlly, nullptr);
	ASSERT_NE(secondAdjacentStack, nullptr);
	ASSERT_NE(shooter, nullptr);
	removeOtherStacks({activeStack, protectedAlly, secondAdjacentStack, shooter});
	protectedAlly->defending = true;
	// The active stack's own ranged damage cannot be reduced below the normal
	// one-point minimum, so any Defend value must come from Shared Cover.
	activeStack->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::GENERAL_DAMAGE_REDUCTION, BonusSource::OTHER, 100, BonusSourceID(),
		BonusSubtypeID(BonusCustomSubtype::damageTypeRanged)));
	const Bonus immobilized(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
		BonusSource::OTHER, -activeStack->getMovementRange(0), BonusSourceID());
	activeStack->addNewBonus(std::make_shared<Bonus>(immobilized));
	battle()->activeStack = activeStack->unitId();
	initializeAI();

	auto view = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle before(environment.get(), view);
	const auto * beforeShooter = before.battleGetUnitByID(shooter->unitId());
	const auto * beforeAlly = before.battleGetUnitByID(protectedAlly->unitId());
	BattleAttackInfo beforeShot(beforeShooter, beforeAlly, 0, true);
	const auto damageBefore = before.battleExpectedLuckDamage(beforeShot);
	auto after = std::make_shared<HypotheticBattle>(environment.get(), view);
	auto projectedActive = after->getForUpdate(activeStack->unitId());
	projectedActive->defending = true;
	const auto * afterShooter = after->battleGetUnitByID(shooter->unitId());
	const auto * afterAlly = after->battleGetUnitByID(protectedAlly->unitId());
	BattleAttackInfo afterShot(afterShooter, afterAlly, 0, true);
	const auto damageAfter = after->battleExpectedLuckDamage(afterShot);
	ASSERT_LT(damageAfter, damageBefore)
		<< "The detached battle forecast should apply the adjacent ally's shared reduction";
	auto afterBothNeighborsDefend = std::make_shared<HypotheticBattle>(environment.get(), view);
	afterBothNeighborsDefend->getForUpdate(activeStack->unitId())->defending = true;
	afterBothNeighborsDefend->getForUpdate(secondAdjacentStack->unitId())->defending = true;
	const auto * bothShooter = afterBothNeighborsDefend->battleGetUnitByID(shooter->unitId());
	const auto * bothProtectedAlly = afterBothNeighborsDefend->battleGetUnitByID(protectedAlly->unitId());
	BattleAttackInfo afterBothShot(bothShooter, bothProtectedAlly, 0, true);
	EXPECT_EQ(afterBothNeighborsDefend->battleExpectedLuckDamage(afterBothShot), damageAfter)
		<< "Multiple adjacent defending stacks must not stack Shared Cover on one target";

	const auto action = choose(activeStack);
	EXPECT_EQ(action.actionType, EActionType::DEFEND)
		<< "Shared Cover should make the active stack's Defend protect an adjacent defender";
	EXPECT_FALSE(activeStack->defended());
}

TEST_F(NewHorizonsBulwarkAITest, BogAmbushPreemptiveForecastIsConsumedOnlyInTheExchangeProjection)
{
	startGame();
	enableBulwark(defenderSideHero, MasteryLevel::BASIC,
		{newHorizonsBulwark::BOG_AMBUSH_ID});
	startBattle();
	beginCombat();
	auto * firstAttacker = addStack(BattleSide::ATTACKER,
		creatureByName("core:magog"), BattleHex(92), 100);
	auto * defendedStack = addStack(BattleSide::DEFENDER,
		creatureByName("core:pikeman"), BattleHex(93), 1000);
	auto * secondAttacker = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), BattleHex(94), 100);
	ASSERT_NE(firstAttacker, nullptr);
	ASSERT_TRUE(firstAttacker->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
	ASSERT_NE(defendedStack, nullptr);
	ASSERT_NE(secondAttacker, nullptr);
	removeOtherStacks({firstAttacker, defendedStack, secondAttacker});
	// Isolate the once-only pre-emptive damage from ordinary retaliation by the target.
	blockRetaliation(firstAttacker);
	blockRetaliation(secondAttacker);
	defendedStack->defending = true;
	battle()->activeStack = firstAttacker->unitId();
	initializeAI(battle()->sideToPlayer(BattleSide::DEFENDER));
	const auto firstAttackerHealth = firstAttacker->getAvailableHealth();
	const auto defendedHealth = defendedStack->getAvailableHealth();
	auto model = simulation();
	DamageCache damageCache;
	damageCache.buildDamageCache(model, BattleSide::ATTACKER);
	auto * projectedFirst = model->battleGetUnitByID(firstAttacker->unitId());
	auto * projectedDefended = model->battleGetUnitByID(defendedStack->unitId());
	const BattleAttackInfo incoming(projectedFirst, projectedDefended, 0, false);
	ASSERT_TRUE(incoming.physicalDamage)
		<< "A Magog's melee attack is physical even though its ranged shot is spell-like";
	BattleAttackInfo preemptive(projectedDefended, projectedFirst, 0, false);
	preemptive.retaliation = true;
	preemptive.preemptiveDamagePercent = newHorizonsBulwark::preemptivePercent(
		static_cast<int>(MasteryLevel::BASIC), true);
	const auto expectedPreemptive = std::min(projectedFirst->getAvailableHealth(),
		model->battleExpectedLuckDamage(preemptive));
	ASSERT_GT(expectedPreemptive, 0);

	auto first = AttackPossibility::evaluate(incoming, projectedFirst->getPosition(), damageCache, model);
	ASSERT_NE(first.effectPreview, nullptr);
	ASSERT_GT(first.attackerDamageReduce, 0.0f);
	EXPECT_EQ(first.attackerState->getAvailableHealth(), firstAttackerHealth - expectedPreemptive);
	EXPECT_TRUE(first.effectPreview->getForUpdate(defendedStack->unitId())->bulwarkPreemptiveUsed);
	EXPECT_FALSE(defendedStack->bulwarkPreemptiveUsed);
	EXPECT_EQ(defendedStack->getAvailableHealth(), defendedHealth);

	BattleExchangeVariant exchange;
	exchange.trackAttack(first, model, damageCache);
	EXPECT_TRUE(model->getForUpdate(defendedStack->unitId())->bulwarkPreemptiveUsed)
		<< "The exchange replay must carry the once-per-Defend reaction into later forecast actions";
	EXPECT_FALSE(defendedStack->bulwarkPreemptiveUsed)
		<< "Forecast commitment must remain isolated from the live battle";

	DamageCache nextDamageCache;
	nextDamageCache.buildDamageCache(model, BattleSide::ATTACKER);
	auto * projectedSecond = model->battleGetUnitByID(secondAttacker->unitId());
	auto * projectedTarget = model->battleGetUnitByID(defendedStack->unitId());
	auto second = AttackPossibility::evaluate(BattleAttackInfo(projectedSecond,
		projectedTarget, 0, false), projectedSecond->getPosition(), nextDamageCache, model);
	EXPECT_NEAR(second.attackerDamageReduce, 0.0f, 0.001f)
		<< "A second melee attacker must not receive the same once-only preemptive hit";
	EXPECT_EQ(firstAttacker->getAvailableHealth(), firstAttackerHealth);
}

TEST_F(NewHorizonsBulwarkAITest, ThickHideReflectsTheRoundedActualRangedDamageInAttackForecast)
{
	startGame();
	enableBulwark(defenderSideHero, MasteryLevel::EXPERT,
		{newHorizonsBulwark::THICK_HIDE_ID});
	startBattle();
	beginCombat();
	auto * shooter = addStack(BattleSide::ATTACKER,
		creatureByName("core:titan"), BattleHex(3, 5), 100);
	auto * defendedStack = addStack(BattleSide::DEFENDER,
		creatureByName("core:pikeman"), BattleHex(12, 5), 1000);
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(defendedStack, nullptr);
	removeOtherStacks({shooter, defendedStack});
	defendedStack->defending = true;
	battle()->activeStack = shooter->unitId();
	initializeAI(battle()->sideToPlayer(BattleSide::DEFENDER));
	const auto shooterHealth = shooter->getAvailableHealth();
	const auto defendedHealth = defendedStack->getAvailableHealth();
	auto model = simulation();
	DamageCache damageCache;
	damageCache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto * projectedShooter = model->battleGetUnitByID(shooter->unitId());
	const auto * projectedDefended = model->battleGetUnitByID(defendedStack->unitId());
	BattleAttackInfo shot(projectedShooter, projectedDefended, 0, true);
	const auto expectedIncoming = std::min(projectedDefended->getAvailableHealth(),
		model->battleExpectedLuckDamage(shot));
	const int reflectionBasisPoints = newHorizonsBulwark::reflectionBasisPoints(
		static_cast<int>(MasteryLevel::EXPERT), true, true);
	const auto expectedReflection = newHorizonsBulwark::reflectedDamage(
		expectedIncoming, reflectionBasisPoints);
	ASSERT_GT(expectedReflection, 0);

	const auto possibility = AttackPossibility::evaluate(shot, BattleHex::INVALID,
		damageCache, model);
	ASSERT_NE(possibility.effectPreview, nullptr);
	EXPECT_EQ(possibility.attackerState->getAvailableHealth(), shooterHealth - expectedReflection);
	EXPECT_GT(possibility.attackerDamageReduce, 0.0f);
	EXPECT_EQ(shooter->getAvailableHealth(), shooterHealth);
	EXPECT_EQ(defendedStack->getAvailableHealth(), defendedHealth);
}

TEST_F(NewHorizonsBulwarkAITest, VengefulMireReflectionUsesThePerkInAttackForecast)
{
	startGame();
	enableBulwark(defenderSideHero, MasteryLevel::EXPERT,
		{newHorizonsBulwark::VENGEFUL_MIRE_ID});
	startBattle();
	beginCombat();
	auto * attacker = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), BattleHex(92), 100);
	auto * defendedStack = addStack(BattleSide::DEFENDER,
		creatureByName("core:pikeman"), BattleHex(93), 1000);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(defendedStack, nullptr);
	removeOtherStacks({attacker, defendedStack});
	defendedStack->defending = true;
	defendedStack->bulwarkPreemptiveUsed = true;
	blockRetaliation(attacker);
	blockRetaliation(defendedStack);
	battle()->activeStack = attacker->unitId();
	initializeAI(PlayerColor(1));

	auto model = simulation();
	DamageCache damageCache;
	damageCache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto * projectedAttacker = model->battleGetUnitByID(attacker->unitId());
	const auto * projectedDefender = model->battleGetUnitByID(defendedStack->unitId());
	BattleAttackInfo incoming(projectedAttacker, projectedDefender, 0, false);
	ASSERT_TRUE(incoming.physicalDamage);
	const auto incomingDamage = std::min(projectedDefender->getAvailableHealth(),
		model->battleExpectedLuckDamage(incoming));
	const auto expectedReflection = newHorizonsBulwark::reflectedDamage(incomingDamage,
		newHorizonsBulwark::reflectionBasisPoints(static_cast<int>(MasteryLevel::EXPERT),
			false, false, true));
	ASSERT_GT(expectedReflection, 0);

	const auto possibility = AttackPossibility::evaluate(incoming,
		projectedAttacker->getPosition(), damageCache, model);
	ASSERT_NE(possibility.effectPreview, nullptr);
	EXPECT_EQ(possibility.attackerState->getAvailableHealth(),
		attacker->getAvailableHealth() - expectedReflection);
	EXPECT_EQ(attacker->getAvailableHealth(), attacker->getTotalHealth())
		<< "Vengeful Mire belongs in the detached attack projection only";
}

TEST_F(NewHorizonsBulwarkAITest, ToxicSpinesProjectsActualReflectionPoisonAndActivationTicks)
{
	startGame();
	enableBulwark(defenderSideHero, MasteryLevel::EXPERT,
		{newHorizonsBulwark::TOXIC_SPINES_ID});
	startBattle();
	beginCombat();
	auto * attacker = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), BattleHex(92), 100);
	auto * defendedStack = addStack(BattleSide::DEFENDER,
		creatureByName("core:pikeman"), BattleHex(93), 1000);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(defendedStack, nullptr);
	removeOtherStacks({attacker, defendedStack});
	defendedStack->defending = true;
	defendedStack->bulwarkPreemptiveUsed = true;
	blockRetaliation(attacker);
	blockRetaliation(defendedStack);
	battle()->activeStack = attacker->unitId();
	initializeAI(PlayerColor(1));

	auto model = simulation();
	DamageCache damageCache;
	damageCache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto * projectedAttacker = model->battleGetUnitByID(attacker->unitId());
	const auto * projectedDefender = model->battleGetUnitByID(defendedStack->unitId());
	const auto incomingDamage = std::min(projectedDefender->getAvailableHealth(),
		model->battleExpectedLuckDamage(BattleAttackInfo(projectedAttacker, projectedDefender, 0, false)));
	const auto reflectedDamage = newHorizonsBulwark::reflectedDamage(incomingDamage,
		newHorizonsBulwark::reflectionBasisPoints(static_cast<int>(MasteryLevel::EXPERT), false, false));
	ASSERT_GT(reflectedDamage, 0);
	const auto actualReflectedDamage = std::min(reflectedDamage, projectedAttacker->getAvailableHealth());
	const auto immediateReflectionValue = AttackPossibility::calculateDamageReduce(
		projectedDefender, projectedAttacker, actualReflectedDamage, damageCache, model);
	const auto possibility = AttackPossibility::evaluate(BattleAttackInfo(
		projectedAttacker, projectedDefender, 0, false), projectedAttacker->getPosition(),
		damageCache, model);
	ASSERT_NE(possibility.effectPreview, nullptr);
	const auto * forecastAttacker = possibility.effectPreview->battleGetUnitByID(attacker->unitId());
	const auto * forecastDefender = possibility.effectPreview->battleGetUnitByID(defendedStack->unitId());
	ASSERT_NE(forecastAttacker, nullptr);
	ASSERT_NE(forecastDefender, nullptr);
	const auto expectedBase = newHorizonsBulwark::toxicSpinesPoisonBase(actualReflectedDamage);
	EXPECT_EQ(forecastAttacker->acquireState()->physicalPoisonBaseDamage, expectedBase);
	EXPECT_EQ(forecastAttacker->acquireState()->physicalPoisonActivationsRemaining, 3);
	EXPECT_EQ(possibility.attackerState->getAvailableHealth(),
		projectedAttacker->getAvailableHealth() - actualReflectedDamage);
	const auto residualPoisonTicks = expectedBase * 4 + expectedBase / 2;
	const auto residualPoisonValue = AttackPossibility::calculateDamageReduce(projectedDefender,
		possibility.attackerState.get(), residualPoisonTicks, damageCache, model);
	EXPECT_NEAR(possibility.attackerDamageReduce,
		immediateReflectionValue + residualPoisonValue, 0.001f)
		<< "AI score must value direct reflection before the hit and residual Poison against the post-reflection attacker";
	EXPECT_EQ(forecastDefender->acquireState()->bulwarkToxicSpinesRound, battle()->battleGetRound());
	EXPECT_EQ(attacker->acquireState()->physicalPoisonBaseDamage, 0)
		<< "the real battle must not be mutated by AI evaluation";
	BattleExchangeVariant exchange;
	exchange.trackAttack(possibility, model, damageCache);
	EXPECT_EQ(model->battleGetUnitByID(attacker->unitId())->acquireState()->physicalPoisonBaseDamage,
		expectedBase);
	EXPECT_EQ(model->battleGetUnitByID(defendedStack->unitId())->acquireState()->bulwarkToxicSpinesRound,
		battle()->battleGetRound());

	const auto healthBeforePoisonTick = forecastAttacker->getAvailableHealth();
	possibility.effectPreview->nextTurn(attacker->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	const auto * afterFirstActivation = possibility.effectPreview->battleGetUnitByID(attacker->unitId());
	EXPECT_EQ(afterFirstActivation->acquireState()->physicalPoisonActivationsRemaining, 2);
	EXPECT_EQ(afterFirstActivation->getAvailableHealth(),
		healthBeforePoisonTick - expectedBase);
}

TEST_F(NewHorizonsBulwarkAITest, ImmovableIsConsumedAfterTheFirstPhysicalHitInMultiAttackForecast)
{
	startGame();
	enableBulwark(defenderSideHero, MasteryLevel::EXPERT,
		{newHorizonsBulwark::IMMOVABLE_ID});
	startBattle();
	beginCombat();
	auto * shooter = addStack(BattleSide::ATTACKER,
		creatureByName("core:titan"), BattleHex(3, 5), 10);
	auto * defendedStack = addStack(BattleSide::DEFENDER,
		creatureByName("core:pikeman"), BattleHex(12, 5), 1000);
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(defendedStack, nullptr);
	removeOtherStacks({shooter, defendedStack});
	defendedStack->defending = true;
	shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 1, BonusSourceID(),
		BonusCustomSubtype::damageTypeRanged));
	ASSERT_EQ(shooter->getTotalAttacks(true), 2);
	battle()->activeStack = shooter->unitId();
	initializeAI(PlayerColor(1));

	auto expectedBattle = simulation();
	auto expectedShooter = expectedBattle->getForUpdate(shooter->unitId());
	auto expectedDefender = expectedBattle->getForUpdate(defendedStack->unitId());
	int64_t expectedTotalDamage = 0;
	for(int attack = 0; attack < 2; ++attack)
	{
		BattleAttackInfo shot(expectedShooter.get(), expectedDefender.get(), 0, true);
		auto damage = expectedBattle->battleExpectedLuckDamage(shot);
		vstd::amin(damage, expectedDefender->getAvailableHealth());
		expectedDefender->damage(damage);
		expectedTotalDamage += damage;
		if(attack == 0)
			expectedDefender->bulwarkImmovableRound = battle()->battleGetRound();
	}

	auto forecast = simulation();
	DamageCache damageCache;
	damageCache.buildDamageCache(forecast, BattleSide::ATTACKER);
	const auto * projectedShooter = forecast->battleGetUnitByID(shooter->unitId());
	const auto * projectedDefender = forecast->battleGetUnitByID(defendedStack->unitId());
	const auto originalHealth = defendedStack->getAvailableHealth();
	const auto possibility = AttackPossibility::evaluate(BattleAttackInfo(
		projectedShooter, projectedDefender, 0, true), BattleHex::INVALID, damageCache, forecast);
	ASSERT_NE(possibility.effectPreview, nullptr);
	const auto * forecastDefender = possibility.effectPreview->battleGetUnitByID(defendedStack->unitId());
	ASSERT_NE(forecastDefender, nullptr);
	EXPECT_EQ(forecastDefender->getAvailableHealth(), originalHealth - expectedTotalDamage);
	EXPECT_EQ(forecastDefender->acquireState()->bulwarkImmovableRound,
		battle()->battleGetRound());
	EXPECT_EQ(defendedStack->getAvailableHealth(), originalHealth)
		<< "The first-hit marker belongs only to the detached forecast";
}

TEST_F(NewHorizonsBulwarkAITest, MireGripProjectsTheActivationScopedSlowWithoutChangingTheLiveAttacker)
{
	startGame();
	enableBulwark(defenderSideHero, MasteryLevel::ADVANCED,
		{newHorizonsBulwark::MIRE_GRIP_ID});
	startBattle();
	beginCombat();
	auto * attacker = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), BattleHex(92), 100);
	auto * defendedStack = addStack(BattleSide::DEFENDER,
		creatureByName("core:pikeman"), BattleHex(93), 1000);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(defendedStack, nullptr);
	removeOtherStacks({attacker, defendedStack});
	defendedStack->defending = true;
	defendedStack->bulwarkPreemptiveUsed = true;
	blockRetaliation(attacker);
	blockRetaliation(defendedStack);
	battle()->activeStack = attacker->unitId();
	initializeAI(PlayerColor(1));

	auto model = simulation();
	DamageCache damageCache;
	damageCache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto * projectedAttacker = model->battleGetUnitByID(attacker->unitId());
	const auto * projectedDefender = model->battleGetUnitByID(defendedStack->unitId());
	const int originalMovement = projectedAttacker->getMovementRange();
	const auto possibility = AttackPossibility::evaluate(BattleAttackInfo(
		projectedAttacker, projectedDefender, 0, false), projectedAttacker->getPosition(),
		damageCache, model);
	ASSERT_TRUE(possibility.bulwarkMireGripTriggered);
	ASSERT_NE(possibility.effectPreview, nullptr);
	const auto * forecastAttacker = possibility.effectPreview->battleGetUnitByID(attacker->unitId());
	ASSERT_NE(forecastAttacker, nullptr);
	EXPECT_EQ(forecastAttacker->getMovementRange(), std::max(0, originalMovement - 2));
	EXPECT_TRUE(forecastAttacker->acquireState()->bulwarkMireGripApplied);
	EXPECT_EQ(attacker->getMovementRange(), originalMovement);
	EXPECT_FALSE(attacker->acquireState()->bulwarkMireGripApplied)
		<< "The one-activation marker belongs only to the detached preview";

	BattleExchangeVariant exchange;
	exchange.trackAttack(possibility, model, damageCache);
	const auto * committedAttacker = model->battleGetUnitByID(attacker->unitId());
	ASSERT_NE(committedAttacker, nullptr);
	EXPECT_EQ(committedAttacker->getMovementRange(), std::max(0, originalMovement - 2));
	EXPECT_TRUE(committedAttacker->acquireState()->bulwarkMireGripApplied);

	const auto sameActivation = AttackPossibility::evaluate(BattleAttackInfo(
		committedAttacker, model->battleGetUnitByID(defendedStack->unitId()), 0, false),
		committedAttacker->getPosition(), damageCache, model);
	EXPECT_FALSE(sameActivation.bulwarkMireGripTriggered)
		<< "Mire Grip can apply only once before the attacker's next activation";
	const int bulwarkSkillId = SecondarySkill::decode(std::string(newHorizonsBulwark::SKILL_ID));
	ASSERT_GE(bulwarkSkillId, 0);
	const auto mireGripSource = BonusSourceID(SecondarySkill(bulwarkSkillId));
	const auto projectedPenalty = committedAttacker->getAllBonuses(
		Selector::source(BonusSource::OTHER, mireGripSource));
	ASSERT_EQ(projectedPenalty->size(), 1u);
	EXPECT_TRUE(Bonus::OneBattle(projectedPenalty->front().get()));
	EXPECT_EQ(projectedPenalty->front()->val, -2);

	model->nextTurn(attacker->unitId(), BattleUnitTurnReason::HERO_SPELLCAST);
	const auto * afterSpellcast = model->battleGetUnitByID(attacker->unitId());
	ASSERT_NE(afterSpellcast, nullptr);
	EXPECT_TRUE(afterSpellcast->acquireState()->bulwarkMireGripApplied);
	EXPECT_EQ(afterSpellcast->getMovementRange(), std::max(0, originalMovement - 2));
	const auto continuation = AttackPossibility::evaluate(BattleAttackInfo(
		afterSpellcast, model->battleGetUnitByID(defendedStack->unitId()), 0, false),
		afterSpellcast->getPosition(), damageCache, model);
	EXPECT_FALSE(continuation.bulwarkMireGripTriggered)
		<< "A hero spellcast resumes the same activation and must retain Mire Grip";

	model->nextTurn(attacker->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	const auto * afterActivation = model->battleGetUnitByID(attacker->unitId());
	EXPECT_EQ(afterActivation->getMovementRange(), originalMovement);
	EXPECT_FALSE(afterActivation->acquireState()->bulwarkMireGripApplied);
	EXPECT_TRUE(afterActivation->getAllBonuses(Selector::source(
		BonusSource::OTHER, mireGripSource))->empty());

	const auto * afterActivationDefender = model->battleGetUnitByID(defendedStack->unitId());
	const auto reapplication = AttackPossibility::evaluate(BattleAttackInfo(
		afterActivation, afterActivationDefender, 0, false), afterActivation->getPosition(),
		damageCache, model);
	EXPECT_TRUE(reapplication.bulwarkMireGripTriggered)
		<< "A new Mire Grip may apply after the attacker's next real activation";
	ASSERT_NE(reapplication.effectPreview, nullptr);
	EXPECT_TRUE(reapplication.attackerState->bulwarkMireGripApplied);
	const auto * reappliedAttacker = reapplication.effectPreview->battleGetUnitByID(attacker->unitId());
	ASSERT_NE(reappliedAttacker, nullptr);
	EXPECT_EQ(reappliedAttacker->getMovementRange(), std::max(0, originalMovement - 2));
	EXPECT_EQ(afterActivation->getMovementRange(), originalMovement)
		<< "The repeated slow remains confined to the detached preview";
}

TEST_F(NewHorizonsBulwarkAITest, MireGripSeededPenaltyExpiresOnSecondWindActivation)
{
	startGame();
	enableBulwark(defenderSideHero, MasteryLevel::ADVANCED,
		{newHorizonsBulwark::MIRE_GRIP_ID});
	startBattle();
	beginCombat();
	auto * attacker = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), BattleHex(92), 100);
	auto * defendedStack = addStack(BattleSide::DEFENDER,
		creatureByName("core:pikeman"), BattleHex(93), 1000);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(defendedStack, nullptr);
	removeOtherStacks({attacker, defendedStack});
	blockRetaliation(attacker);
	defendedStack->defending = true;
	defendedStack->bulwarkPreemptiveUsed = true;
	blockRetaliation(defendedStack);
	const int originalMovement = attacker->getMovementRange();
	const int bulwarkSkillId = SecondarySkill::decode(std::string(newHorizonsBulwark::SKILL_ID));
	ASSERT_GE(bulwarkSkillId, 0);
	const auto mireGripSource = BonusSourceID(SecondarySkill(bulwarkSkillId));
	attacker->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::STACKS_SPEED, BonusSource::OTHER, -2, mireGripSource));
	attacker->bulwarkMireGripApplied = true;
	battle()->activeStack = attacker->unitId();
	initializeAI(PlayerColor(1));

	auto model = simulation();
	auto projectedAttacker = model->getForUpdate(attacker->unitId());
	ASSERT_TRUE(projectedAttacker->bulwarkMireGripApplied);
	EXPECT_EQ(projectedAttacker->getMovementRange(), std::max(0, originalMovement - 2));
	const auto seededPenalty = projectedAttacker->getAllBonuses(
		Selector::source(BonusSource::OTHER, mireGripSource));
	ASSERT_EQ(seededPenalty->size(), 1u);
	EXPECT_TRUE(Bonus::OneBattle(seededPenalty->front().get()));

	model->nextTurn(attacker->unitId(), BattleUnitTurnReason::ACTION_REJECTED);
	EXPECT_TRUE(projectedAttacker->bulwarkMireGripApplied);
	EXPECT_EQ(projectedAttacker->getMovementRange(), std::max(0, originalMovement - 2));

	HeroOrderState ordinaryOrder;
	ordinaryOrder.command = HeroCommand::RIPOSTE;
	ordinaryOrder.issuedRound = model->battleGetRound();
	model->setHeroOrderState(BattleSide::ATTACKER, ordinaryOrder);
	model->nextTurn(attacker->unitId(), BattleUnitTurnReason::HERO_COMMAND);
	EXPECT_TRUE(projectedAttacker->bulwarkMireGripApplied);
	EXPECT_EQ(projectedAttacker->getMovementRange(), std::max(0, originalMovement - 2));

	model->nextRound();
	EXPECT_TRUE(projectedAttacker->bulwarkMireGripApplied);
	EXPECT_EQ(projectedAttacker->getMovementRange(), std::max(0, originalMovement - 2));

	model->nextTurn(attacker->unitId(), BattleUnitTurnReason::HERO_SPELLCAST);
	EXPECT_TRUE(projectedAttacker->bulwarkMireGripApplied);
	EXPECT_EQ(projectedAttacker->getMovementRange(), std::max(0, originalMovement - 2));
	EXPECT_EQ(projectedAttacker->getAllBonuses(
		Selector::source(BonusSource::OTHER, mireGripSource))->size(), 1u);

	HeroOrderState secondWind;
	secondWind.command = HeroCommand::SECOND_WIND;
	secondWind.issuedRound = model->battleGetRound();
	secondWind.secondWindActive = true;
	secondWind.primaryTargetUnitId = attacker->unitId();
	model->setHeroOrderState(BattleSide::ATTACKER, secondWind);
	model->nextTurn(attacker->unitId(), BattleUnitTurnReason::HERO_COMMAND);
	EXPECT_FALSE(projectedAttacker->bulwarkMireGripApplied);
	EXPECT_EQ(projectedAttacker->getMovementRange(), originalMovement);
	EXPECT_TRUE(projectedAttacker->getAllBonuses(
		Selector::source(BonusSource::OTHER, mireGripSource))->empty());
}

TEST_F(NewHorizonsBulwarkAITest, MireGripProjectsWhenOnlyDamagingCollateralDefends)
{
	startGame();
	enableBulwark(defenderSideHero, MasteryLevel::ADVANCED,
		{newHorizonsBulwark::MIRE_GRIP_ID});
	startBattle();
	auto * attacker = addStack(BattleSide::ATTACKER,
		creatureByName("core:hydra"), BattleHex(leftHex), 10);
	auto * primary = addStack(BattleSide::DEFENDER,
		creatureByName("core:pikeman"), BattleHex(rightHex), 100);
	auto * collateral = addStack(BattleSide::DEFENDER,
		creatureByName("core:pikeman"), BattleHex(leftHex - 17), 100);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(primary, nullptr);
	ASSERT_NE(collateral, nullptr);
	removeOtherStacks({attacker, primary, collateral});
	beginCombat();
	primary->defending = false;
	collateral->defending = true;
	collateral->bulwarkPreemptiveUsed = true;
	forceMaximumDamage(attacker);
	blockRetaliation(attacker);
	blockRetaliation(primary);
	blockRetaliation(collateral);
	battle()->activeStack = attacker->unitId();
	initializeAI(PlayerColor(1));

	auto model = simulation();
	DamageCache damageCache;
	damageCache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto * projectedAttacker = model->battleGetUnitByID(attacker->unitId());
	const auto * projectedPrimary = model->battleGetUnitByID(primary->unitId());
	const auto * projectedCollateral = model->battleGetUnitByID(collateral->unitId());
	ASSERT_NE(projectedAttacker, nullptr);
	ASSERT_NE(projectedPrimary, nullptr);
	ASSERT_NE(projectedCollateral, nullptr);
	const int originalMovement = projectedAttacker->getMovementRange();
	const auto possibility = AttackPossibility::evaluate(BattleAttackInfo(
		projectedAttacker, projectedPrimary, 0, false), projectedAttacker->getPosition(),
		damageCache, model);

	ASSERT_TRUE(possibility.bulwarkMireGripTriggered)
		<< "A damaging collateral defender can apply Mire Grip even when the primary target is not Defending";
	ASSERT_NE(possibility.effectPreview, nullptr);
	const auto * forecastAttacker = possibility.effectPreview->battleGetUnitByID(attacker->unitId());
	const auto * forecastCollateral = possibility.effectPreview->battleGetUnitByID(collateral->unitId());
	ASSERT_NE(forecastAttacker, nullptr);
	ASSERT_NE(forecastCollateral, nullptr);
	EXPECT_LT(forecastCollateral->getAvailableHealth(), collateral->getAvailableHealth());
	EXPECT_EQ(forecastAttacker->getMovementRange(), std::max(0, originalMovement - 2));
	EXPECT_EQ(attacker->getMovementRange(), originalMovement)
		<< "Collateral Mire Grip projection must not mutate the live attacker";
}

TEST_F(NewHorizonsBulwarkAITest, SwampRenewalCanJustifyDefendAndHealsAccumulatedForecastDamage)
{
	startGame();
	enableBulwark(attackerSideHero, MasteryLevel::ADVANCED,
		{newHorizonsBulwark::SWAMP_RENEWAL_ID});
	startBattle();
	beginCombat();
	auto * protectedStack = addStack(BattleSide::ATTACKER,
		creatureByName("core:angel"), BattleHex(3, 5), 1);
	auto * shooter = addStack(BattleSide::DEFENDER,
		creatureByName("core:titan"), BattleHex(12, 5), 1);
	ASSERT_NE(protectedStack, nullptr);
	ASSERT_NE(shooter, nullptr);
	removeOtherStacks({protectedStack, shooter});
	const Bonus rangedImmunity(BonusDuration::ONE_BATTLE, BonusType::GENERAL_DAMAGE_REDUCTION,
		BonusSource::OTHER, 100, BonusSourceID(), BonusSubtypeID(BonusCustomSubtype::damageTypeRanged));
	protectedStack->addNewBonus(std::make_shared<Bonus>(rangedImmunity));
	const Bonus immobilized(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
		BonusSource::OTHER, -protectedStack->getMovementRange(0), BonusSourceID());
	protectedStack->addNewBonus(std::make_shared<Bonus>(immobilized));
	shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 9, BonusSourceID(),
		BonusCustomSubtype::damageTypeRanged));
	ASSERT_EQ(shooter->getTotalAttacks(true), 10);
	battle()->activeStack = protectedStack->unitId();
	initializeAI();
	EXPECT_EQ(choose(protectedStack).actionType, EActionType::DEFEND)
		<< "Ten reduced physical hits restore one HP at the next activation, making Defend valuable";
	EXPECT_FALSE(protectedStack->defended());

	auto model = simulation();
	DamageCache damageCache;
	damageCache.buildDamageCache(model, BattleSide::DEFENDER);
	model->getForUpdate(protectedStack->unitId())->defending = true;
	const auto * projectedShooter = model->battleGetUnitByID(shooter->unitId());
	const auto * projectedTarget = model->battleGetUnitByID(protectedStack->unitId());
	const auto initialHealth = projectedTarget->getAvailableHealth();
	const auto possibility = AttackPossibility::evaluate(BattleAttackInfo(
		projectedShooter, projectedTarget, 0, true), BattleHex::INVALID, damageCache, model);
	ASSERT_NE(possibility.effectPreview, nullptr);
	ASSERT_TRUE(possibility.effectPreview->getForUpdate(protectedStack->unitId())
		->bulwarkDefendPhysicalDamage > 0);
	BattleExchangeVariant exchange;
	exchange.trackAttack(possibility, model, damageCache);
	auto damagedTarget = model->getForUpdate(protectedStack->unitId());
	const auto physicalDamageWhileDefending = damagedTarget->bulwarkDefendPhysicalDamage;
	ASSERT_GT(physicalDamageWhileDefending, 0);
	ASSERT_LT(physicalDamageWhileDefending, initialHealth);
	EXPECT_EQ(damagedTarget->getAvailableHealth(), initialHealth - physicalDamageWhileDefending);
	model->nextTurn(protectedStack->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(damagedTarget->getAvailableHealth(), initialHealth - physicalDamageWhileDefending
		+ physicalDamageWhileDefending / 10);
	EXPECT_EQ(damagedTarget->bulwarkDefendPhysicalDamage, 0);
	EXPECT_EQ(protectedStack->getAvailableHealth(), initialHealth)
		<< "Forecast attacks and next-activation healing must remain detached from live health";
}

TEST_F(NewHorizonsBulwarkAITest, ReflectionWaitsUntilAllBreathVictimsAreForecast)
{
	startGame();
	enableBulwark(defenderSideHero, MasteryLevel::EXPERT);
	startBattle();
	beginCombat();
	auto * attacker = addStack(BattleSide::ATTACKER,
		creatureByName("core:titan"), BattleHex(92), 1);
	auto * defendedPrimary = addStack(BattleSide::DEFENDER,
		creatureByName("core:pikeman"), BattleHex(93), 1000);
	auto * collateral = addStack(BattleSide::DEFENDER,
		creatureByName("core:pikeman"), BattleHex(94), 1000);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(defendedPrimary, nullptr);
	ASSERT_NE(collateral, nullptr);
	removeOtherStacks({attacker, defendedPrimary, collateral});
	defendedPrimary->defending = true;
	// The pre-emptive hit is unrelated to this ordering regression and could
	// kill the deliberately fragile attacker before its breath strike resolves.
	defendedPrimary->bulwarkPreemptiveUsed = true;
	battle()->activeStack = attacker->unitId();
	initializeAI(battle()->sideToPlayer(BattleSide::DEFENDER));

	auto model = simulation();
	auto projectedAttacker = model->getForUpdate(attacker->unitId());
	const Bonus breath(BonusDuration::ONE_BATTLE, BonusType::TWO_HEX_ATTACK_BREATH,
		BonusSource::OTHER, 1, BonusSourceID());
	model->addUnitBonus(attacker->unitId(), {breath});
	int64_t damageToLeaveFiveHealth = projectedAttacker->getAvailableHealth() - 5;
	projectedAttacker->damage(damageToLeaveFiveHealth);
	ASSERT_EQ(projectedAttacker->getAvailableHealth(), 5);

	DamageCache damageCache;
	damageCache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto * projectedPrimary = model->battleGetUnitByID(defendedPrimary->unitId());
	BattleAttackInfo incoming(projectedAttacker.get(), projectedPrimary, 0, false);
	const auto primaryLoss = std::min(projectedPrimary->getAvailableHealth(),
		model->battleExpectedLuckDamage(incoming));
	const auto expectedReflection = newHorizonsBulwark::reflectedDamage(primaryLoss,
		newHorizonsBulwark::reflectionBasisPoints(static_cast<int>(MasteryLevel::EXPERT), false, false));
	ASSERT_GT(expectedReflection, projectedAttacker->getAvailableHealth());

	const auto possibility = AttackPossibility::evaluate(incoming,
		projectedAttacker->getPosition(), damageCache, model);
	ASSERT_NE(possibility.effectPreview, nullptr);
	EXPECT_FALSE(possibility.attackerState->alive())
		<< "The primary Bulwark hit should lethally reflect after the attack is complete";
	const auto * forecastCollateral = possibility.effectPreview->battleGetUnitByID(collateral->unitId());
	ASSERT_NE(forecastCollateral, nullptr);
	EXPECT_LT(forecastCollateral->getAvailableHealth(), collateral->getAvailableHealth())
		<< "Breath collateral must be forecast before lethal reflection reduces the attacker";
	EXPECT_EQ(attacker->getAvailableHealth(), attacker->getTotalHealth())
		<< "The forecast must not mutate live health";
}

TEST_F(NewHorizonsBulwarkAITest, DefendHasNoSurvivalValueWhenBothIncomingForecastsAreLethalOverkill)
{
	startGame();
	enableBulwark(attackerSideHero, MasteryLevel::BASIC);
	startBattle();
	beginCombat();
	auto * protectedStack = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), BattleHex(3, 5), 20);
	auto * shooter = addStack(BattleSide::DEFENDER,
		creatureByName("core:titan"), BattleHex(12, 5), 100);
	ASSERT_NE(protectedStack, nullptr);
	ASSERT_NE(shooter, nullptr);
	removeOtherStacks({protectedStack, shooter});
	const Bonus immobilized(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
		BonusSource::OTHER, -protectedStack->getMovementRange(0), BonusSourceID());
	protectedStack->addNewBonus(std::make_shared<Bonus>(immobilized));
	ASSERT_EQ(protectedStack->getMovementRange(0), 0);
	protectedStack->bulwarkPreemptiveUsed = true;
	battle()->activeStack = protectedStack->unitId();
	initializeAI();

	auto view = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle preview(environment.get(), view);
	const auto * projectedShooter = preview.battleGetUnitByID(shooter->unitId());
	const auto * projectedProtected = preview.battleGetUnitByID(protectedStack->unitId());
	BattleAttackInfo beforeDefend(projectedShooter, projectedProtected, 0, true);
	const auto beforeDamage = preview.battleExpectedLuckDamage(beforeDefend);
	auto defendedPreview = std::make_shared<HypotheticBattle>(environment.get(), view);
	auto defendedTarget = defendedPreview->getForUpdate(protectedStack->unitId());
	defendedTarget->defending = true;
	BattleAttackInfo afterDefend(defendedPreview->battleGetUnitByID(shooter->unitId()),
		defendedTarget.get(), 0, true);
	const auto afterDamage = defendedPreview->battleExpectedLuckDamage(afterDefend);
	ASSERT_GE(beforeDamage, protectedStack->getAvailableHealth());
	ASSERT_GE(afterDamage, protectedStack->getAvailableHealth());

	const auto healthBefore = protectedStack->getAvailableHealth();
	const auto action = choose(protectedStack);
	EXPECT_EQ(action.actionType, EActionType::WAIT)
		<< "Damage reduction that still leaves the entire stack killed must not make Defend worthwhile";
	EXPECT_FALSE(protectedStack->defended());
	EXPECT_EQ(protectedStack->getAvailableHealth(), healthBefore);
}
