/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/bonuses/BonusDependencyFacts.h"
#include "../../lib/bonuses/CBonusSystemNode.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/bonuses/BonusGraphQuery.h"
#include "../../lib/bonuses/BonusLimitation.h"
#include "../../lib/bonuses/BonusDependencyCapture.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/BonusCache.h"
#include "../../lib/bonuses/OwnedRecipientBonusGraph.h"
#include <array>

namespace
{
using namespace bonusDependencies;
using Decision = ILimiter::EDecision;
std::shared_ptr<Bonus> record(int n = 7)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED,
		BonusSource::OTHER, n, BonusSourceID());
}
OwnedBonusEvaluationFacts facts(BonusNodeType type = BonusNodeType::STACK_BATTLE)
{
	OwnedBonusEvaluationFacts result;
	result.nodeType = type;
	result.owner = PlayerColor(1);
	result.heroLevel = 13;
	result.stackCount = 21;
	result.stableStackLevel = 3;
	result.armySlots.emplace();
	result.creatureContext.emplace();
	result.acceptance.emplace();
	if(type == BonusNodeType::STACK_BATTLE)
	{
		result.creatureContext->creature = OwnedBonusEvaluationFacts::CreatureContext::Creature{
			CreatureID(7), 3, EAlignment::GOOD};
		result.creatureContext->faction = FactionID(2);
	}
	return result;
}
class NativeHeroNode final : public CBonusSystemNode
{
public:
	NativeHeroNode() : CBonusSystemNode(BonusNodeType::HERO) {}
	bool isPrimaryRatingNode() const override { return true; }
};
class ForeignCount final : public TimesStackSizeUpdater {};
class ForeignType final : public CCreatureTypeLimiter {};
class ForeignComposite final : public CompositeUpdater {};
class NativeLinearGraph final : public BonusGraphQuery
{
	std::span<const Stage> stages;
	BonusList localRoot, empty;
	std::vector<TCNodesVector> parentRows;
	TNodesVector noNodes;
	size_t index(const CBonusSystemNode & node) const
	{
		for(size_t i = 0; i < stages.size(); ++i)
			if(&stages[i].context.originalNode() == &node) return i;
		throw std::invalid_argument("Unbound native test node");
	}
public:
	NativeLinearGraph(const std::shared_ptr<Bonus> & raw, std::span<const Stage> stages, int duplicates = 1)
		: stages(stages), parentRows(stages.size())
	{
		for(int i = 0; i < duplicates; ++i) localRoot.push_back(raw);
		for(size_t i = 1; i < stages.size(); ++i)
			parentRows[i].push_back(&stages[i - 1].context.originalNode());
	}
	const BonusList & local(const CBonusSystemNode & node) const override { return index(node) == 0 ? localRoot : empty; }
	const TCNodesVector & parents(const CBonusSystemNode & node) const override { return parentRows.at(index(node)); }
	const TNodesVector & propagatingParents(const CBonusSystemNode &) const override { return noNodes; }
	const TNodesVector & children(const CBonusSystemNode &) const override { return noNodes; }
	BonusEvaluationContext evaluation(const CBonusSystemNode & node) const override { return stages[index(node)].context; }
};
void expectPreparedEqual(const nativeDamage::PreparedUnit & actual, const nativeDamage::PreparedUnit & expected)
{
	const auto & a = actual.unit; const auto & e = expected.unit;
	EXPECT_EQ(a.bonuses, e.bonuses); EXPECT_EQ(a.values, e.values);
	EXPECT_EQ(a.minDamage, e.minDamage); EXPECT_EQ(a.maxDamage, e.maxDamage);
	EXPECT_EQ(a.count, e.count); EXPECT_EQ(a.attack, e.attack); EXPECT_EQ(a.defense, e.defense);
	EXPECT_EQ(a.firstHealth, e.firstHealth); EXPECT_EQ(a.maxHealth, e.maxHealth);
	EXPECT_EQ(a.phantomIntegrity, e.phantomIntegrity); EXPECT_EQ(a.baseAmount, e.baseAmount);
	EXPECT_EQ(a.availableHealth, e.availableHealth);
	EXPECT_EQ(a.shooter, e.shooter); EXPECT_EQ(a.turret, e.turret); EXPECT_EQ(a.creatureKey, e.creatureKey);
	EXPECT_EQ(actual.physicalDefense, expected.physicalDefense);
	EXPECT_EQ(actual.enemyDefenseReduction, expected.enemyDefenseReduction);
	EXPECT_EQ(actual.enemyAttackReduction, expected.enemyAttackReduction);
	EXPECT_EQ(actual.attackReduction, expected.attackReduction);
	EXPECT_EQ(actual.damageBoost, expected.damageBoost); EXPECT_EQ(actual.petrification, expected.petrification);
	EXPECT_EQ(actual.shooting, expected.shooting);
}

