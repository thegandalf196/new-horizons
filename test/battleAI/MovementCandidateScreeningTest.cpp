/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "BattlecraftPreemptiveAIFixture.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/MovementCandidateScreening.h"
#include "../../AI/BattleAI/CreatureCandidateScreening.h"
#include "../../AI/BattleAI/BattleSearchPolicy.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/callback/GameRandomizer.h"
#include <array>
#include <chrono>
#include "../../lib/filesystem/NhArtSha256.h"
#include <bit>
#include <cmath>
#include <map>
#include <sstream>

namespace
{
struct ApproachGeometry
{
	ReachabilityInfo reachability;
	BattleHexArray hexes;
	ApproachGeometry()
	{
		reachability.params.startPosition = BattleHex(20);
		reachability.distances[20] = 0;
		reachability.accessibility.fill(EAccessibility::ACCESSIBLE);
		for(int index = 0; index < 6; ++index)
		{
			const BattleHex candidate(100 + index);
			const BattleHex firstTurn(40 + index);
			hexes.insert(candidate);
			reachability.distances[candidate.toInt()] = index == 0 ? 9 : 4 + index;
			reachability.predecessors[candidate.toInt()] = firstTurn;
			reachability.distances[firstTurn.toInt()] = 2;
			reachability.predecessors[firstTurn.toInt()] = reachability.params.startPosition;
		}
	}
};
}

TEST(MovementCandidateScreeningTest, RetainsSeedNearestAndTwoDistinctDestinationsInOriginalOrder)
{
	ApproachGeometry geometry;
	const auto selected = movementCandidateScreening::promisingApproaches(geometry.hexes, geometry.reachability, 3);
	EXPECT_EQ(selected, (BattleHexArray{BattleHex(100), BattleHex(101), BattleHex(102), BattleHex(103)}));
	EXPECT_LT(selected.size(), geometry.hexes.size());
}

TEST(MovementCandidateScreeningTest, ImmediateValuePlateauCannotExpandBudget)
{
	ApproachGeometry geometry;
	for(const auto hex : geometry.hexes)
		geometry.reachability.distances[hex.toInt()] = 8;
	const auto selected = movementCandidateScreening::promisingApproaches(geometry.hexes, geometry.reachability, 3);
	EXPECT_EQ(selected, (BattleHexArray{BattleHex(100), BattleHex(101), BattleHex(102)}));
}

TEST(MovementCandidateScreeningTest, SameFirstTurnDestinationStillOmitsRedundantForecasts)
{
	ApproachGeometry geometry;
	for(const auto hex : geometry.hexes)
		geometry.reachability.predecessors[hex.toInt()] = BattleHex(40);
	const auto selected = movementCandidateScreening::promisingApproaches(geometry.hexes, geometry.reachability, 3);
	EXPECT_EQ(selected, (BattleHexArray{BattleHex(100), BattleHex(101)}));
}

TEST(MovementCandidateScreeningTest, OccupiedRouteAndInvalidPredecessorRetainExhaustiveCandidates)
{
	ApproachGeometry geometry;
	geometry.reachability.accessibility[43] = EAccessibility::ALIVE_STACK;
	EXPECT_EQ(movementCandidateScreening::promisingApproaches(geometry.hexes, geometry.reachability, 3), geometry.hexes);
	geometry.reachability.accessibility[43] = EAccessibility::ACCESSIBLE;
	geometry.reachability.predecessors[43] = BattleHex::INVALID;
	EXPECT_EQ(movementCandidateScreening::promisingApproaches(geometry.hexes, geometry.reachability, 3), geometry.hexes);
}

