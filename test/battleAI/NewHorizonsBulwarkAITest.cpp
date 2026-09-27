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
	BulwarkAICallback() : CBattleCallback(PlayerColor(0), nullptr) {}
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

	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		auto perkRules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		auto & perks = perkRules["skills"][std::string(newHorizonsBulwark::SKILL_ID)]["perks"].Vector();
		for(const auto perkId : {"new-horizons:bulwarkOfTheMire.mireborn",
			"new-horizons:bulwarkOfTheMire.thickHide", "new-horizons:bulwarkOfTheMire.bogAmbush"})
		{
			const auto found = std::find_if(perks.begin(), perks.end(), [perkId](const JsonNode & perk)
			{
				return perk["id"].String() == perkId;
			});
			if(found == perks.end())
				throw std::runtime_error(std::string("Bulwark test perk missing: ") + perkId);
			(*found)["effect"]["status"].String() = "active";
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perkRules);
	}

	void enableBulwark(CGHeroInstance * hero, MasteryLevel::Type rank,
		std::initializer_list<std::string_view> perkIds = {})
	{
		const int decoded = SecondarySkill::decode(std::string(newHorizonsBulwark::SKILL_ID));
		ASSERT_GE(decoded, 0);
		hero->setSecSkillLevel(SecondarySkill(decoded), rank, ChangeValueMode::ABSOLUTE);
		for(const auto perkId : perkIds)
			hero->applyPerkSelection({std::string(newHorizonsBulwark::SKILL_ID), std::string(perkId)});
		EXPECT_EQ(newHorizonsBulwark::rank(hero), static_cast<int>(rank));
		for(const auto perkId : perkIds)
			EXPECT_TRUE(hero->hasActivePerk(std::string(newHorizonsBulwark::SKILL_ID), std::string(perkId)));
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

	void initializeAI()
	{
		callback = std::make_shared<BulwarkAICallback>();
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
	blockRetaliation(defendedStack);
	defendedStack->defending = true;
	battle()->activeStack = firstAttacker->unitId();
	initializeAI();
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
	initializeAI();
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
	initializeAI();

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