Values reference(const std::shared_ptr<Bonus> & raw, std::span<const Stage> stages,
	const BonusEvaluationContext & final)
{
	// Actual shared native collector + native limitation are the differential oracle.
	// No updater loop or direct limiter invocation is reproduced in this helper.
	BonusList collected, accepted;
	NativeLinearGraph graph(raw, stages);
	collectBonusGraph(graph, stages.back().context.originalNode(), collected);
	limitBonuses(collected, accepted, final);
	if(accepted.empty()) return {0, raw->bonusOwner, Decision::DISCARD};
	return {accepted[0]->val, accepted[0]->bonusOwner, Decision::ACCEPT};
}
}
TEST(BonusDependencyFactsTest, CommonHeroTypeLevelFiltersMatchOrderedNativeCollection)
{
	NativeHeroNode hero;
	CBonusSystemNode stack(BonusNodeType::STACK_BATTLE);
	auto hf = facts(BonusNodeType::HERO), sf = facts();
	BonusEvaluationContext hc(hero, hf), sc(stack, sf);
	auto bonus = record();
	bonus->updater = std::make_shared<GrowsWithLevelUpdater>(13, 2);
	auto type = std::make_shared<CCreatureTypeLimiter>();
	type->creatureID = CreatureID(7);
	bonus->limiter = std::make_shared<AllOfLimiter>(std::vector<TLimiterPtr>{type,
		std::make_shared<CreatureLevelLimiter>(2, 4), std::make_shared<FactionLimiter>(FactionID(2))});
	std::array stages{Stage{hc, false, true}, Stage{sc, true, false}};
	const auto recipe = capture(bonus, stages, sc);
	const auto expected = reference(bonus, stages, sc);
	EXPECT_EQ(recipe.evaluate(21).value, expected.value);
	EXPECT_EQ(recipe.evaluate(21).decision, Decision::ACCEPT);
	EXPECT_EQ(expected.decision, Decision::ACCEPT);
	EXPECT_EQ(recipe.evaluate(2).value, expected.value); // Immutable hero specialization.
	EXPECT_FALSE(recipe.canRefresh(Change::OwnerLevel));
	EXPECT_FALSE(recipe.canRefresh(Change::Profile));
	EXPECT_THROW(recipe.evaluate(21, Change::Profile), std::invalid_argument);
}
TEST(BonusDependencyFactsTest, CountChangingCompositeUsesSharedNativeKernelsAtEveryRefresh)
{
	CBonusSystemNode stack(BonusNodeType::STACK_BATTLE);
	auto sf = facts();
	BonusEvaluationContext sc(stack, sf);
	auto composite = std::make_shared<CompositeUpdater>();
	composite->updaters = {std::make_shared<TimesStackSizeUpdater>(2, 17, 3, 5),
		std::make_shared<TimesStackLevelUpdater>()};
	auto bonus = record();
	bonus->updater = composite;
	const std::array stages{Stage{sc, true, false}};
	const auto recipe = capture(bonus, stages, sc);
	EXPECT_TRUE(recipe.canRefresh(Change::Count | Change::Position));
	for(int count : {0, 1, 8, 21, 70})
	{
		sf.stackCount = count;
		const auto expected = reference(bonus, stages, sc);
		EXPECT_EQ(recipe.evaluate(count).value, expected.value);
	}
	EXPECT_NE(recipe.evaluate(8).value, recipe.evaluate(21).value);
}
TEST(BonusDependencyFactsTest, SpecialtyUpdaterReplacementAndClearingSurviveTwoStages)
{
	NativeHeroNode hero;
	CBonusSystemNode stack(BonusNodeType::STACK_BATTLE), leaf(BonusNodeType::STACK_BATTLE);
	auto hf = facts(BonusNodeType::HERO), sf = facts();
	BonusEvaluationContext hc(hero, hf), sc(stack, sf), lc(leaf, sf);
	auto bonus = record(5);
	bonus->updater = std::make_shared<TimesHeroLevelDivideStackLevelUpdater>();
	const std::array stages{Stage{hc, false, true}, Stage{sc, true, false}, Stage{lc, true, false}};
	const auto recipe = capture(bonus, stages, sc);
	EXPECT_EQ(recipe.evaluate(21).value, reference(bonus, stages, sc).value);
	EXPECT_EQ(recipe.evaluate(21).value, 20); // Third stage must not divide a second time.
}
TEST(BonusDependencyFactsTest, InheritedHeroPrimaryRatingSuppressionIsNotFlattenedAway)
{
	NativeHeroNode hero;
	CBonusSystemNode stack(BonusNodeType::STACK_BATTLE);
	auto hf = facts(BonusNodeType::HERO), sf = facts();
	BonusEvaluationContext hc(hero, hf), sc(stack, sf);
	auto bonus = record(5);
	bonus->type = BonusType::PRIMARY_SKILL;
	bonus->subtype = BonusSubtypeID(PrimarySkill::ATTACK);
	bonus->updater = std::make_shared<TimesHeroLevelUpdater>(2);
	const std::array stages{Stage{hc, false, true}, Stage{sc, true, false}};
	EXPECT_EQ(capture(bonus, stages, sc).evaluate(21).decision, Decision::DISCARD);
	EXPECT_EQ(reference(bonus, stages, sc).decision, Decision::DISCARD);
	NativeLinearGraph genericGraph(bonus, stages);
	const auto suppressed = captureNativeModifiers(genericGraph, stack);
	std::array<nativeDamage::PreparedUnit, 2> base;
	const auto absent = suppressed.refresh(21, Change::None, base, 0);
	ASSERT_TRUE(absent);
	EXPECT_EQ(absent->prepared[0].unit.attack, 0);
	EXPECT_FALSE(absent->prepared[0].unit.has(BonusType::PRIMARY_SKILL));
	auto creature = std::make_shared<CCreatureTypeLimiter>();
	creature->creatureID = CreatureID(7);
	bonus->limiter = creature;
	const auto retained = captureNativeModifiers(genericGraph, stack).refresh(21, Change::None, base, 0);
	ASSERT_TRUE(retained);
	EXPECT_EQ(reference(bonus, stages, sc).decision, Decision::ACCEPT);
	EXPECT_EQ(retained->prepared[0].unit.attack, reference(bonus, stages, sc).value);
	EXPECT_GT(retained->prepared[0].unit.attack, 0);
	EXPECT_TRUE(retained->prepared[0].unit.has(BonusType::PRIMARY_SKILL));
}
TEST(BonusDependencyFactsTest, DormantRawRecordIsRetainedAndProfileChangeRequiresRebind)
{
	CBonusSystemNode stack(BonusNodeType::STACK_BATTLE);
	auto sf = facts();
	BonusEvaluationContext sc(stack, sf);
	auto bonus = record();
	bonus->limiter = std::make_shared<CreatureLevelLimiter>(4, 9);
	const auto dormant = capture(bonus, {}, sc);
	EXPECT_EQ(dormant.evaluate(21).decision, Decision::DISCARD);
	EXPECT_EQ(dormant.rawRecord()->type, BonusType::STACKS_SPEED);
	EXPECT_THROW(dormant.evaluate(21, Change::Profile), std::invalid_argument);
	sf.creatureContext->creature->level = 5;
	const auto rebound = capture(bonus, {}, sc);
	EXPECT_EQ(rebound.evaluate(21).decision, Decision::ACCEPT);
}
TEST(BonusDependencyFactsTest, OwnerNativeKernelAndDependencyInvalidationAgree)
{
	CBonusSystemNode stack(BonusNodeType::STACK_BATTLE);
	auto sf = facts();
	sf.owner = PlayerColor::UNFLAGGABLE;
	BonusEvaluationContext sc(stack, sf);
	auto bonus = record();
	bonus->updater = std::make_shared<OwnerUpdater>();
	bonus->limiter = std::make_shared<OppositeSideLimiter>();
	const std::array stages{Stage{sc, true, false}};
	const auto recipe = capture(bonus, stages, sc);
	NativeLinearGraph graph(bonus, stages);
	BonusList nativeOutput, accepted;
	collectBonusGraph(graph, stack, nativeOutput);
	limitBonuses(nativeOutput, accepted, sc);
	ASSERT_EQ(nativeOutput.size(), 1u);
	EXPECT_EQ(recipe.evaluate(21).owner, nativeOutput[0]->bonusOwner);
	EXPECT_EQ(recipe.evaluate(21).decision, accepted.empty() ? Decision::DISCARD : Decision::ACCEPT);
	EXPECT_EQ(nativeOutput[0]->bonusOwner, PlayerColor::NEUTRAL);
	EXPECT_FALSE(recipe.canRefresh(Change::Controller));
	EXPECT_FALSE(recipe.canRefresh(Change::Round));
	EXPECT_FALSE(recipe.canRefresh(Change::ActiveEffects));
	EXPECT_FALSE(recipe.canRefresh(Change::Roster));
}
TEST(BonusDependencyFactsTest, ExactTypeAdmissionRejectsSubclassesEvenWhenBaseLooksSupported)
{
	auto bonus = record();
	for(const TUpdaterPtr & updater : std::vector<TUpdaterPtr>{std::make_shared<ForeignCount>(),
		std::make_shared<ForeignComposite>()})
	{
		bonus->updater = updater;
		EXPECT_FALSE(classify(*bonus).supported);
	}
	bonus->updater.reset();
	bonus->limiter = std::make_shared<ForeignType>();
	EXPECT_FALSE(classify(*bonus).supported);
	// Presence is still conservative for this raw type despite unknown executable rules.
	EXPECT_EQ(bonus->type, BonusType::STACKS_SPEED);
}
TEST(BonusDependencyFactsTest, AliasDagIsAcceptedAndCyclesAreRejectedBeforeNativeRecursion)
{
	auto leaf = std::make_shared<CreatureLevelLimiter>(1, 5);
	auto aggregate = std::make_shared<AllOfLimiter>(std::vector<TLimiterPtr>{leaf, leaf});
	auto bonus = record();
	bonus->limiter = aggregate;
	EXPECT_TRUE(classify(*bonus).supported);
	CBonusSystemNode stack(BonusNodeType::STACK_BATTLE);
	auto sf = facts();
	const auto recipe = capture(bonus, {}, BonusEvaluationContext(stack, sf));
	const auto & cloned = static_cast<const AllOfLimiter &>(*recipe.rawRecord()->limiter);
	EXPECT_EQ(cloned.limiters[0], cloned.limiters[1]);
	EXPECT_NE(cloned.limiters[0], leaf);
	aggregate->limiters.push_back(aggregate);
	EXPECT_FALSE(classify(*bonus).supported);
	EXPECT_THROW(capture(bonus, {}, BonusEvaluationContext(stack, sf)), std::invalid_argument);
	aggregate->limiters.pop_back();
	auto composite = std::make_shared<CompositeUpdater>();
	composite->updaters.push_back(composite);
	bonus->updater = composite;
	EXPECT_FALSE(classify(*bonus).supported);
	composite->updaters.clear();
}
TEST(BonusDependencyFactsTest, RecipesOwnRulesAndRemainIndependentAfterSourceContextDies)
{
	const auto make = [](int minimum)
	{
		CBonusSystemNode stack(BonusNodeType::STACK_BATTLE);
		auto sf = facts();
		BonusEvaluationContext sc(stack, sf);
		auto bonus = record();
		bonus->updater = std::make_shared<TimesStackSizeUpdater>(minimum, 100, 2, 3);
		const std::array stages{Stage{sc, true, false}};
		return capture(bonus, stages, sc);
	};
	const auto a = make(1), b = make(9);
	EXPECT_EQ(a.evaluate(4).value, 6);
	EXPECT_EQ(b.evaluate(4).value, 27);
	EXPECT_EQ(a.evaluate(10).value, 15);
	EXPECT_EQ(b.evaluate(4).value, 27);
}
TEST(BonusDependencyFactsTest, SpatialRosterAndOpaqueRulesRemainExplicitFallbacks)
{
	auto bonus = record();
	bonus->limiter = std::make_shared<UnitOnHexLimiter>();
	EXPECT_FALSE(classify(*bonus).supported);
	EXPECT_TRUE(intersects(classify(*bonus).dependencies, Change::Position));
	bonus->limiter = std::make_shared<UnitAdjacentLimiter>();
	EXPECT_FALSE(classify(*bonus).supported);
	EXPECT_TRUE(intersects(classify(*bonus).dependencies, Change::Roster));
	bonus->limiter.reset();
	bonus->updater = std::make_shared<TimesArmySizeUpdater>();
	EXPECT_FALSE(classify(*bonus).supported);
	EXPECT_TRUE(intersects(classify(*bonus).dependencies, Change::Count | Change::Roster));
}