TEST(MovementCandidateScreeningTest, DoubleWideTailOnlyOccupiedTransitRetainsFullSetForBothSides)
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		ApproachGeometry geometry;
		geometry.reachability.params.doubleWide = true;
		geometry.reachability.params.side = side;
		// First show that ordinary double-wide paths really enter screening.
		EXPECT_LT(movementCandidateScreening::promisingApproaches(geometry.hexes,
			geometry.reachability, 3).size(), geometry.hexes.size());
		const BattleHex head(side == BattleSide::ATTACKER ? 40 : 45);
		const auto tail = battle::Unit::occupiedHex(head, true, side);
		ASSERT_FALSE(geometry.hexes.contains(tail));
		ASSERT_NE(head, tail);
		ASSERT_EQ(geometry.reachability.accessibility[head.toInt()], EAccessibility::ACCESSIBLE);
		geometry.reachability.accessibility[tail.toInt()] = EAccessibility::ALIVE_STACK;
		// Enemy-bypass distance may remain finite even though only the tail
		// touches an occupied node. It must preserve every conditional forecast.
		ASSERT_TRUE(geometry.reachability.isReachable(head));
		EXPECT_EQ(movementCandidateScreening::promisingApproaches(geometry.hexes,
			geometry.reachability, 3), geometry.hexes);
	}
}

TEST(MovementCandidateScreeningTest, NonOrdinaryGeometryAndSmallPoolsRemainExhaustive)
{
	ApproachGeometry geometry;
	EXPECT_EQ(movementCandidateScreening::promisingApproaches(geometry.hexes, geometry.reachability, 0), geometry.hexes);
	geometry.reachability.params.flying = true;
	EXPECT_EQ(movementCandidateScreening::promisingApproaches(geometry.hexes, geometry.reachability, 3), geometry.hexes);
	geometry.reachability.params.flying = false;
	geometry.reachability.params.ghostWalk = true;
	EXPECT_EQ(movementCandidateScreening::promisingApproaches(geometry.hexes, geometry.reachability, 3), geometry.hexes);
	geometry.reachability.params.ghostWalk = false;
	const BattleHexArray small{BattleHex(100), BattleHex(101)};
	EXPECT_EQ(movementCandidateScreening::promisingApproaches(small, geometry.reachability, 3), small);
}

namespace
{
class MovementCandidateForecastTest : public battlecraftTest::PreemptiveFixture
{
protected:
	CStack * actor = nullptr;
	CStack * enemy = nullptr;

	void prepare()
	{
		ASSERT_NO_FATAL_FAILURE(startGame());
		// Native fixture has already neutralized secondary skill/perk effects.
		// Rissa's resource-only identity is an explicitly supported own policy.
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:rissa")));
		ASSERT_NO_FATAL_FAILURE(startBattle());
		actor = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(2, 5), 80);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(12, 5), 80);
		removeOtherStacks({actor, enemy});
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = actor->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
		for(const auto & unit : battle()->stacks)
			(void)unit->counterAttacks.available();
		initializeAI(PlayerColor(0));
		ASSERT_EQ(actor->getMovementRange(), enemy->getMovementRange());
		ASSERT_EQ(battle()->battleAliveUnits().size(), 2);
	}

	auto worldAndRng()
	{
		return std::array<std::vector<std::byte>, 4>{gameState()->saveToMemory(),
			projectedHitTest::serialized(*gameHandler->randomizer),
			projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())),
			projectedHitTest::serialized(CRandomGenerator::getDefault())};
	}

	static auto units(const HypotheticBattle & model)
	{
		std::map<uint32_t, JsonNode> result;
		for(const auto * unit : model.battleGetAllUnits(false))
			result.emplace(unit->unitId(), unit->acquireState()->save());
		return result;
	}

	// Test-local full search for exactly two equal-speed single-wide Pikemen:
	// no ally penalty, multiplier one, no blocker/bypass branch. Uses public
	// full evaluateExchange and the same AP construction/ordinal/tie policy.
	MoveTarget exhaustive(const std::shared_ptr<HypotheticBattle> & model,
		DamageCache & damage, PotentialTargets & targets, uint64_t & calls)
	{
		MoveTarget result;
		const auto reachability = getReachabilityWithEnemyBypass(actor, damage, model);
		const auto distance = reachability.distToNearestNeighbour(actor, enemy);
		const auto speed = actor->getMovementRange();
		const auto turns = (distance - 1) / speed + 1;
		BattleExchangeEvaluator evaluator(callback->getBattle(BattleID(0)), environment, 0.5f, 2);
		evaluator.updateReachabilityMap(model);
		for(const auto hex : enemy->getAttackableHexes(actor))
		{
			if(!reachability.isReachable(hex)) continue;
			auto attack = AttackPossibility::evaluate(BattleAttackInfo(actor, enemy, 0,
				callback->getBattle(BattleID(0))->battleCanShoot(actor)), hex, damage, model);
			attack.shootersBlockedDmg = 0;
			const float score = evaluator.evaluateExchange(attack, turns, targets, damage, model);
			++calls;
			if(result.score < score || (result.turnsToReach > turns && vstd::isAlmostEqual(result.score, score)))
			{
				result.score = score;
				result.positions.clear();
				auto destination = hex;
				while(reachability.distances[destination.toInt()] > speed
					&& reachability.predecessors[destination.toInt()].isValid())
					destination = reachability.predecessors[destination.toInt()];
				result.positions.insert(destination);
				result.cachedAttack = attack;
				result.turnsToReach = turns;
			}
		}
		return result;
	}
};
}

