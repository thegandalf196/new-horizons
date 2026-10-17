/*
 * BracePreemptiveForecastTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BracePreemptiveForecast.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/SpellTargetsEvaluator.h"
#include "HeroQueueBonusSnapshot.h"
#include "../../lib/GameConstants.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsArmorer.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/serializer/CMemorySerializer.h"

#include <tuple>

namespace
{
class BraceEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit BraceEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

template<typename T> std::vector<std::byte> serialized(const T & value)
{
	CMemorySerializer memory;
	memory.oser & value;
	return memory.extractBuffer();
}
}

class BracePreemptiveForecastTest : public HeroCommandFixture
{
protected:
	CStack * braced = nullptr;
	CStack * mover = nullptr;
	std::unique_ptr<BraceEnvironment> environment;
	std::shared_ptr<CBattleInfoCallback> live;
	std::shared_ptr<HypotheticBattle> stage;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare(int bracedCount = 10, int moverCount = 100, BattleHex origin = BattleHex(89))
	{
		ASSERT_NO_FATAL_FAILURE(prepareCommands());
		braced = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(93), bracedCount);
		mover = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), origin, moverCount);
		forceMaximumDamage(braced);
		blockRetaliation(braced);
		blockRetaliation(mover);
		ASSERT_TRUE(issue(HeroCommand::BRACE));
		battle()->activeStack = mover->unitId();
		environment = std::make_unique<BraceEnvironment>(gameState());
		live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		stage = std::make_shared<HypotheticBattle>(environment.get(), live);
	}

	void arrive(int distance)
	{
		ASSERT_EQ(SpellTargetEvaluator::physicalTravelDistance(live->getReachability(mover), BattleHex(92)), distance);
		// Construct this test's explicit post-movement stage through the maintained
		// detached packet consumer. Production forecast NEVER moves its inputs.
		BattleStackMoved moved;
		moved.battleID = BattleID(0);
		moved.stack = mover->unitId();
		moved.tilesToMove.insert(BattleHex(92));
		moved.distance = distance;
		stage->getServerCallback()->apply(moved);
	}

	heroOrderValue::BracePreemptiveForecast forecast(int distance = 3,
		bool ranged = false, bool counter = false)
	{
		return heroOrderValue::forecastBraceAtStage(*stage,
			{mover->unitId(), braced->unitId(), distance, ranged, counter});
	}

	void assertServerPrimaryParity(const heroOrderValue::BracePreemptiveForecast & predicted,
		bool expectIncoming)
	{
		ASSERT_TRUE(predicted.event);
		ASSERT_TRUE(predicted.primary);
		ASSERT_EQ(predicted.coverage, heroOrderValue::BraceStageCoverage::CONDITIONAL_PRIMARY_HIT);
		ASSERT_EQ(predicted.primary->damageRange.damage.min, predicted.primary->damageRange.damage.max);
		ASSERT_EQ(stage->battleGetAttackLuck(stage->battleGetUnitByID(braced->unitId()),
			stage->battleGetUnitByID(mover->unitId()), false), 0);
		ASSERT_EQ(stage.use_count(), 1);
		const bool ownsPlayerWrapper = dynamic_cast<CPlayerBattleCallback *>(live.get()) != nullptr;
		stage.reset(); // Last projected provider owner; forecast retains values only.
		if(ownsPlayerWrapper)
		{
			ASSERT_EQ(live.use_count(), 1);
		}
		live.reset(); // An ALL_KNOWING alias does not own/destroy the native world.
		server.attacks.clear();
		const auto retaliationBefore = braced->counterAttacks.available();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
			BattleAction::makeMeleeAttack(mover, braced, BattleHex(92), false)));
		ASSERT_FALSE(server.attacks.empty());
		const auto & first = server.attacks.front();
		EXPECT_EQ(first.stackAttacking, predicted.event->attackerId);
		EXPECT_FALSE(first.counter()) << "Brace is not a normal retaliation packet";
		const auto hit = std::ranges::find(first.bsa, predicted.event->victimId, &BattleStackAttacked::stackAttacked);
		ASSERT_NE(hit, first.bsa.end());
		EXPECT_EQ(hit->damageAmount, predicted.primary->expectedPrimaryDamageAmount);
		EXPECT_EQ(mover->getAvailableHealth(), predicted.primary->victimHealthAfterPrimary);
		EXPECT_EQ(std::ranges::count(server.attacks, braced->unitId(), &BattleAttack::stackAttacking), 1);
		EXPECT_EQ(braced->counterAttacks.available(), retaliationBefore);
		EXPECT_EQ(std::ranges::any_of(server.attacks, [this](const auto & attack)
		{
			return attack.stackAttacking == mover->unitId();
		}), expectIncoming);
	}

	void selectArmorer(CGHeroInstance * hero, MasteryLevel::Type rank, const std::string & perk)
	{
		const int skill = SecondarySkill::decode("new-horizons:armorer");
		ASSERT_GE(skill, 0);
		hero->setSecSkillLevel(SecondarySkill(skill), rank, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(hero->getPerkState().canAcquirePerk("new-horizons:armorer", perk,
			hero->getPerkSkillRank("new-horizons:armorer")));
		hero->applyPerkSelection({"new-horizons:armorer", perk});
		ASSERT_TRUE(hero->hasActivePerk("new-horizons:armorer", perk));
	}
};

TEST_F(BracePreemptiveForecastTest, ActualThreeHexIncomingAttackMatchesOnePrimaryBeforeIncoming)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	arrive(3);
	const auto predicted = forecast();
	ASSERT_TRUE(predicted.event);
	EXPECT_EQ(predicted.event->attackerId, braced->unitId());
	EXPECT_EQ(predicted.event->victimId, mover->unitId());
	EXPECT_EQ(predicted.event->attackerPosition, BattleHex(93));
	EXPECT_EQ(predicted.event->victimPosition, BattleHex(92));
	EXPECT_EQ(predicted.event->victimHealthAtEntry, mover->getAvailableHealth());
	EXPECT_EQ(predicted.event->attackerHealthAtEntry, braced->getAvailableHealth());
	EXPECT_EQ(predicted.event->voluntaryMovementDistance, 3);
	EXPECT_EQ(predicted.event->round, battle()->battleGetRound());
	EXPECT_EQ(predicted.event->damageProvenance, battle::DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_FALSE(predicted.event->normalRetaliation);
	EXPECT_TRUE(predicted.event->reactionForLastStand);
	EXPECT_FALSE(predicted.continuationComplete);
	ASSERT_NO_FATAL_FAILURE(assertServerPrimaryParity(predicted, true));
}

TEST_F(BracePreemptiveForecastTest, ActualTwoHexIncomingDoesNotTriggerDespiteSufficientSpeed)
{
	ASSERT_NO_FATAL_FAILURE(prepare(10, 100, BattleHex(90)));
	ASSERT_GE(mover->getMovementRange(0), 3u);
	arrive(2);
	const auto predicted = forecast(2);
	EXPECT_EQ(predicted.coverage, heroOrderValue::BraceStageCoverage::NOT_TRIGGERED);
	EXPECT_FALSE(predicted.event);
	EXPECT_FALSE(predicted.primary);
	ASSERT_EQ(stage.use_count(), 1);
	const bool ownsPlayerWrapper = dynamic_cast<CPlayerBattleCallback *>(live.get()) != nullptr;
	stage.reset(); // Last projected provider owner; forecast retains values only.
	if(ownsPlayerWrapper)
	{
		ASSERT_EQ(live.use_count(), 1);
	}
	live.reset(); // An ALL_KNOWING alias does not own/destroy the native world.
	server.attacks.clear();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeMeleeAttack(mover, braced, BattleHex(92), false)));
	ASSERT_FALSE(server.attacks.empty());
	EXPECT_EQ(server.attacks.front().stackAttacking, mover->unitId());
	EXPECT_EQ(std::ranges::count(server.attacks, braced->unitId(), &BattleAttack::stackAttacking), 0);
}

TEST_F(BracePreemptiveForecastTest, LethalPreemptionMatchesServerAndSuppressesIncoming)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, 1));
	arrive(3);
	const auto predicted = forecast();
	ASSERT_TRUE(predicted.primary);
	EXPECT_EQ(predicted.primary->victimHealthAfterPrimary, 0);
	ASSERT_NO_FATAL_FAILURE(assertServerPrimaryParity(predicted, false));
}

TEST_F(BracePreemptiveForecastTest, CounterchargeUsesCanonicalSnapshotNotASecondFormula)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(selectArmorer(attackerSideHero, MasteryLevel::BASIC,
		"new-horizons:armorer.countercharge"));
	arrive(3);
	const auto predicted = forecast();
	ASSERT_TRUE(predicted.primary);
	EXPECT_THAT(predicted.primary->damageRange.attackerOrderCauses, ::testing::Contains(HeroCommand::BRACE));
	ASSERT_NO_FATAL_FAILURE(assertServerPrimaryParity(predicted, true));
}

TEST_F(BracePreemptiveForecastTest, ExtraAttackBonusDoesNotCreateMoreThanOneBraceBlow)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	braced->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_GT(braced->getTotalAttacks(false), 1);
	arrive(3);
	ASSERT_NO_FATAL_FAILURE(assertServerPrimaryParity(forecast(), true));
}

TEST_F(BracePreemptiveForecastTest, GuardianSpiritAbsorbsPhysicalPrimaryExactlyOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	Bonus guardian(BonusDuration::N_TURNS, BonusType::GUARDIAN_SPIRIT,
		BonusSource::SPELL_EFFECT, 100, BonusSourceID(SpellID(SpellID::HASTE)));
	guardian.turnsRemain = 2;
	SetStackEffect effects;
	effects.battleID = BattleID(0);
	effects.toAdd.emplace_back(mover->unitId(), std::vector<Bonus>{guardian});
	gameHandler->sendAndApply(effects);
	// Rebuild the stage from the real, accepted Guardian Spirit state.
	stage = std::make_shared<HypotheticBattle>(environment.get(), live);
	arrive(3);
	const auto predicted = forecast();
	ASSERT_TRUE(predicted.primary);
	ASSERT_EQ(stage->battleGetUnitByID(mover->unitId())->acquireState()->guardianSpiritHitPoints, 100);
	ASSERT_GT(predicted.primary->expectedRequestedDamage, 100);
	EXPECT_EQ(predicted.primary->guardianSpiritAfterPrimary, 0);
	EXPECT_EQ(predicted.primary->expectedPrimaryDamageAmount, predicted.primary->expectedRequestedDamage - 100);
	ASSERT_NO_FATAL_FAILURE(assertServerPrimaryParity(predicted, true));
}

TEST_F(BracePreemptiveForecastTest, LastStandReactionEndsActiveIncomingWithoutNormalRetaliation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, 1));
	stage.reset(); // Select real source perks BEFORE capturing the parity stage.
	ASSERT_NO_FATAL_FAILURE(selectArmorer(defenderSideHero, MasteryLevel::BASIC, "new-horizons:armorer.pavise"));
	ASSERT_NO_FATAL_FAILURE(selectArmorer(defenderSideHero, MasteryLevel::ADVANCED, "new-horizons:armorer.veteran"));
	ASSERT_NO_FATAL_FAILURE(selectArmorer(defenderSideHero, MasteryLevel::EXPERT, "new-horizons:armorer.lastStand"));
	live = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	stage = std::make_shared<HypotheticBattle>(environment.get(), live);
	ASSERT_EQ(stage->battleGetMySide(), BattleSide::ALL_KNOWING);
	ASSERT_EQ(stage->battleGetFightingHero(BattleSide::DEFENDER), defenderSideHero);
	ASSERT_TRUE(newHorizonsArmorer::canTriggerLastStand(
		stage->battleGetFightingHero(BattleSide::DEFENDER), stage->battleGetUnitByID(mover->unitId())));
	arrive(3);
	const auto predicted = forecast();
	ASSERT_TRUE(predicted.primary);
	ASSERT_TRUE(predicted.primary->lastStandTriggered);
	EXPECT_TRUE(predicted.primary->lastStandEndsIncomingActivation);
	EXPECT_EQ(predicted.primary->victimHealthAfterPrimary, 1);
	EXPECT_FALSE(stage->armorerLastStandUsed(BattleSide::DEFENDER));
	ASSERT_NO_FATAL_FAILURE(assertServerPrimaryParity(predicted, false));
	EXPECT_TRUE(battle()->armorerLastStandUsed(BattleSide::DEFENDER));
	EXPECT_TRUE(mover->acquireState()->armorerLastStandEndedActivation);
}

TEST_F(BracePreemptiveForecastTest, RangedCounterMissingAndExpiredStagesAreNotEvents)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	arrive(3);
	EXPECT_EQ(forecast(3, true).coverage, heroOrderValue::BraceStageCoverage::NOT_TRIGGERED);
	EXPECT_EQ(forecast(3, false, true).coverage, heroOrderValue::BraceStageCoverage::NOT_TRIGGERED);
	EXPECT_EQ(heroOrderValue::forecastBraceAtStage(*stage, {UINT32_MAX, braced->unitId(), 3}).coverage,
		heroOrderValue::BraceStageCoverage::NOT_TRIGGERED);
	stage->nextRound();
	EXPECT_EQ(forecast().coverage, heroOrderValue::BraceStageCoverage::NOT_TRIGGERED);
}

TEST_F(BracePreemptiveForecastTest, FrozenOrEndedPreemptorDoesNotProducePrimaryDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	arrive(3);
	auto ended = std::make_shared<HypotheticBattle>(environment.get(), stage);
	ended->getForUpdate(braced->unitId())->armorerLastStandEndedActivation = true;
	const auto stopped = heroOrderValue::forecastBraceAtStage(*ended,
		{mover->unitId(), braced->unitId(), 3});
	EXPECT_EQ(stopped.coverage, heroOrderValue::BraceStageCoverage::BLOCKED_AT_ENTRY);
	EXPECT_TRUE(stopped.event);
	EXPECT_FALSE(stopped.primary);
	stage->addUnitBonus(braced->unitId(), {newHorizonsFrozen::makeFrozenMarker(
		BonusSourceID(SpellID(SpellID::HASTE)), stage->battleGetRound())});
	EXPECT_EQ(forecast().coverage, heroOrderValue::BraceStageCoverage::BLOCKED_AT_ENTRY);
	EXPECT_FALSE(forecast().primary);
}

TEST_F(BracePreemptiveForecastTest, UnprojectedCombatTriggerIsNotAZeroOrCompleteContinuation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	arrive(3);
	for(const auto type : {BonusType::COMBAT_EVENT_TRIGGER, BonusType::ON_COMBAT_EVENT})
	{
		auto child = std::make_shared<HypotheticBattle>(environment.get(), stage);
		child->addUnitBonus(braced->unitId(), {Bonus(BonusDuration::ONE_BATTLE,
			type, BonusSource::OTHER, 1, BonusSourceID())});
		const auto result = heroOrderValue::forecastBraceAtStage(*child,
			{mover->unitId(), braced->unitId(), 3});
		EXPECT_EQ(result.coverage, heroOrderValue::BraceStageCoverage::NEEDS_PRE_HIT_PROJECTION);
		EXPECT_TRUE(result.event);
		EXPECT_FALSE(result.primary);
		EXPECT_FALSE(result.continuationComplete);
	}
}

TEST_F(BracePreemptiveForecastTest, IndependentDamageRollNeedsProjectionNotNeutralLuckFallback)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	arrive(3);
	stage->addUnitBonus(braced->unitId(), {Bonus(BonusDuration::ONE_BATTLE,
		BonusType::DOUBLE_DAMAGE_CHANCE, BonusSource::OTHER, 25, BonusSourceID())});
	const auto result = forecast();
	EXPECT_EQ(result.coverage, heroOrderValue::BraceStageCoverage::NEEDS_PRE_HIT_PROJECTION);
	EXPECT_TRUE(result.event);
	EXPECT_FALSE(result.primary);
}

TEST_F(BracePreemptiveForecastTest, RecursiveProtectRequiresProjectionWithoutSpendingItsAllowance)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	stage.reset(); // Finish native roster construction before projected capture.
	auto * protector = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(75), 50);
	live = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	stage = std::make_shared<HypotheticBattle>(environment.get(), live);
	ASSERT_EQ(stage->battleGetMySide(), BattleSide::ALL_KNOWING);
	arrive(3);
	const auto protect = stage->battlePrepareHeroOrderState(BattleSide::DEFENDER,
		HeroCommand::PROTECT, {protector->unitId(), mover->unitId()});
	ASSERT_TRUE(protect);
	const auto allowance = stage->prepareHeroOrderAllowance(BattleSide::DEFENDER);
	ASSERT_TRUE(allowance);
	const auto effects = stage->beginProjectedHeroOrder(BattleSide::DEFENDER, HeroCommand::PROTECT,
		{protector->unitId(), mover->unitId()}, *allowance);
	ASSERT_TRUE(effects);
	ASSERT_TRUE(stage->projectAcceptedHeroOrder(BattleSide::DEFENDER, *effects, *allowance));
	// Accepted effects install the same prepared mechanical state directly.
	EXPECT_EQ(stage->getHeroOrderState(BattleSide::DEFENDER, HeroCommand::PROTECT), protect);
	const auto installed = stage->getHeroOrderState(BattleSide::DEFENDER, HeroCommand::PROTECT);
	ASSERT_TRUE(installed);
	ASSERT_TRUE(installed->scheduledFor(mover->unitId(), stage->battleGetRound()));
	ASSERT_TRUE(installed->scheduledFor(protector->unitId(), stage->battleGetRound()));
	ASSERT_EQ(stage->battleResolveHeroOrderTarget(stage->battleGetUnitByID(braced->unitId()),
		stage->battleGetUnitByID(mover->unitId()), false)->unitId(), protector->unitId());
	const auto order = serialized(stage->getHeroOrderStates(BattleSide::DEFENDER));
	const auto result = forecast();
	EXPECT_EQ(result.coverage, heroOrderValue::BraceStageCoverage::NEEDS_PRE_HIT_PROJECTION);
	ASSERT_TRUE(result.event);
	EXPECT_EQ(result.event->victimId, mover->unitId()) << "Entry event precedes recursive Protect";
	EXPECT_FALSE(result.primary);
	EXPECT_EQ(serialized(stage->getHeroOrderStates(BattleSide::DEFENDER)), order);
}

TEST_F(BracePreemptiveForecastTest, StageCapturesCurrentHealthRatherThanOriginalIncomingStack)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	arrive(3);
	auto changed = stage->getForUpdate(mover->unitId());
	int64_t priorReactionDamage = 37;
	changed->damage(priorReactionDamage, false, battle::DamageProvenance::PHYSICAL_CREATURE);
	const auto result = forecast();
	ASSERT_TRUE(result.event);
	EXPECT_EQ(result.event->victimHealthAtEntry, changed->getAvailableHealth());
	EXPECT_EQ(result.event->victimHealthAtEntry, mover->getAvailableHealth() - 37);
	EXPECT_EQ(mover->getPosition(), BattleHex(89));
	EXPECT_EQ(result.event->victimPosition, BattleHex(92));
}

TEST_F(BracePreemptiveForecastTest, ForecastDoesNotConsumeBranchStateBonusesRngOrPackets)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	arrive(3);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), live);
	const auto liveMover = mover->acquireState()->save().toString();
	const auto stageMover = stage->battleGetUnitByID(mover->unitId())->acquireState()->save().toString();
	const auto stageBraced = stage->battleGetUnitByID(braced->unitId())->acquireState()->save().toString();
	const auto bonusBytes = heroQueueTest::bonusSnapshots(*braced->getAllBonuses(CSelector([](const Bonus *) { return true; })));
	const auto orders = serialized(stage->getHeroOrderStates(BattleSide::ATTACKER));
	const auto ledger = serialized(stage->getHeroActionAllowances(BattleSide::ATTACKER));
	const auto rng = serialized(*gameHandler->randomizer);
	const auto traffic = std::tuple{server.attacks.size(), server.startedActions.size(), server.orderStateUpdates.size()};
	for(int repetition = 0; repetition < 3; ++repetition)
	{
		const auto result = forecast();
		ASSERT_TRUE(result.primary);
		EXPECT_FALSE(result.continuationComplete);
	}
	EXPECT_EQ(mover->acquireState()->save().toString(), liveMover);
	EXPECT_EQ(sibling->battleGetUnitByID(mover->unitId())->acquireState()->save().toString(), liveMover);
	EXPECT_EQ(stage->battleGetUnitByID(mover->unitId())->acquireState()->save().toString(), stageMover);
	EXPECT_EQ(stage->battleGetUnitByID(braced->unitId())->acquireState()->save().toString(), stageBraced);
	EXPECT_EQ(heroQueueTest::bonusSnapshots(*braced->getAllBonuses(CSelector([](const Bonus *) { return true; }))), bonusBytes);
	EXPECT_EQ(serialized(stage->getHeroOrderStates(BattleSide::ATTACKER)), orders);
	EXPECT_EQ(serialized(stage->getHeroActionAllowances(BattleSide::ATTACKER)), ledger);
	EXPECT_EQ(serialized(*gameHandler->randomizer), rng);
	EXPECT_EQ((std::tuple{server.attacks.size(), server.startedActions.size(), server.orderStateUpdates.size()}), traffic);
}