TEST(BonusDependencyFactsTest, BatchRecipesPreserveCrossRootIdentityWithoutSourceOwnership)
{
	CBonusSystemNode stack(BonusNodeType::STACK_BATTLE);
	auto sf = facts();
	BonusEvaluationContext sc(stack, sf);
	auto shared = std::make_shared<CreatureLevelLimiter>(1, 9);
	auto a = record(2), b = record(3);
	a->limiter = b->limiter = shared;
	const std::shared_ptr<const ownedBonusPayloads::Captured> owner = ownedBonusPayloads::copyOwnedValues({a, b, a});
	const auto x = capture(owner, 0, {}, sc), y = capture(owner, 1, {}, sc), z = capture(owner, 2, {}, sc);
	EXPECT_EQ(x.rawRecord()->limiter, y.rawRecord()->limiter);
	EXPECT_EQ(x.rawRecord(), z.rawRecord());
	EXPECT_NE(x.rawRecord(), a);
	EXPECT_NE(x.rawRecord()->limiter, shared);
}
TEST(BonusDependencyFactsTest, IncompleteUpdaterTraceAndOversizedRuleDagFailClosed)
{
	CBonusSystemNode stack(BonusNodeType::STACK_BATTLE);
	auto sf = facts();
	BonusEvaluationContext sc(stack, sf);
	auto bonus = record();
	bonus->updater = std::make_shared<TimesStackSizeUpdater>();
	EXPECT_THROW(capture(bonus, {}, sc), std::invalid_argument);
	bonus->updater.reset();
	auto aggregate = std::make_shared<AllOfLimiter>();
	auto leaf = std::make_shared<ILimiter>();
	aggregate->limiters.assign(4097, leaf);
	bonus->limiter = aggregate;
	EXPECT_FALSE(classify(*bonus).supported);
}

