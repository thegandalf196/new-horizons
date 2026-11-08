/*
 * DamageCacheAdmissionTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/DamageCacheAdmission.h"
#include "../../AI/BattleAI/DreadCavalryProjection.h"
#include "../../AI/BattleAI/NewHorizonsHexOfPain.h"
#include "../../lib/battle/NewHorizonsBattlecraft.h"
#include "../../lib/battle/DamageCalculationTiming.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/combatScripts/NativeDamageCalculator.h"
#include "../../lib/scripting/ScriptHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/BattleLayout.h"
#include "../../lib/int3.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsShroud.h"
#include "../../lib/entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
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

// Count the real callback boundary; retain native specialty validation and policy lookup.
class CountingHeroBattle final : public BattleInfo
{
public:
	const CGHeroInstance * attacker = nullptr;
	const CGHeroInstance * defender = nullptr;
	mutable std::vector<BattleSide> queries;
	mutable bool failNextHero = false;
	CountingHeroBattle() : BattleInfo(nullptr) {}
	const CGHeroInstance * getSideHero(BattleSide side) const override
	{
		queries.push_back(side);
		if(failNextHero)
		{
			failNextHero = false;
			throw std::runtime_error("fixture visible-hero query failure");
		}
		return side == BattleSide::ATTACKER ? attacker : defender;
	}
};

// Query-only missing-capability view over an existing final branch. Never used
// as a projection source, world recipe, or mutation authority.
class MissingFocusRulesView final : public BattleProxy
{
	JsonNode rules;
public:
	explicit MissingFocusRulesView(std::shared_ptr<HypotheticBattle> source)
		: BattleProxy(source), rules(source->getBattle()->getHeroCommandRules())
	{
		rules["commands"].Struct().erase("focusFire");
	}
	const JsonNode & getHeroCommandRules() const override { return rules; }
	BattleID getBattleID() const override { return subject->getBattle()->getBattleID(); }
	const scripting::Pool & getScriptContextPool() const override { return subject->getBattle()->getScriptContextPool(); }
	std::vector<SpellID> getUsedSpells(BattleSide side) const override { return subject->getBattle()->getUsedSpells(side); }
	uint32_t nextUnitId() const override { return subject->getBattle()->nextUnitId(); }
	int64_t getActualDamage(const DamageRange &, int32_t, vstd::RNG &) const override
	{
		throw std::logic_error("Missing-Focus query view cannot realize damage");
	}
	int3 getLocation() const override { return subject->getBattle()->getLocation(); }
	BattleLayout getLayout() const override { return subject->getBattle()->getLayout(); }
	void nextRound() override { rejectMutation(); }
	void nextTurn(uint32_t, BattleUnitTurnReason) override { rejectMutation(); }
	void addUnit(uint32_t, const JsonNode &) override { rejectMutation(); }
	void updateUnit(uint32_t, const JsonNode &, int64_t) override { rejectMutation(); }
	void moveUnit(uint32_t, const BattleHex &) override { rejectMutation(); }
	void removeUnit(uint32_t) override { rejectMutation(); }
	void addUnitBonus(uint32_t, const std::vector<Bonus> &) override { rejectMutation(); }
	void updateUnitBonus(uint32_t, const std::vector<Bonus> &) override { rejectMutation(); }
	void removeUnitBonus(uint32_t, const std::vector<Bonus> &) override { rejectMutation(); }
	void setWallState(EWallPart, EWallState) override { rejectMutation(); }
	void addObstacle(const ObstacleChanges &) override { rejectMutation(); }
	void updateObstacle(const ObstacleChanges &) override { rejectMutation(); }
	void removeObstacle(uint32_t) override { rejectMutation(); }
private:
	[[noreturn]] static void rejectMutation() { throw std::logic_error("Missing-Focus query view cannot mutate battle"); }
};
static_assert(!std::is_abstract_v<MissingFocusRulesView>);

class DamageCacheAdmissionTest : public ProjectedTargetHitFixture
{
protected:
	bool finiteProtectRoster = false;
	void mapLoaded(CMap * loaded) override
	{
		useRevisedOrders = true;
		ProjectedTargetHitFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		if(finiteProtectRoster)
		{
			// Authored custom roster in a current-format game, not a historical save
			// or canonical V4 roster. Keep finite payment and current perk policies.
			const JsonNode file(JsonPath::builtin("config/newHorizonsCombatV3"));
			auto rules = file["combat"]["heroCommands"];
			rules["commandResourcePolicyVersion"].Integer() = 1;
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
		ASSERT_NE(dynamic_cast<const NativeDamageCalculator *>(LIBRARY->scriptTypes()->getDamageCalculator()), nullptr);
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
		const auto masks = std::pair{child->getElementalAttunementMask(BattleSide::ATTACKER),
			child->getElementalAttunementMask(BattleSide::DEFENDER)};
		const auto resources = child->getCommandResources(BattleSide::ATTACKER);
		const auto opponentResourcesKnown = child->hasCommandResourceObservation(BattleSide::DEFENDER);
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
						cacheTiming.calculation.initialCalls + cacheTiming.calculation.luckySecondCalls
						+ cacheTiming.calculation.unluckySecondCalls);
					EXPECT_EQ(cacheTiming.calculation.bonusPreparation.calls, 0u);
					EXPECT_EQ(cacheTiming.calculation.bonusPreparation.nanoseconds, 0u);
					EXPECT_EQ(cacheTiming.calculation.luaCall.calls, 0u);
					EXPECT_EQ(cacheTiming.calculation.luaCall.nanoseconds, 0u);
				}
			}
		}
		EXPECT_EQ(gameState()->saveToMemory(), world);
		EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), defaultRng);
		EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), serverRng);
		EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), gameRng);
		EXPECT_EQ((std::pair{child->getElementalAttunementMask(BattleSide::ATTACKER),
			child->getElementalAttunementMask(BattleSide::DEFENDER)}), masks);
		EXPECT_EQ(child->getCommandResources(BattleSide::ATTACKER), resources);
		EXPECT_EQ(child->hasCommandResourceObservation(BattleSide::DEFENDER), opponentResourcesKnown);
	}
};
}

TEST_F(DamageCacheAdmissionTest, ConstructionIsLazyAndActorBonusFactsAreReadOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	// A generic counting bearer proves no opaque hidden-unit provenance.
	const auto hidden = child->getForUpdate(incoming->unitId());
	CountingBearer unsealed(*hidden);
	battle::CUnitStateDetached refused(hidden.get(), &unsealed);
	EXPECT_THROW(refused.localInit(child.get()), std::invalid_argument);
	const auto original = child->getForUpdate(braced->unitId());
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
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * other = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(94), 20);
	resetChild();
	ASSERT_NO_FATAL_FAILURE(compareMainBatch(child->getForUpdate(incoming->unitId()),
		{braced, other, braced, other}));
}

TEST_F(DamageCacheAdmissionTest, WrongActorAndSiblingBranchFallBackToIndependentAdmission)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto actor = child->getForUpdate(incoming->unitId());
	const auto defender = child->getForUpdate(braced->unitId());
	const auto view = std::make_shared<MissingFocusRulesView>(child);
	DamageCacheAdmission wrongActor(*defender, *view);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), live);
	const auto siblingActor = sibling->getForUpdate(incoming->unitId());
	siblingActor->addUnitBonus({newHorizonsCombatSkills::gamblerLuckPenalty()});
	const auto siblingView = std::make_shared<MissingFocusRulesView>(sibling);
	DamageCacheAdmission wrongBranch(*siblingActor, *siblingView);
	ASSERT_TRUE(wrongBranch.hasGamblerPenalty());
	const JsonNode canonical(JsonPath::builtin("config/newHorizonsCombat"));
	CountingRulesBattle otherRules(canonical["combat"]["heroCommands"]);
	DamageCacheAdmission wrongRules(*actor, otherRules);
	ASSERT_TRUE(wrongRules.supportsFocusFire());
	ASSERT_FALSE(wrongActor.supportsFocusFire());
	ASSERT_FALSE(wrongBranch.supportsFocusFire());
	DamageCache cache;
	cache.buildDamageCache(child, BattleSide::ATTACKER);
	const auto expected = cache.getDamage(actor.get(), defender.get(), view);
	for(auto * hint : {&wrongActor, &wrongBranch, &wrongRules})
	{
		DamageCacheProbeTiming timing;
		EXPECT_EQ(cache.getDamage(actor.get(), defender.get(), view, &timing, hint), expected);
		EXPECT_EQ(timing.dynamicBypasses, 0u);
		EXPECT_EQ(timing.rootCacheHits, 1u);
		DamageCacheProbeTiming currentTiming;
		EXPECT_EQ(cache.getDamage(actor.get(), defender.get(), child, &currentTiming, hint), expected);
		EXPECT_EQ(currentTiming.dynamicBypasses, 1u);
		EXPECT_EQ(currentTiming.rootCacheHits, 0u);
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
	finiteProtectRoster = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	child.reset(); // End the observation interval before native roster/action changes.
	const auto * ward = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(94), 20);
	activate(braced->unitId());
	const auto commandBefore = battle()->getCommandResources(BattleSide::ATTACKER);
	const auto actionsBefore = battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(battle()->getRound()).heroActions;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makePairedHeroCommand(BattleSide::ATTACKER, HeroCommand::PROTECT,
			braced->unitId(), ward->unitId())));
	EXPECT_EQ(battle()->getCommandResources(BattleSide::ATTACKER).remaining, commandBefore.remaining - 1);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(battle()->getRound()).heroActions,
		actionsBefore - 1);
	activate(incoming->unitId());
	resetChild();
	const auto actor = child->getForUpdate(incoming->unitId());
	ASSERT_EQ(child->battleResolveHeroOrderTarget(actor.get(), ward, false)->unitId(), braced->unitId());
	const auto orders = child->getHeroOrderStates(BattleSide::ATTACKER);
	ASSERT_NO_FATAL_FAILURE(compareMainBatch(actor, {ward, braced, ward}));
	EXPECT_EQ(child->getHeroOrderStates(BattleSide::ATTACKER), orders);
}

TEST_F(DamageCacheAdmissionTest, CurrentV4ProtectAttemptIsUnavailableAndSpendsNoCommandOrHeroAction)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	child.reset();
	const auto * ward = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(94), 20);
	activate(braced->unitId());
	ASSERT_TRUE(heroCommands::isRevisedRules(battle()->getHeroCommandRules()));
	ASSERT_FALSE(heroCommands::supportedByRules(battle()->getHeroCommandRules(), HeroCommand::PROTECT));
	const auto resources = battle()->getCommandResources(BattleSide::ATTACKER);
	const auto actions = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makePairedHeroCommand(BattleSide::ATTACKER, HeroCommand::PROTECT, braced->unitId(), ward->unitId())));
	EXPECT_EQ(battle()->getCommandResources(BattleSide::ATTACKER), resources);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), actions);
}

TEST_F(DamageCacheAdmissionTest, ParallelSiblingBatchesOwnSeparateFactsAndDamageCaches)
{
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
	EXPECT_THROW(heroCommands::validateRules(legacy), std::runtime_error);
	auto unknown = canonical["combat"]["heroCommands"];
	unknown["rulesetVersion"].Integer() = 999;
	auto malformed = unknown;
	malformed["rulesetVersion"].String() = "future";
	auto missingFocus = canonical["combat"]["heroCommands"];
	missingFocus["commands"].Struct().erase("focusFire");
	const std::vector<std::pair<JsonNode, bool>> variants{
		{canonical["combat"]["heroCommands"], true}, {missingFocus, false},
		{targeted["combat"]["heroCommands"], false},
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

TEST_F(DamageCacheAdmissionTest, SelectedOrdinaryProbeAndFirstApplicationRepeatDamageWork)
{
	useRevisedOrders = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto actor = child->getForUpdate(incoming->unitId());
	auto target = child->getForUpdate(braced->unitId());
	ASSERT_NE(actor, nullptr);
	ASSERT_NE(target, nullptr);
	ASSERT_TRUE(actor->alive() && target->alive());
	ASSERT_TRUE(newHorizonsCombatSkills::isPhysicalCreatureAttack(actor.get(), true));
	ASSERT_FALSE(child->battleMatchOwner(actor.get(), target.get(), true));
	for(const auto & unit : {actor, target})
		unit->addUnitBonus(std::vector<Bonus>{Bonus(BonusDuration::ONE_BATTLE,
			BonusType::NO_LUCK, BonusSource::OTHER, 1, BonusSourceID())});
	actor->addUnitBonus(std::vector<Bonus>{Bonus(BonusDuration::ONE_BATTLE,
		BonusType::BLOCKS_RETALIATION, BonusSource::OTHER, 1, BonusSourceID())});
	ASSERT_EQ(child->battleResolveHeroOrderTarget(actor.get(), target.get(), false), target.get());
	ASSERT_EQ(newHorizonsBattlecraft::preemptiveStrikeDamagePercent(
		child->battleGetOwnerHero(target.get()), target.get(), child->battleGetRound()), 0);
	ASSERT_FALSE(child->battleCanTriggerHeroOrderBrace(actor.get(), target.get(), 3, false, false));
	const auto alive = child->battleAliveUnits();
	ASSERT_FALSE(std::any_of(alive.begin(), alive.end(), [&](const auto * unit)
		{ return child->battleOverwatchReady(unit); }));
	ASSERT_FALSE(dreadCavalryAI::hasActivationState(actor.get()));
	ASSERT_EQ(actor->creatureId(), creatureByName("core:pikeman"));
	ASSERT_FALSE(newHorizonsHexOfPainAI::hasEffect(actor.get()));
	ASSERT_FALSE(child->battleCanTriggerCleave(actor.get()));
	DamageCache cache;
	cache.buildDamageCache(child, BattleSide::DEFENDER);
	const auto before = snapshot();
	const auto world = gameState()->saveToMemory();
	const auto serverRng = projectedHitTest::serialized(*gameHandler->randomizer);
	const auto gameRng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto defaultRng = projectedHitTest::serialized(CRandomGenerator::getDefault());
	const auto targetHealthBefore = target->getAvailableHealth();
	const auto cacheBefore = cache.captureSnapshot();
	DamageCacheAdmission admission(*actor, *child);
	BattleExchangeVariant probe, applied;
	std::optional<bool> overwatch;
	damageCalculationTiming::Sample probeCalculations, appliedCalculations;
	const auto probeValue = [&]()
	{
		damageCalculationTiming::Binding measured(&probeCalculations);
		return probe.trackAttack(actor, target, false, true, cache, child,
			true, true, &overwatch, nullptr, &admission);
	}();
	EXPECT_EQ(snapshot(), before);
	EXPECT_EQ(target->getAvailableHealth(), targetHealthBefore);
	EXPECT_EQ(gameState()->saveToMemory(), world);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), serverRng);
	EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), gameRng);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), defaultRng);
	const bool probeCacheUnchanged = cache.matchesSnapshot(cacheBefore);
	const auto appliedValue = [&]()
	{
		damageCalculationTiming::Binding measured(&appliedCalculations);
		return applied.trackAttack(actor, target, false, true, cache, child);
	}();
	EXPECT_GT(probeValue, 0.0f);
	EXPECT_GT(appliedValue, 0.0f);
	EXPECT_LT(target->getAvailableHealth(), targetHealthBefore);
	EXPECT_EQ(std::bit_cast<uint32_t>(probeValue), std::bit_cast<uint32_t>(appliedValue));
	EXPECT_GT(probeCalculations.initialCalls, 0u);
	EXPECT_GT(appliedCalculations.initialCalls, 0u);
	EXPECT_EQ(gameState()->saveToMemory(), world);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), serverRng);
	EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), gameRng);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), defaultRng);
	RecordProperty("selectedProbeValueBits", std::to_string(std::bit_cast<uint32_t>(probeValue)));
	RecordProperty("firstApplicationValueBits", std::to_string(std::bit_cast<uint32_t>(appliedValue)));
	RecordProperty("probeNativeInitialCalls", std::to_string(probeCalculations.initialCalls));
	RecordProperty("appliedNativeInitialCalls", std::to_string(appliedCalculations.initialCalls));
	RecordProperty("probeCacheUnchanged", probeCacheUnchanged ? "true" : "false");
}

TEST_F(DamageCacheAdmissionTest, OrdinaryPolicyCharacterizesCapturedIdentityAndCoefficient)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:tyraxor")));
	CountingHeroBattle source;
	source.attacker = attackerSideHero;
	auto & captured = const_cast<JsonNode &>(attackerSideHero->getPrimaryGrowthRules());
	auto & policy = captured["additionalSpecialties"]["ordinaryCreatureSpecialties"];
	const auto installedCoefficient = policy["ridersExploitDamagePercent"].Integer();
	ASSERT_GT(installedCoefficient, 0);
	EXPECT_TRUE(newHorizonsHeroes::ordinaryCreatureDamageIsStateful(source));
	EXPECT_EQ(source.queries, (std::vector<BattleSide>{BattleSide::ATTACKER}));
	policy["ridersExploitDamagePercent"].Integer() = 0;
	source.queries.clear();
	EXPECT_FALSE(newHorizonsHeroes::ordinaryCreatureDamageIsStateful(source));
	EXPECT_EQ(source.queries, (std::vector<BattleSide>{BattleSide::ATTACKER, BattleSide::DEFENDER}));
	policy.Struct().erase("ridersExploitDamagePercent"); // Historical absence stays inactive.
	EXPECT_FALSE(newHorizonsHeroes::ordinaryCreatureDamageIsStateful(source));
	source.attacker = nullptr;
	source.defender = attackerSideHero;
	EXPECT_FALSE(newHorizonsHeroes::ordinaryCreatureDamageIsStateful(source));
	policy["ridersExploitDamagePercent"].Integer() = installedCoefficient;
	source.queries.clear();
	EXPECT_TRUE(newHorizonsHeroes::ordinaryCreatureDamageIsStateful(source));
	EXPECT_EQ(source.queries, (std::vector<BattleSide>{BattleSide::ATTACKER, BattleSide::DEFENDER}));
}

TEST_F(DamageCacheAdmissionTest, OrdinaryPolicyMainProbesMatchScoresStateAndRngsWithBothPresenceStates)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:tyraxor")));
	const auto * other = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(94), 20);
	resetChild();
	ASSERT_TRUE(newHorizonsHeroes::ordinaryCreatureDamageIsStateful(*child));
	ASSERT_NO_FATAL_FAILURE(compareMainBatch(child->getForUpdate(incoming->unitId()),
		{braced, other, braced, other}));
	// End every admission lifetime before changing captured policy and constructing a new batch.
	auto & policy = const_cast<JsonNode &>(attackerSideHero->getPrimaryGrowthRules())
		["additionalSpecialties"]["ordinaryCreatureSpecialties"];
	policy["ridersExploitDamagePercent"].Integer() = 0;
	resetChild();
	ASSERT_FALSE(newHorizonsHeroes::ordinaryCreatureDamageIsStateful(*child));
	ASSERT_NO_FATAL_FAILURE(compareMainBatch(child->getForUpdate(incoming->unitId()),
		{braced, other, braced, other}));
}

TEST_F(DamageCacheAdmissionTest, OrdinaryPolicyAdmissionIsLazyAndRetainsTrueAndFalse)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:tyraxor")));
	const auto actor = child->getForUpdate(incoming->unitId());
	CountingHeroBattle source;
	source.attacker = attackerSideHero;
	{
		DamageCacheAdmission admission(*actor, source);
		EXPECT_TRUE(source.queries.empty());
		EXPECT_TRUE(admission.ordinaryCreatureDamageIsStateful());
		EXPECT_TRUE(admission.ordinaryCreatureDamageIsStateful());
		EXPECT_EQ(source.queries, (std::vector<BattleSide>{BattleSide::ATTACKER}));
	}
	// A new immutable batch must observe changed captured policy on the same callback.
	auto & policy = const_cast<JsonNode &>(attackerSideHero->getPrimaryGrowthRules())
		["additionalSpecialties"]["ordinaryCreatureSpecialties"];
	policy["ridersExploitDamagePercent"].Integer() = 0;
	source.queries.clear();
	DamageCacheAdmission next(*actor, source);
	EXPECT_FALSE(next.ordinaryCreatureDamageIsStateful());
	EXPECT_FALSE(next.ordinaryCreatureDamageIsStateful());
	EXPECT_EQ(source.queries, (std::vector<BattleSide>{BattleSide::ATTACKER, BattleSide::DEFENDER}));
	CountingHeroBattle sibling;
	EXPECT_FALSE(next.matches(*actor, sibling));
}

TEST_F(DamageCacheAdmissionTest, OrdinaryPolicyFailureDoesNotPublishAndRetryPreservesFirstUse)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:tyraxor")));
	const auto actor = child->getForUpdate(incoming->unitId());
	CountingHeroBattle source;
	source.attacker = attackerSideHero;
	// Captured JSON is validated at admission, not on the bounded damage query.
	// Fail the actual visible-Hero query to exercise cache publication/retry.
	source.failNextHero = true;
	DamageCacheAdmission admission(*actor, source);
	EXPECT_TRUE(source.queries.empty());
	EXPECT_THROW(admission.ordinaryCreatureDamageIsStateful(), std::runtime_error);
	EXPECT_EQ(source.queries.size(), 1u);
	EXPECT_TRUE(admission.ordinaryCreatureDamageIsStateful());
	EXPECT_TRUE(admission.ordinaryCreatureDamageIsStateful());
	EXPECT_EQ(source.queries, (std::vector<BattleSide>{BattleSide::ATTACKER, BattleSide::ATTACKER}));
}

TEST_F(DamageCacheAdmissionTest, OrdinaryPolicyAdmissionRetainsPlayerVisibleHeroBoundary)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:orrin")));
	defenderSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:tyraxor")));
	resetChild();
	const auto actor = child->getForUpdate(incoming->unitId());
	ASSERT_TRUE(newHorizonsHeroes::ordinaryCreatureDamageIsStateful(*battle()));
	ASSERT_FALSE(newHorizonsHeroes::ordinaryCreatureDamageIsStateful(*child));
	DamageCacheAdmission visible(*actor, *child), allKnowing(*actor, *battle());
	EXPECT_FALSE(visible.ordinaryCreatureDamageIsStateful());
	EXPECT_TRUE(allKnowing.ordinaryCreatureDamageIsStateful());
	EXPECT_FALSE(visible.matches(*actor, *battle()));
}

TEST_F(DamageCacheAdmissionTest, LuckyCanonicalMainBatchRetainsEveryTargetScoreAndBothRngs)
{
	useRevisedOrders = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	// Native roster changes precede the immutable observation interval.
	child.reset();
	const auto * other = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(90), 30);
	resetChild();
	auto actor = child->getForUpdate(braced->unitId());
	actor->addUnitBonus(std::vector<Bonus>{Bonus(BonusDuration::ONE_BATTLE,
		BonusType::LUCK, BonusSource::OTHER, 1, BonusSourceID())});
	ASSERT_GT(child->battleGetAttackLuck(actor.get(), incoming, false), 0);
	ASSERT_NO_FATAL_FAILURE(compareMainBatch(actor, {incoming, other, incoming, other}));
}

TEST_F(DamageCacheAdmissionTest, ActualMainProbeKeepsBothFullCalculationsDamageStateAndRngs)
{
	useRevisedOrders = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NE(dynamic_cast<const NativeDamageCalculator *>(LIBRARY->scriptTypes()->getDamageCalculator()), nullptr);
	auto actor = child->getForUpdate(braced->unitId());
	auto target = child->getForUpdate(incoming->unitId());
	actor->addUnitBonus(std::vector<Bonus>{Bonus(BonusDuration::ONE_BATTLE,
		BonusType::LUCK, BonusSource::OTHER, 1, BonusSourceID())});
	ASSERT_GT(child->battleGetAttackLuck(actor.get(), target.get(), false), 0);
	DamageCache cache;
	cache.buildDamageCache(child, BattleSide::ATTACKER);
	const auto state = snapshot();
	for(const auto & unit : battle()->stacks)
		(void)unit->counterAttacks.available();
	const auto world = gameState()->saveToMemory();
	const auto defaultRng = projectedHitTest::serialized(CRandomGenerator::getDefault());
	const auto serverRng = projectedHitTest::serialized(*gameHandler->randomizer);
	const auto gameRng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto expected = cache.getDamage(actor.get(), target.get(), child);
	DamageCacheAdmission admission(*actor, *child);
	DamageCacheProbeTiming timing;
	const auto actual = cache.getDamage(actor.get(), target.get(), child, &timing, &admission);
	EXPECT_EQ(actual, expected);
	EXPECT_EQ(timing.dynamicBypasses, 1u);
	EXPECT_EQ(timing.calculation.nativeAttackContext.calls, 2u);
	EXPECT_EQ(timing.calculation.nativeFinalization.calls, 2u);
	EXPECT_EQ(timing.calculation.initialCalls, 1u);
	EXPECT_EQ(timing.calculation.luckySecondCalls, 1u);
	EXPECT_EQ(timing.calculation.unluckySecondCalls, 0u);
	EXPECT_EQ(timing.calculation.nativePayload.calls, 2u);
	EXPECT_EQ(timing.calculation.nativePhysicalModifiers.calls, 2u);
	EXPECT_EQ(timing.calculation.nativeHeroOrders.calls, 2u);
	EXPECT_EQ(timing.calculation.bonusPreparation.calls, 0u);
	EXPECT_EQ(timing.calculation.bonusPreparation.nanoseconds, 0u);
	EXPECT_EQ(timing.calculation.luaCall.calls, 0u);
	EXPECT_EQ(timing.calculation.luaCall.nanoseconds, 0u);
	EXPECT_EQ(snapshot(), state);
	EXPECT_EQ(gameState()->saveToMemory(), world);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), defaultRng);
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), serverRng);
	EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), gameRng);
}