TEST_F(MovementCandidateForecastTest, VisibleCallbackOmitsActualFullForecastsAndRecordsFullSearchRegret)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto before = worldAndRng();
	auto model = simulation();
	ASSERT_EQ(model->battleGetVisibleHeroPolicy(BattleSide::DEFENDER).kind(),
		VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
	ASSERT_TRUE(creatureCandidateScreening::supportsVisibleOrdinaryMelee(actor, *model));
	DamageCache damage;
	damage.buildDamageCache(model, BattleSide::ATTACKER);
	PotentialTargets targets(actor, damage, model);
	ASSERT_TRUE(targets.possibleAttacks.empty());
	ASSERT_EQ(targets.unreachableEnemies.size(), 1);
	const auto stateBefore = units(*model);
	BattleExchangeEvaluator evaluator(callback->getBattle(BattleID(0)), environment, 0.5f, 2);
	BattleMovementTiming timing;
	const auto selected = evaluator.findMoveTowardsUnreachable(actor, targets, damage, model, &timing);
	EXPECT_EQ(units(*model), stateBefore);
	ASSERT_TRUE(selected.cachedAttack);
	ASSERT_FALSE(selected.positions.empty());

	auto referenceModel = simulation();
	DamageCache referenceDamage;
	referenceDamage.buildDamageCache(referenceModel, BattleSide::ATTACKER);
	PotentialTargets referenceTargets(actor, referenceDamage, referenceModel);
	const auto referenceBefore = units(*referenceModel);
	uint64_t fullCalls = 0;
	const auto full = exhaustive(referenceModel, referenceDamage, referenceTargets, fullCalls);
	EXPECT_EQ(units(*referenceModel), referenceBefore);
	EXPECT_EQ(worldAndRng(), before);
	ASSERT_TRUE(full.cachedAttack);
	EXPECT_GT(fullCalls, timing.findMoveExchange.calls);
	EXPECT_GT(timing.findMoveExchange.calls, 0);
	EXPECT_LE(timing.findMoveExchange.calls, 4);
	EXPECT_TRUE(std::isfinite(selected.score));
	EXPECT_TRUE(std::isfinite(full.score));
	EXPECT_LE(selected.score, full.score + 0.01f);
	RecordProperty("ScreenedFullExchangeCalls", std::to_string(timing.findMoveExchange.calls));
	RecordProperty("ReferenceFullExchangeCalls", std::to_string(fullCalls));
	RecordProperty("ScreenedScoreBits", std::to_string(std::bit_cast<uint32_t>(selected.score)));
	RecordProperty("ReferenceScoreBits", std::to_string(std::bit_cast<uint32_t>(full.score)));
	RecordProperty("ObservedFullSearchRegret", std::to_string(full.score - selected.score));
	RecordProperty("ScreenedApproachHex", std::to_string(selected.cachedAttack->from.toInt()));
	RecordProperty("ReferenceApproachHex", std::to_string(full.cachedAttack->from.toInt()));
	const auto destination = *selected.positions.begin();
	const auto normalReachability = model->getReachability(actor);
	EXPECT_TRUE(model->battleGetAvailableHexes(normalReachability, actor, false).contains(destination));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMove(actor, destination)));
	EXPECT_EQ(actor->getPosition(), destination);
}