TEST(BonusDependencyFactsTest, ActualNativeLimitationFiltersOrdinarySylvanSummonWithoutWildChance)
{
	CBonusSystemNode stack(BonusNodeType::STACK_BATTLE);
	auto sf = facts();
	const SecondarySkill sylvan(SecondarySkill::decode("new-horizons:sylvanLuck"));
	ASSERT_TRUE(sylvan.hasValue());
	sf.acceptance->metadata.sylvanSkill = sylvan;
	sf.acceptance->metadata.heroPresent = true;
	sf.acceptance->metadata.slot = SlotID(0);
	sf.acceptance->state.summoned = true;
	sf.acceptance->state.natureSummoned = false;
	BonusEvaluationContext sc(stack, sf);
	auto bonus = record();
	bonus->source = BonusSource::SECONDARY_SKILL;
	bonus->sid = BonusSourceID(sylvan);
	const std::array stages{Stage{sc, true, false}};
	const auto recipe = capture(bonus, stages, sc);
	EXPECT_EQ(reference(bonus, stages, sc).decision, Decision::DISCARD);
	EXPECT_EQ(recipe.evaluate(21).decision, Decision::DISCARD);
	EXPECT_FALSE(recipe.canRefresh(Change::Profile));
	EXPECT_FALSE(recipe.canRefresh(Change::Controller));
	sf.acceptance->state.natureSummoned = true;
	sf.acceptance->metadata.wildChance = true;
	EXPECT_EQ(reference(bonus, stages, sc).decision, Decision::ACCEPT);
	EXPECT_EQ(capture(bonus, stages, sc).evaluate(21).decision, Decision::ACCEPT);
}
TEST(BonusDependencyFactsTest, ActualNativeLimitationSuppressesOriginalCreatureAfterFormChange)
{
	CBonusSystemNode stack(BonusNodeType::STACK_BATTLE);
	auto sf = facts();
	sf.acceptance->state.hasForm = true;
	sf.acceptance->state.currentForm = CreatureID(7);
	sf.acceptance->state.originalForm = CreatureID(1);
	BonusEvaluationContext sc(stack, sf);
	auto bonus = record();
	bonus->source = BonusSource::CREATURE_ABILITY;
	bonus->sid = BonusSourceID(CreatureID(1));
	const std::array stages{Stage{sc, true, false}};
	EXPECT_EQ(reference(bonus, stages, sc).decision, Decision::DISCARD);
	EXPECT_EQ(capture(bonus, stages, sc).evaluate(21).decision, Decision::DISCARD);
	bonus->sid = BonusSourceID(CreatureID(7));
	EXPECT_EQ(reference(bonus, stages, sc).decision, Decision::ACCEPT);
	EXPECT_EQ(capture(bonus, stages, sc).evaluate(21).decision, Decision::ACCEPT);
}
TEST(BonusDependencyFactsTest, ActualCollectorBindingRefreshesHeroCreatureAttackAcrossCasualtyAndResurrection)
{
	NativeHeroNode hero;
	CBonusSystemNode stack(BonusNodeType::STACK_BATTLE);
	auto hf = facts(BonusNodeType::HERO), sf = facts();
	BonusEvaluationContext hc(hero, hf), sc(stack, sf);
	auto bonus = record();
	bonus->type = BonusType::PRIMARY_SKILL;
	bonus->subtype = BonusSubtypeID(PrimarySkill::ATTACK);
	auto type = std::make_shared<CCreatureTypeLimiter>();
	type->creatureID = CreatureID(7);
	bonus->limiter = type;
	bonus->updater = std::make_shared<TimesStackSizeUpdater>(0, 100, 3, 5);
	const std::array stages{Stage{hc, false, true}, Stage{sc, true, false}};
	NativeLinearGraph graph(bonus, stages);
	const auto captured = captureNativeModifiers(graph, stack);
	std::array<nativeDamage::PreparedUnit, 2> base;
	for(auto & input : base) { input.unit.maxHealth = 20; input.unit.firstHealth = 20; }
	std::optional<NumericFrame> previous;
	for(int count : {21, 8, 0, 15})
	{
		sf.stackCount = count; // Reference native context only; captured recipe owns no pointer here.
		for(auto & input : base) input.unit.count = count;
		const auto next = captured.refresh(count, Change::Count, base, 0, previous ? &*previous : nullptr);
		ASSERT_TRUE(next);
		const auto expected = reference(bonus, stages, sc);
		EXPECT_EQ(next->prepared[0].unit.attack, expected.value);
		EXPECT_EQ(next->prepared[1].unit.attack, expected.value);
		EXPECT_EQ(next->prepared[0].unit.count, count);
		EXPECT_EQ(next->prepared[0].unit.maxHealth, 20);
		previous = next;
	}
	EXPECT_FALSE(captured.refresh(15, Change::Controller, base, 0, &*previous));
	EXPECT_FALSE(captured.refresh(15, Change::Profile, base, 0, &*previous));
	EXPECT_FALSE(captured.refresh(15, Change::Roster, base, 0, &*previous));
}
TEST(BonusDependencyFactsTest, NativeOutputAliasesPreserveUnmodifiedDuplicatesAndFreshUpdaterCopies)
{
	CBonusSystemNode stack(BonusNodeType::STACK_BATTLE);
	auto sf = facts();
	BonusEvaluationContext sc(stack, sf);
	const std::array stages{Stage{sc, true, false}};
	auto bonus = record(4);
	bonus->type = BonusType::CREATURE_DAMAGE;
	bonus->subtype = BonusCustomSubtype::creatureDamageBoth;
	NativeLinearGraph sharedGraph(bonus, stages, 2);
	std::array<nativeDamage::PreparedUnit, 2> base;
	const auto shared = captureNativeModifiers(sharedGraph, stack);
	ASSERT_EQ(shared.occurrences().size(), 2u);
	EXPECT_EQ(shared.occurrences()[0].outputAlias, shared.occurrences()[1].outputAlias);
	const auto initial = shared.refresh(21, Change::Count, base, 0);
	ASSERT_TRUE(initial);
	EXPECT_EQ(initial->prepared[0].unit.minDamage, 4);
	bonus->updater = std::make_shared<TimesStackSizeUpdater>(0, 100, 3, 1);
	const auto updated = captureNativeModifiers(sharedGraph, stack);
	EXPECT_NE(updated.occurrences()[0].outputAlias, updated.occurrences()[1].outputAlias);
	const auto counted = updated.refresh(21, Change::Count, base, 0);
	ASSERT_TRUE(counted);
	EXPECT_EQ(counted->prepared[0].unit.minDamage, 14);
	EXPECT_EQ(counted->prepared[1].unit.maxDamage, 14);
}
TEST(BonusDependencyFactsTest, MaterializedImmutablePropagationIsOwnedButUnresolvedAndCountSourcesFailClosed)
{
	CBonusSystemNode stack(BonusNodeType::STACK_BATTLE);
	auto sf = facts();
	BonusEvaluationContext sc(stack, sf);
	const std::array stages{Stage{sc, true, false}};
	auto bonus = record();
	bonus->bonusOwner = PlayerColor(1); // Actual local materialization result, not a source template.
	bonus->propagationUpdater = std::make_shared<OwnerUpdater>();
	EXPECT_FALSE(classify(*bonus).supported);
	EXPECT_TRUE(classifyMaterializedLocal(*bonus).supported);
	NativeLinearGraph graph(bonus, stages);
	const auto captured = captureNativeModifiers(graph, stack);
	EXPECT_TRUE(captured.canRefresh(Change::Count));
	EXPECT_FALSE(captured.canRefresh(Change::Controller));
	bonus->propagationUpdater = std::make_shared<TimesStackSizeUpdater>();
	EXPECT_THROW(captureNativeModifiers(graph, stack), std::invalid_argument);
	bonus->propagationUpdater.reset();
	bonus->type = BonusType::STACK_HEALTH;
	bonus->updater = std::make_shared<TimesStackSizeUpdater>();
	EXPECT_THROW(captureNativeModifiers(graph, stack), std::invalid_argument);
	bonus->updater.reset();
	bonus->parameters = std::make_shared<BonusParameters>(BonusParametersOnCombatEvent{});
	EXPECT_FALSE(classify(*bonus).supported);
}

