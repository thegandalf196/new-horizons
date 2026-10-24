/*
 * DamageCacheAdmissionTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/DamageCacheAdmission.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsShroud.h"
#include <bit>
#include <future>
#include <stdexcept>
#include <utility>

namespace
{
class CountingBearer final : public IBonusBearer
{
	const IBonusBearer & source;
public:
	mutable unsigned queries = 0;
	explicit CountingBearer(const IBonusBearer & source) : source(source) {}
	TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string & key = "") const override
	{
		++queries;
		return source.getAllBonuses(selector, key);
	}
	TConstBonusListPtr getUnstackedBonuses(const CSelector & selector) const override
	{
		++queries;
		return source.getUnstackedBonuses(selector);
	}
	int32_t getTreeVersion() const override { return source.getTreeVersion(); }
};

// Real virtual saved-rules query, with no mocked damage/value implementation.
class CountingRulesBattle final : public BattleInfo
{
public:
	JsonNode rules;
	mutable unsigned queries = 0;
	mutable bool failNext = false;
	explicit CountingRulesBattle(JsonNode rules) : BattleInfo(nullptr), rules(std::move(rules)) {}
	const JsonNode & getHeroCommandRules() const override
	{
		++queries;
		if(failNext)
		{
			failNext = false;
			throw std::runtime_error("fixture saved-rules query failure");
		}
		return rules;
	}
};

class DamageCacheAdmissionTest : public ProjectedTargetHitFixture
{
protected:
	bool legacyRootAdmission = false;
	void mapLoaded(CMap * loaded) override
	{
		ProjectedTargetHitFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		if(legacyRootAdmission)
		{
			// Supported historical command capture, NOT Commands-off. Retain full
			// current hero/perk rules, including strict progression and weights.
			const JsonNode config(JsonPath::builtin("config/newHorizonsCombatV2"));
			auto rules = config["combat"]["heroCommands"];
			rules["schemaVersion"].Integer() = 1;
			rules["rulesetVersion"].Integer() = 1;
			rules["commands"].Struct().erase("focusFire");
			heroCommands::validateRules(rules);
			loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, rules);
		}
	}

	using Snapshot = std::map<uint32_t, std::pair<JsonNode, std::vector<JsonNode>>>;
	Snapshot snapshot()
	{
		Snapshot result;
		for(const auto * unit : child->battleGetAllUnits(false))
		{
			const auto projected = child->getForUpdate(unit->unitId());
			std::vector<JsonNode> bonuses;
			const auto bonusOwner = projected->getUnstackedBonuses(Selector::all);
			for(const auto & bonus : *bonusOwner)
				bonuses.push_back(bonus->toJsonNode());
			result.emplace(unit->unitId(), std::pair{projected->save(), std::move(bonuses)});
		}
		return result;
	}
	void compareMainBatch(const std::shared_ptr<StackWithBonuses> & actor,
		const battle::Units & targets, bool shooting = false)
	{
		DamageCache expectedCache;
		expectedCache.buildDamageCache(child, BattleSide::ATTACKER);
		DamageCache actualCache;
		actualCache.buildDamageCache(child, BattleSide::ATTACKER);
		const auto before = snapshot();
		for(const auto & unit : battle()->stacks)
			(void)unit->counterAttacks.available();
		const auto world = gameState()->saveToMemory();
		const auto defaultRng = projectedHitTest::serialized(CRandomGenerator::getDefault());
		const auto serverRng = projectedHitTest::serialized(*gameHandler->randomizer);
		const auto gameRng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
		const auto spawns = child->getElementalRebirthSpawnUnitIds();
		const auto bursts = child->getProjectedPrimalBurstHits().size();
		DamageCacheAdmission admission(*actor, *child);
		BattleExchangeVariant expected;
		BattleExchangeVariant actual;
		std::optional<bool> expectedOverwatch, actualOverwatch;
		unsigned index = 0;
		for(const auto * target : targets)
		{
			const auto defender = child->getForUpdate(target->unitId());
			const auto reference = expected.trackAttack(actor, defender, shooting, true,
				expectedCache, child, true, true, &expectedOverwatch);
			DamageCacheProbeTiming cacheTiming;
			BattleMovementTiming::StageTiming mainTiming, dpsTiming;
			const BattleTargetProbeTiming probeTiming{&mainTiming, &dpsTiming, &cacheTiming};
			const bool measured = (++index % 2) == 0;
			const auto value = actual.trackAttack(actor, defender, shooting, true,
				actualCache, child, true, true, &actualOverwatch, measured ? &probeTiming : nullptr, &admission);
			EXPECT_EQ(std::bit_cast<uint32_t>(value), std::bit_cast<uint32_t>(reference));
			EXPECT_EQ(snapshot(), before);
			if(measured)
			{
				EXPECT_EQ(mainTiming.calls, 1u);
				EXPECT_EQ(dpsTiming.calls, 1u);
				EXPECT_GE(cacheTiming.getDamageCalls, 1u);
				EXPECT_EQ(cacheTiming.failedOperations, 0u);
				if(cacheTiming.dynamicDamage.calls)
				{
					EXPECT_GT(cacheTiming.calculation.initialCalls, 0u);
					EXPECT_EQ(cacheTiming.calculation.nativePayload.calls,
						cacheTiming.calculation.bonusPreparation.calls);
					EXPECT_EQ(cacheTiming.calculation.nativePayload.calls,
						cacheTiming.calculation.luaCall.calls);
				}
			}
		}
		EXPECT_EQ(gameState()->saveToMemory(), world);
		EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), defaultRng);
		EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), serverRng);
		EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), gameRng);
		EXPECT_EQ(child->getElementalRebirthSpawnUnitIds(), spawns);
		EXPECT_EQ(child->getProjectedPrimalBurstHits().size(), bursts);
	}
};
}

TEST_F(DamageCacheAdmissionTest, ConstructionIsLazyAndActorBonusFactsAreReadOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto original = child->getForUpdate(incoming->unitId());
	CountingBearer counter(*original);
	battle::CUnitStateDetached actor(original.get(), &counter);
	actor.localInit(child.get());
	actor = *original;
	const auto before = counter.queries;
	DamageCacheAdmission admission(actor, *child);
	EXPECT_EQ(counter.queries, before);
	EXPECT_FALSE(admission.matches(*original, *child)); // Same ID is insufficient.
	EXPECT_TRUE(admission.matches(actor, *child));
	EXPECT_FALSE(admission.hasNightProwlerEffect());
	const auto once = counter.queries;
	EXPECT_GT(once, before);
	EXPECT_FALSE(admission.hasNightProwlerEffect());
	EXPECT_EQ(counter.queries, once);
	EXPECT_FALSE(admission.hasGamblerPenalty());
	const auto twice = counter.queries;
	EXPECT_GT(twice, once);
	EXPECT_FALSE(admission.hasGamblerPenalty());
	EXPECT_EQ(counter.queries, twice);
}

TEST_F(DamageCacheAdmissionTest, MainProbesMatchUncachedScoresStateAndBothRngs)
{
	legacyRootAdmission = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * other = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(94), 20);
	resetChild();
	ASSERT_NO_FATAL_FAILURE(compareMainBatch(child->getForUpdate(incoming->unitId()),
		{braced, other, braced, other}));
}

TEST_F(DamageCacheAdmissionTest, WrongActorAndSiblingBranchFallBackToIndependentAdmission)
{
	legacyRootAdmission = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto actor = child->getForUpdate(incoming->unitId());
	const auto defender = child->getForUpdate(braced->unitId());
	DamageCacheAdmission wrongActor(*defender, *child);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), live);
	const auto siblingActor = sibling->getForUpdate(incoming->unitId());
	siblingActor->addUnitBonus({newHorizonsCombatSkills::gamblerLuckPenalty()});
	DamageCacheAdmission wrongBranch(*siblingActor, *sibling);
	ASSERT_TRUE(wrongBranch.hasGamblerPenalty());
	const JsonNode canonical(JsonPath::builtin("config/newHorizonsCombat"));
	CountingRulesBattle otherRules(canonical["combat"]["heroCommands"]);
	DamageCacheAdmission wrongRules(*actor, otherRules);
	ASSERT_TRUE(wrongRules.supportsFocusFire());
	ASSERT_FALSE(wrongActor.supportsFocusFire());
	ASSERT_FALSE(wrongBranch.supportsFocusFire());
	DamageCache cache;
	cache.buildDamageCache(child, BattleSide::ATTACKER);
	const auto expected = cache.getDamage(actor.get(), defender.get(), child);
	for(auto * hint : {&wrongActor, &wrongBranch, &wrongRules})
	{
		DamageCacheProbeTiming timing;
		EXPECT_EQ(cache.getDamage(actor.get(), defender.get(), child, &timing, hint), expected);
		EXPECT_EQ(timing.dynamicBypasses, 0u);
		EXPECT_EQ(timing.rootCacheHits, 1u);
	}
}

TEST_F(DamageCacheAdmissionTest, NewBatchObservesBonusAndActivationChanges)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto actor = child->getForUpdate(incoming->unitId());
	{
		DamageCacheAdmission before(*actor, *child);
		EXPECT_FALSE(before.hasGamblerPenalty());
		EXPECT_FALSE(before.hasNightProwlerEffect());
		EXPECT_FALSE(before.hasReducedExtraActivation());
	}
	actor->addUnitBonus({newHorizonsCombatSkills::gamblerLuckPenalty()});
	actor->addUnitBonus(newHorizonsShroud::nightProwlerDamageBonuses());
	actor->moraleExtraActivation = true;
	{
		DamageCacheAdmission after(*actor, *child);
		EXPECT_TRUE(after.hasGamblerPenalty());
		EXPECT_TRUE(after.hasNightProwlerEffect());
		EXPECT_TRUE(after.hasReducedExtraActivation());
	}
	actor->removeGamblerLuckPenalty();
	actor->removeUnitBonus(CSelector(newHorizonsShroud::isNightProwlerBonus));
	actor->moraleExtraActivation = false;
	DamageCacheAdmission restored(*actor, *child);
	EXPECT_FALSE(restored.hasGamblerPenalty());
	EXPECT_FALSE(restored.hasNightProwlerEffect());
	EXPECT_FALSE(restored.hasReducedExtraActivation());
}

TEST_F(DamageCacheAdmissionTest, DetachedAfterAttackCannotConsumeOriginalActorFacts)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto actor = child->getForUpdate(incoming->unitId());
	actor->afterWait();
	actor->addUnitBonus({newHorizonsCombatSkills::gamblerLuckPenalty()});
	actor->addUnitBonus(newHorizonsShroud::nightProwlerDamageBonuses());
	const auto before = snapshot();
	const auto detached = actor->acquireState();
	ASSERT_NE(detached.get(), actor.get());
	ASSERT_TRUE(detached->waitedThisTurn);
	ASSERT_FALSE(detached->battlecraftWaitBonusUsed);
	detached->afterAttack(true, true);
	EXPECT_TRUE(detached->battlecraftWaitBonusUsed);
	EXPECT_FALSE(actor->battlecraftWaitBonusUsed);
	EXPECT_EQ(snapshot(), before);
	DamageCacheAdmission admission(*actor, *child);
	EXPECT_TRUE(admission.hasGamblerPenalty());
	EXPECT_TRUE(admission.hasNightProwlerEffect());
	ASSERT_NO_FATAL_FAILURE(compareMainBatch(actor, {braced, braced}));
}

TEST_F(DamageCacheAdmissionTest, ProtectTargetsAndReverseDpsKeepIndependentLiveChecks)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * ward = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(94), 20);
	activate(braced->unitId());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makePairedHeroCommand(BattleSide::ATTACKER, HeroCommand::PROTECT,
			braced->unitId(), ward->unitId())));
	activate(incoming->unitId());
	resetChild();
	const auto actor = child->getForUpdate(incoming->unitId());
	ASSERT_EQ(child->battleResolveHeroOrderTarget(actor.get(), ward, false)->unitId(), braced->unitId());
	const auto orders = child->getHeroOrderStates(BattleSide::ATTACKER);
	ASSERT_NO_FATAL_FAILURE(compareMainBatch(actor, {ward, braced, ward}));
	EXPECT_EQ(child->getHeroOrderStates(BattleSide::ATTACKER), orders);
}

TEST_F(DamageCacheAdmissionTest, ParallelSiblingBatchesOwnSeparateFactsAndDamageCaches)
{
	legacyRootAdmission = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), live);
	const auto leftActor = child->getForUpdate(incoming->unitId());
	const auto rightActor = sibling->getForUpdate(incoming->unitId());
	rightActor->addUnitBonus({newHorizonsCombatSkills::gamblerLuckPenalty()});
	const auto before = snapshot();
	const auto rightBefore = rightActor->save();
	const auto serverRng = projectedHitTest::serialized(*gameHandler->randomizer);
	const auto gameRng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto run = [&](const std::shared_ptr<HypotheticBattle> & branch, bool hinted)
	{
		const auto actor = branch->getForUpdate(incoming->unitId());
		const auto target = branch->getForUpdate(braced->unitId());
		DamageCache cache;
		cache.buildDamageCache(branch, BattleSide::ATTACKER);
		DamageCacheAdmission admission(*actor, *branch);
		std::vector<int64_t> values;
		for(unsigned index = 0; index < 4; ++index)
			values.push_back(cache.getDamage(actor.get(), target.get(), branch, nullptr, hinted ? &admission : nullptr));
		return values;
	};
	const auto leftExpected = run(child, false);
	const auto rightExpected = run(sibling, false);
	auto left = std::async(std::launch::async, [&] { return run(child, true); });
	auto right = std::async(std::launch::async, [&] { return run(sibling, true); });
	EXPECT_EQ(left.get(), leftExpected);
	EXPECT_EQ(right.get(), rightExpected);
	EXPECT_EQ(snapshot(), before);
	EXPECT_EQ(rightActor->save(), rightBefore);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), serverRng);
	EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), gameRng);
}

TEST_F(DamageCacheAdmissionTest, CanonicalMainProbesMatchReferenceWithRepeatedMixedTargets)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(heroCommands::supportedByRules(child->getBattle()->getHeroCommandRules(), HeroCommand::FOCUS_FIRE));
	const auto * other = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(94), 20);
	resetChild();
	ASSERT_NO_FATAL_FAILURE(compareMainBatch(child->getForUpdate(incoming->unitId()),
		{braced, other, braced, other}));
}

TEST_F(DamageCacheAdmissionTest, SavedRulesSupportIsLazyAndCachesTrueAndFalseWithoutInventingUnknownSupport)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto actor = child->getForUpdate(incoming->unitId());
	const JsonNode canonical(JsonPath::builtin("config/newHorizonsCombat"));
	const JsonNode targeted(JsonPath::builtin("config/newHorizonsCombatV2"));
	auto legacy = targeted["combat"]["heroCommands"];
	legacy["rulesetVersion"].Integer() = 1;
	legacy["commands"].Struct().erase("focusFire");
	heroCommands::validateRules(legacy);
	auto unknown = canonical["combat"]["heroCommands"];
	unknown["rulesetVersion"].Integer() = 999;
	auto malformed = unknown;
	malformed["rulesetVersion"].String() = "future";
	const std::vector<std::pair<JsonNode, bool>> variants{
		{canonical["combat"]["heroCommands"], true}, {targeted["combat"]["heroCommands"], true},
		{legacy, false}, {unknown, false}, {malformed, false}, {JsonNode(), false}};
	for(const auto & [rules, expected] : variants)
	{
		CountingRulesBattle source(rules);
		DamageCacheAdmission admission(*actor, source);
		EXPECT_EQ(source.queries, 0u);
		EXPECT_EQ(admission.supportsFocusFire(), expected);
		EXPECT_EQ(admission.supportsFocusFire(), heroCommands::supportedByRules(rules, HeroCommand::FOCUS_FIRE));
		EXPECT_EQ(source.queries, 1u);
	}
}

TEST_F(DamageCacheAdmissionTest, FailedRulesQueryDoesNotPublishAndRetryRetainsFirstUseOrdering)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto actor = child->getForUpdate(incoming->unitId());
	const JsonNode canonical(JsonPath::builtin("config/newHorizonsCombat"));
	CountingRulesBattle source(canonical["combat"]["heroCommands"]);
	source.failNext = true;
	DamageCacheAdmission admission(*actor, source);
	EXPECT_EQ(source.queries, 0u);
	EXPECT_TRUE(source.failNext); // Construction did not move this query before other preflight facts.
	EXPECT_THROW(admission.supportsFocusFire(), std::runtime_error);
	EXPECT_EQ(source.queries, 1u);
	EXPECT_TRUE(admission.supportsFocusFire());
	EXPECT_EQ(source.queries, 2u);
	EXPECT_TRUE(admission.supportsFocusFire());
	EXPECT_EQ(source.queries, 2u);
}

TEST_F(DamageCacheAdmissionTest, NewBatchObservesChangedSavedRulesAndCallbackIdentity)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto actor = child->getForUpdate(incoming->unitId());
	const JsonNode canonical(JsonPath::builtin("config/newHorizonsCombat"));
	CountingRulesBattle source(canonical["combat"]["heroCommands"]);
	{
		DamageCacheAdmission first(*actor, source);
		EXPECT_TRUE(first.supportsFocusFire());
		EXPECT_EQ(source.queries, 1u);
	}
	source.rules["rulesetVersion"].Integer() = 999; // Prior immutable batch ended.
	CountingRulesBattle sibling(canonical["combat"]["heroCommands"]);
	DamageCacheAdmission next(*actor, source), independent(*actor, sibling);
	EXPECT_FALSE(next.supportsFocusFire());
	EXPECT_TRUE(independent.supportsFocusFire());
	EXPECT_FALSE(next.matches(*actor, sibling));
	EXPECT_FALSE(independent.matches(*actor, source));
	EXPECT_EQ(source.queries, 2u);
	EXPECT_EQ(sibling.queries, 1u);
}