namespace
{
class MovementCreatureDecisionProfileTest : public MovementCandidateForecastTest
{
protected:
	void configureWorldInitialization(StartInfo & info, GameRandomizer & randomizer) const override
	{
		info.startTime = 1'700'000'000;
		randomizer.setSeed(1337);
	}
};
}

// Explicit isolated creature-decision observations; no Hero Action/callback/turn timing.
TEST_F(MovementCreatureDecisionProfileTest, DISABLED_NullWholeCreatureDecisionTimingProfile)
{
	dynamic_cast<CRandomGenerator &>(*getRNG()).setSeed(1337);
	CRandomGenerator::getDefault().setSeed(1337);
	ASSERT_NO_FATAL_FAILURE(prepare());
	CRandomGenerator::getDefault().setSeed(1337);
	const auto before = worldAndRng();
	const auto digest = [](const std::vector<std::byte> & bytes)
	{
		return nhart::hex(nhart::sha256(std::span<const ui8>(
			reinterpret_cast<const ui8 *>(bytes.data()), bytes.size())));
	};
	const std::array<const char *, 4> captureNames{"World", "ServerRng", "GameRng", "DefaultRng"};
	for(size_t index = 0; index < before.size(); ++index)
	{
		RecordProperty(std::string("CreatureDecision") + captureNames[index] + "BeforeSHA256", digest(before[index]));
		RecordProperty(std::string("CreatureDecision") + captureNames[index] + "BeforeBytes", std::to_string(before[index].size()));
	}
	const auto run = [&](BattleEvaluator::SelectionTiming * timing)
	{
		// Fresh HB, damage cache and candidates each time; evaluator destruction
		// finishes inside the lambda, before the measured caller reads its clock.
		BattleEvaluator evaluator(environment, callback, actor, PlayerColor(0), BattleID(0),
			BattleSide::ATTACKER, 0.5f, 2);
		return evaluator.selectStackAction(actor, timing);
	};
	const auto warmup = run(nullptr);
	const auto expectedBytes = projectedHitTest::serialized(warmup);
	const auto targets = [&](const BattleAction & action)
	{
		std::vector<std::pair<int, int64_t>> result;
		for(const auto & target : action.getTarget(battle()))
			result.emplace_back(target.hexValue.toInt(), target.unitValue
				? static_cast<int64_t>(target.unitValue->unitId()) : -1);
		return result;
	};
	const auto expectedTargets = targets(warmup);
	ASSERT_EQ(worldAndRng(), before);
	std::ostringstream actionBytes;
	for(const auto byte : expectedBytes) actionBytes << std::hex << std::to_integer<unsigned>(byte) << '.';
	RecordProperty("CreatureDecisionExpectedActionBytes", actionBytes.str());
	RecordProperty("CreatureDecisionExpectedActionSHA256", digest(expectedBytes));
	RecordProperty("CreatureDecisionExpectedAction", warmup.toString());
	// Separate observer/count diagnostic. Its elapsed values are not samples.
	BattleEvaluator::SelectionTiming diagnostic;
	const auto diagnosticAction = run(&diagnostic);
	ASSERT_EQ(projectedHitTest::serialized(diagnosticAction), expectedBytes);
	ASSERT_TRUE(diagnosticAction == warmup);
	ASSERT_EQ(targets(diagnosticAction), expectedTargets);
	ASSERT_EQ(worldAndRng(), before);
	RecordProperty("CreatureDecisionDiagnosticNormalExchangeCalls", std::to_string(diagnostic.bestTargetDetails.normalExchange.calls));
	RecordProperty("CreatureDecisionDiagnosticWaitedExchangeCalls", std::to_string(diagnostic.bestTargetDetails.waitedExchange.calls));
	RecordProperty("CreatureDecisionDiagnosticMovementExchangeCalls", std::to_string(diagnostic.movementDetails.findMoveExchange.calls));
	EXPECT_GT(diagnostic.bestTargetDetails.normalExchange.calls + diagnostic.bestTargetDetails.waitedExchange.calls
		+ diagnostic.movementDetails.findMoveExchange.calls, 0u);
	for(unsigned index = 0; index < 4; ++index)
	{
		SCOPED_TRACE("NULLsample" + std::to_string(index));
		const auto started = std::chrono::steady_clock::now();
		const auto action = run(nullptr);
		const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now() - started).count();
		// Exact serialized state/RNG/action/target comparisons stay outside clocks.
		EXPECT_EQ(worldAndRng(), before);
		EXPECT_EQ(projectedHitTest::serialized(action), expectedBytes);
		EXPECT_TRUE(action == warmup);
		EXPECT_EQ(action.toString(), warmup.toString());
		EXPECT_EQ(targets(action), expectedTargets);
		RecordProperty("CreatureDecisionNullSample" + std::to_string(index) + "WallNs", std::to_string(elapsed));
	}
	RecordProperty("CreatureDecisionClockScope", "fresh BattleEvaluator construction/selectStackAction(NULL)/destruction; four samples; no HeroAction/fullcallback/turn");
	RecordProperty("CreatureDecisionPreparation", "current native fixture; world startTime1700000000; all seeds1337; one NULL warmup then one separately instrumented diagnostic; native/global caches retained");
	RecordProperty("CreatureDecisionForecast", "current NH; strengthRatio0.5; two-round horizon; fixture default difficulty unchanged; no timing threshold");
}