TEST(BonusDependencyFactsTest, NativeReducerBinderMatchesCanonicalStatRangeAndSourceSelectors)
{
	BonusList effective;
	const auto add = [&](BonusType type, int value, BonusSubtypeID subtype,
		BonusLimitEffect range = BonusLimitEffect::NO_LIMIT, BonusSource source = BonusSource::OTHER)
	{
		auto bonus = record(value);
		bonus->type = type; bonus->subtype = subtype;
		bonus->effectRange = range; bonus->source = source;
		effective.push_back(bonus);
	};
	add(BonusType::CREATURE_DAMAGE, 5, BonusCustomSubtype::creatureDamageBoth);
	add(BonusType::CREATURE_DAMAGE, 3, BonusCustomSubtype::creatureDamageMin);
	add(BonusType::CREATURE_DAMAGE, 8, BonusCustomSubtype::creatureDamageMax);
	add(BonusType::PRIMARY_SKILL, 12, BonusSubtypeID(PrimarySkill::ATTACK));
	add(BonusType::PRIMARY_SKILL, 9, BonusSubtypeID(PrimarySkill::DEFENSE), BonusLimitEffect::ONLY_MELEE_FIGHT);
	add(BonusType::PRIMARY_SKILL, 3, BonusSubtypeID(PrimarySkill::DEFENSE), BonusLimitEffect::ONLY_DISTANCE_FIGHT);
	add(BonusType::ENEMY_DEFENCE_REDUCTION, 10, BonusSubtypeID());
	add(BonusType::ENEMY_DEFENCE_REDUCTION, 3, BonusSubtypeID(), BonusLimitEffect::ONLY_MELEE_FIGHT);
	add(BonusType::PERCENTAGE_DAMAGE_BOOST, 5, BonusCustomSubtype::damageTypeMelee);
	add(BonusType::PERCENTAGE_DAMAGE_BOOST, 8, BonusCustomSubtype::damageTypeRanged);
	add(BonusType::GENERAL_DAMAGE_REDUCTION, 20, BonusCustomSubtype::damageTypeAll,
		BonusLimitEffect::NO_LIMIT, BonusSource::SPELL_EFFECT);
	add(BonusType::GENERAL_DAMAGE_REDUCTION, 9, BonusCustomSubtype::damageTypeAll);
	add(BonusType::ALWAYS_MAXIMUM_DAMAGE, 0, BonusSubtypeID());
	effective.stackBonuses();
	const auto melee = nativeDamage::prepareOwnedBonuses(effective, {}, 4, false);
	const auto ranged = nativeDamage::prepareOwnedBonuses(effective, {}, 4, true);
	ASSERT_TRUE(melee); ASSERT_TRUE(ranged);
	EXPECT_EQ(melee->unit.minDamage, 8); EXPECT_EQ(melee->unit.maxDamage, 13);
	EXPECT_EQ(melee->unit.attack, 16); EXPECT_EQ(ranged->unit.attack, 16);
	EXPECT_EQ(melee->unit.defense, 13); EXPECT_EQ(ranged->unit.defense, 7);
	EXPECT_EQ(melee->enemyDefenseReduction, 13); EXPECT_EQ(ranged->enemyDefenseReduction, 10);
	EXPECT_EQ(melee->damageBoost, 5); EXPECT_EQ(ranged->damageBoost, 8);
	EXPECT_EQ(melee->petrification, 20); EXPECT_EQ(ranged->petrification, 20);
	EXPECT_TRUE(melee->unit.has(BonusType::ALWAYS_MAXIMUM_DAMAGE));
	const auto & canonical = UnitBonusValuesProxy::canonicalSelectors();
	EXPECT_EQ(melee->unit.minDamage, effective.valOfBonuses(canonical[UnitBonusValuesProxy::MIN_DAMAGE_MELEE]));
	EXPECT_EQ(ranged->unit.maxDamage, effective.valOfBonuses(canonical[UnitBonusValuesProxy::MAX_DAMAGE_RANGED]));
}
TEST(BonusDependencyFactsTest, ForeignNumericFrameAndUnboundCountConsumersAreRejected)
{
	CBonusSystemNode stack(BonusNodeType::STACK_BATTLE);
	auto sf = facts();
	BonusEvaluationContext sc(stack, sf);
	const std::array stages{Stage{sc, true, false}};
	auto bonus = record();
	NativeLinearGraph graph(bonus, stages);
	const auto a = captureNativeModifiers(graph, stack), b = captureNativeModifiers(graph, stack);
	std::array<nativeDamage::PreparedUnit, 2> base;
	const auto first = a.refresh(21, Change::Count, base, 0);
	ASSERT_TRUE(first);
	EXPECT_FALSE(b.refresh(21, Change::Count, base, 0, &*first));
	EXPECT_FALSE(a.refresh(21, Change::Count, base, 5, &*first));
	bonus->updater = std::make_shared<TimesStackSizeUpdater>();
	EXPECT_THROW(captureNativeModifiers(graph, stack), std::invalid_argument); // Speed is not a damage-only Count field.
}

TEST(BonusDependencyFactsTest, HealthOnlyBasesPreserveFullNativePreparationAcrossCleanAndPartialDirtyCounts)
{
	using Input = OwnedRecipientBonusGraph::NodeInput;
	const auto add = [](BonusType type, int amount, BonusSubtypeID subtype = BonusSubtypeID())
	{
		auto b = record(amount); b->type = type; b->subtype = subtype; return b;
	};
	Input source;
	source.facts = facts();
	auto damage = add(BonusType::CREATURE_DAMAGE, 4, BonusCustomSubtype::creatureDamageBoth);
	auto defense = add(BonusType::PRIMARY_SKILL, 9, BonusSubtypeID(PrimarySkill::DEFENSE));
	auto protection = add(BonusType::PHYSICAL_DEFENSE, 7);
	auto enemyDefense = add(BonusType::ENEMY_DEFENCE_REDUCTION, 11);
	auto enemyAttack = add(BonusType::ENEMY_ATTACK_REDUCTION, 12);
	auto attackReduction = add(BonusType::GENERAL_ATTACK_REDUCTION, 13);
	auto boost = add(BonusType::PERCENTAGE_DAMAGE_BOOST, 14, BonusCustomSubtype::damageTypeMelee);
	auto rangedBoost = add(BonusType::PERCENTAGE_DAMAGE_BOOST, 15, BonusCustomSubtype::damageTypeRanged);
	auto petrification = add(BonusType::GENERAL_DAMAGE_REDUCTION, 16, BonusCustomSubtype::damageTypeAll);
	petrification->source = BonusSource::SPELL_EFFECT;
	auto flag = add(BonusType::ALWAYS_MAXIMUM_DAMAGE, 1);
	source.local = {damage, defense, protection, enemyDefense, enemyAttack, attackReduction,
		boost, rangedBoost, petrification, flag};
	for(bool dynamicAttack : {false, true})
	{
		auto input = source;
		if(dynamicAttack)
		{
			auto attack = add(BonusType::PRIMARY_SKILL, 0, BonusSubtypeID(PrimarySkill::ATTACK));
			attack->updater = std::make_shared<TimesStackSizeUpdater>(0, 100, 2, 3);
			input.local.push_back(attack);
		}
		auto graph = OwnedRecipientBonusGraph::fromOwnedInputs({input}, 0);
		const auto captured = captureNativeModifiers(*graph, graph->root());
		std::optional<NumericFrame> previous;
		for(int count : {21, 8, 0, 15})
		{
			input.facts.stackCount = count;
			auto referenceGraph = OwnedRecipientBonusGraph::fromOwnedInputs({input}, 0);
			const auto effective = referenceGraph->query(Selector::all);
			std::array<nativeDamage::PreparedUnit, 2> healthOnly;
			for(size_t shot = 0; shot < 2; ++shot)
			{
				auto & u = healthOnly[shot].unit;
				u.count = count; u.firstHealth = count ? 17 : 0; u.maxHealth = 20;
				u.availableHealth = count ? 17 + (count - 1) * 20 : 0;
				u.baseAmount = 21; u.phantomIntegrity = 31; u.shooter = true;
				u.turret = false; u.creatureKey = "owned-native-fixture";
			}
			const auto refreshed = captured.refresh(count, Change::Count, healthOnly, 4, previous ? &*previous : nullptr);
			ASSERT_TRUE(refreshed);
			for(size_t shot = 0; shot < 2; ++shot)
			{
				const auto native = nativeDamage::prepareOwnedBonuses(effective, healthOnly[shot], 4, shot != 0);
				ASSERT_TRUE(native);
				expectPreparedEqual(refreshed->prepared[shot], *native);
				EXPECT_EQ(native->unit.minDamage, 4); EXPECT_EQ(native->unit.maxDamage, 4);
				EXPECT_EQ(native->unit.defense, 13); EXPECT_EQ(native->physicalDefense, 7);
				EXPECT_TRUE(native->unit.has(BonusType::ALWAYS_MAXIMUM_DAMAGE));
			}
			previous = refreshed;
		}
	}
}