TEST(MovementCandidateScreeningTest, DiagnosticBeamKeepsFirstMaximumAndInvalidValuesKeepFullSearch)
{
	const std::array<float, 4> values{-4.0f, 7.0f, 7.0f, 2.0f};
	EXPECT_EQ(creatureCandidateScreening::forecastBeamIndex(values), 1u);
	const std::array<float, 2> invalid{7.0f, std::numeric_limits<float>::quiet_NaN()};
	EXPECT_FALSE(creatureCandidateScreening::forecastBeamIndex(invalid));
	EXPECT_FALSE(creatureCandidateScreening::forecastBeamIndex(std::span<const float>{}));
}

TEST_F(MovementCandidateForecastTest, ProductionQuickProfileRetainsNativeAdmissionFallbacksAndReferenceOverride)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto model = simulation();
	const auto before = worldAndRng();
	EXPECT_FALSE(creatureCandidateScreening::supportsForecastBeam(actor, *model, false, false));
	{
		const battleSearch::Scope quick(battleSearch::Profile::BeamOneTwoActors);
		EXPECT_TRUE(creatureCandidateScreening::supportsForecastBeam(actor, *model, false, false));
		EXPECT_EQ(creatureCandidateScreening::admittedFutureActorLimit(actor, *model, false), 2u);
		EXPECT_FALSE(creatureCandidateScreening::supportsForecastBeam(nullptr, *model, false, false));
		EXPECT_FALSE(creatureCandidateScreening::supportsForecastBeam(actor, *model, true, false));
		EXPECT_FALSE(creatureCandidateScreening::supportsForecastBeam(actor, *model, false, true));
		EXPECT_EQ(creatureCandidateScreening::admittedFutureActorLimit(nullptr, *model, false), 0u);
		const battleSearch::Scope full(battleSearch::Profile::Full);
		EXPECT_FALSE(creatureCandidateScreening::supportsForecastBeam(actor, *model, false, false));
		EXPECT_EQ(creatureCandidateScreening::admittedFutureActorLimit(actor, *model, false), 0u);
	}
	EXPECT_FALSE(battleSearch::forecastBeamOne());
	EXPECT_EQ(worldAndRng(), before);
}