TEST(BonusDependencyFactsTest, ReboundEffectRecipeAndFrameRejectOldRootAfterExpiry)
{
	OwnedRecipientBonusGraph::NodeInput input;
	input.facts = facts();
	auto damage = record(4); damage->type = BonusType::CREATURE_DAMAGE;
	damage->subtype = BonusCustomSubtype::creatureDamageBoth;
	auto temporaryAttack = record(9); temporaryAttack->type = BonusType::PRIMARY_SKILL;
	temporaryAttack->subtype = BonusSubtypeID(PrimarySkill::ATTACK);
	input.local = {damage, temporaryAttack};
	auto beforeGraph = OwnedRecipientBonusGraph::fromOwnedInputs({input}, 0);
	const auto before = captureNativeModifiers(*beforeGraph, beforeGraph->root());
	std::array<nativeDamage::PreparedUnit, 2> healthOnly;
	const auto old = before.refresh(21, Change::None, healthOnly, 0);
	ASSERT_TRUE(old); EXPECT_EQ(old->prepared[0].unit.attack, 9);
	input.local = {damage}; // Authorized raw expiry; raw presence masks stay conservative elsewhere.
	auto afterGraph = OwnedRecipientBonusGraph::fromOwnedInputs({input}, 0);
	const auto after = captureNativeModifiers(*afterGraph, afterGraph->root());
	EXPECT_FALSE(after.refresh(21, Change::None, healthOnly, 0, &*old));
	const auto rebound = after.refresh(21, Change::None, healthOnly, 0);
	ASSERT_TRUE(rebound); EXPECT_EQ(rebound->prepared[0].unit.attack, 0);
	EXPECT_FALSE(before.refresh(8, Change::Count, healthOnly, 0, &*rebound));
	const auto casualty = after.refresh(8, Change::Count, healthOnly, 0, &*rebound);
	ASSERT_TRUE(casualty);
	EXPECT_EQ(casualty->prepared[0].unit.attack, 0);
	EXPECT_EQ(casualty->prepared[0].unit.minDamage, 4);
}
