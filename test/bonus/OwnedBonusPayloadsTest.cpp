/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/bonuses/OwnedBonusPayloads.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/CBonusSystemNode.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include <stdexcept>
namespace
{
std::shared_ptr<Bonus> value(int n = 2)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED,
		BonusSource::OTHER, n, BonusSourceID());
}
OwnedBonusEvaluationFacts heroFacts(int level)
{
	OwnedBonusEvaluationFacts f;
	f.nodeType = BonusNodeType::HERO;
	f.heroLevel = level;
	f.armySlots.emplace();
	return f;
}
class ForeignUpdater final : public IUpdater {};
class ForeignLimiter final : public ILimiter {};
class ForeignPropagator final : public IPropagator {};
}
TEST(OwnedBonusPayloadsTest, PreservesNullAndRepeatedRootsWithoutRetainingSource)
{
	auto source = value();
	source->bonusOwner = PlayerColor(1);
	std::weak_ptr<Bonus> weak = source;
	auto owner = ownedBonusPayloads::copy({source, nullptr, source});
	const auto & result = owner->roots();
	ASSERT_EQ(result.size(), 3u);
	EXPECT_EQ(result[0], result[2]);
	EXPECT_EQ(result[1], nullptr);
	EXPECT_NE(result[0], source);
	EXPECT_EQ(result[0]->bonusOwner, PlayerColor(1));
	source->val = 90;
	source->bonusOwner = PlayerColor(0);
	source.reset();
	EXPECT_TRUE(weak.expired());
	EXPECT_EQ(result[0]->val, 2);
	EXPECT_EQ(result[0]->bonusOwner, PlayerColor(1));
}
TEST(OwnedBonusPayloadsTest, NestedCombatEffectsRetainCrossRootIdentityAndRuntimeOwner)
{
	auto source = value();
	auto nested = value(7);
	nested->bonusOwner = PlayerColor(1);
	BonusParametersOnCombatEvent effects;
	effects.effects.emplace_back(BonusParametersOnCombatEvent::CombatEffectBonus{nested, false});
	effects.effects.emplace_back(BonusParametersOnCombatEvent::CombatEffectBonus{nested, true});
	auto parameters = std::make_shared<BonusParameters>(effects);
	source->parameters = parameters;
	auto owner = ownedBonusPayloads::copy({source, nested});
	const auto & result = owner->roots();
	const auto * copied = result[0]->parameters->combatEffects();
	ASSERT_NE(copied, nullptr);
	ASSERT_EQ(copied->effects.size(), 2u);
	EXPECT_EQ(std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(copied->effects[0]).bonus, result[1]);
	EXPECT_EQ(std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(copied->effects[1]).bonus, result[1]);
	EXPECT_NE(result[0]->parameters, parameters);
	EXPECT_EQ(result[1]->bonusOwner, PlayerColor(1));
	parameters->toCustom<BonusParametersOnCombatEvent>().effects.clear();
	nested->val = 100;
	EXPECT_EQ(copied->effects.size(), 2u);
	EXPECT_EQ(result[1]->val, 7);
}
TEST(OwnedBonusPayloadsTest, CombatEffectCyclesCloneAndCanRetireWithoutNativeEdges)
{
	auto source = value();
	BonusParametersOnCombatEvent effects;
	effects.effects.emplace_back(BonusParametersOnCombatEvent::CombatEffectBonus{source, false});
	source->parameters = std::make_shared<BonusParameters>(effects);
	auto owner = ownedBonusPayloads::copy({source});
	const auto & result = owner->roots();
	const auto * copied = result[0]->parameters->combatEffects();
	ASSERT_NE(copied, nullptr);
	EXPECT_EQ(std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(copied->effects[0]).bonus, result[0]);
	source->parameters.reset();
	result[0]->parameters.reset(); // Explicitly break both owned shared cycles.
}
TEST(OwnedBonusPayloadsTest, TimesStackSizePrivateStepValueSurvivesNestedSharedAliases)
{
	auto step = std::make_shared<TimesStackSizeUpdater>(0, 100, 2, 7);
	auto composite = std::make_shared<CompositeUpdater>();
	composite->updaters = {step};
	auto first = value();
	auto second = value();
	first->updater = composite;
	second->propagationUpdater = step;
	auto owner = ownedBonusPayloads::copy({first, second});
	const auto & result = owner->roots();
	const auto * copied = dynamic_cast<const CompositeUpdater *>(result[0]->updater.get());
	ASSERT_NE(copied, nullptr);
	ASSERT_EQ(copied->updaters.size(), 1u);
	EXPECT_EQ(copied->updaters[0], result[1]->propagationUpdater);
	EXPECT_NE(copied->updaters[0], step);
	CBonusSystemNode shell(BonusNodeType::STACK_BATTLE, true);
	OwnedBonusEvaluationFacts facts;
	facts.nodeType = BonusNodeType::STACK_BATTLE;
	facts.stackCount = 10;
	facts.stableStackLevel = 3;
	BonusEvaluationContext context(shell, facts);
	*step = TimesStackSizeUpdater(0, 100, 2, 9);
	EXPECT_EQ(result[0]->updater->createUpdatedBonus(result[0], context)->val, 35);
	EXPECT_EQ(step->createUpdatedBonus(first, context)->val, 45);
}
TEST(OwnedBonusPayloadsTest, UnitOnHexPreservesHistoricalVectorAndPresenceFlags)
{
	auto hex = std::make_shared<UnitOnHexLimiter>(BattleHexArray{BattleHex(4)});
	hex->applicableHexes.set(0, BattleHex(5));
	ASSERT_TRUE(hex->applicableHexes.contains(BattleHex(4)));
	ASSERT_TRUE(hex->applicableHexes.contains(BattleHex(5)));
	auto aggregate = std::make_shared<AllOfLimiter>(std::vector<TLimiterPtr>{hex});
	auto first = value();
	auto second = value();
	first->limiter = aggregate;
	second->limiter = hex;
	auto owner = ownedBonusPayloads::copy({first, second});
	const auto & result = owner->roots();
	const auto * copied = dynamic_cast<const UnitOnHexLimiter *>(result[1]->limiter.get());
	const auto * copiedAggregate = dynamic_cast<const AllOfLimiter *>(result[0]->limiter.get());
	ASSERT_NE(copied, nullptr);
	ASSERT_NE(copiedAggregate, nullptr);
	EXPECT_EQ(copiedAggregate->limiters.front(), result[1]->limiter);
	EXPECT_EQ(copied->applicableHexes.size(), hex->applicableHexes.size());
	for(int cell = 0; cell < GameConstants::BFIELD_SIZE; ++cell)
		EXPECT_EQ(copied->applicableHexes.contains(BattleHex(cell)), hex->applicableHexes.contains(BattleHex(cell)));
	hex->applicableHexes.clear();
	EXPECT_TRUE(copied->applicableHexes.contains(BattleHex(4)));
	EXPECT_TRUE(copied->applicableHexes.contains(BattleHex(5)));
}
TEST(OwnedBonusPayloadsTest, NestedNonIdempotentCompositePreservesEveryUpdaterStage)
{
	auto multiply = std::make_shared<TimesHeroLevelUpdater>();
	auto inner = std::make_shared<CompositeUpdater>();
	inner->updaters = {multiply};
	auto outer = std::make_shared<CompositeUpdater>();
	outer->updaters = {inner, multiply};
	auto source = value();
	source->updater = outer;
	auto owner = ownedBonusPayloads::copy({source});
	const auto & result = owner->roots();
	CBonusSystemNode shell(BonusNodeType::HERO, true);
	const auto facts = heroFacts(3);
	BonusEvaluationContext context(shell, facts);
	outer->updaters.clear();
	inner->updaters.clear();
	EXPECT_EQ(result[0]->updater->createUpdatedBonus(result[0], context)->val, 18);
	EXPECT_EQ(result[0]->updater->createUpdatedBonus(result[0], context)->val, 18);
	EXPECT_EQ(result[0]->val, 2);
}
TEST(OwnedBonusPayloadsTest, TypedHeroDividerPrivateNestedStateStillUsesCanonicalStages)
{
	auto source = value(25);
	source->updater = std::make_shared<TimesHeroLevelDivideStackLevelUpdater>();
	auto owner = ownedBonusPayloads::copy({source});
	const auto & result = owner->roots();
	CBonusSystemNode hero(BonusNodeType::HERO, true);
	const auto facts = heroFacts(11);
	BonusEvaluationContext heroContext(hero, facts);
	const auto pending = result[0]->updater->createUpdatedBonus(result[0], heroContext);
	CBonusSystemNode stack(BonusNodeType::STACK_BATTLE, true);
	OwnedBonusEvaluationFacts s;
	s.nodeType = BonusNodeType::STACK_BATTLE;
	s.stackCount = 9;
	s.stableStackLevel = 3;
	BonusEvaluationContext stackContext(stack, s);
	ASSERT_NE(pending->updater, nullptr);
	EXPECT_EQ(pending->updater->createUpdatedBonus(pending, stackContext)->val, 75);
}
TEST(OwnedBonusPayloadsTest, RejectsUnknownPolymorphicInputsBeforeReturningAnyClone)
{
	auto source = value();
	source->updater = std::make_shared<ForeignUpdater>();
	EXPECT_THROW(ownedBonusPayloads::copy({source}), std::invalid_argument);
	source->updater.reset();
	source->limiter = std::make_shared<ForeignLimiter>();
	EXPECT_THROW(ownedBonusPayloads::copy({source}), std::invalid_argument);
	source->limiter.reset();
	source->propagator = std::make_shared<ForeignPropagator>();
	EXPECT_THROW(ownedBonusPayloads::copy({source}), std::invalid_argument);
}

TEST(OwnedBonusPayloadsTest, OriginalPayloadOwnersExpireAfterCapture)
{
	std::weak_ptr<const IUpdater> updater;
	std::weak_ptr<const ILimiter> limiter;
	std::weak_ptr<const IPropagator> propagator;
	std::weak_ptr<const BonusParameters> parameters;
	std::unique_ptr<ownedBonusPayloads::Captured> owner;
	{
		auto source = value();
		source->updater = std::make_shared<TimesStackSizeUpdater>(0, 100, 2, 7);
		source->limiter = std::make_shared<UnitOnHexLimiter>(BattleHexArray{BattleHex(4)});
		source->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::STACK_BATTLE);
		source->parameters = std::make_shared<BonusParameters>(std::vector<int32_t>{1, 2});
		updater = source->updater;
		limiter = source->limiter;
		propagator = source->propagator;
		parameters = source->parameters;
		owner = ownedBonusPayloads::copy({source});
	}
	const auto & result = owner->roots();
	EXPECT_TRUE(updater.expired());
	EXPECT_TRUE(limiter.expired());
	EXPECT_TRUE(propagator.expired());
	EXPECT_TRUE(parameters.expired());
	ASSERT_EQ(result.size(), 1u);
	EXPECT_EQ(result[0]->parameters->toVector(), (std::vector<int32_t>{1, 2}));
}

TEST(OwnedBonusPayloadsTest, OwnerRetiresNonRootParameterCyclesWithoutConstMutation)
{
	auto root = value();
	auto first = value(3);
	auto second = value(4);
	auto connect = [](const std::shared_ptr<Bonus> & from, const std::shared_ptr<Bonus> & to)
	{
		BonusParametersOnCombatEvent effects;
		effects.effects.emplace_back(BonusParametersOnCombatEvent::CombatEffectBonus{to, false});
		from->parameters = std::make_shared<BonusParameters>(effects);
	};
	connect(root, first);
	connect(first, second);
	connect(second, first);
	auto owner = ownedBonusPayloads::copy({root});
	const auto & clonedRoot = owner->roots().front();
	const auto & clonedFirst = std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(
		clonedRoot->parameters->combatEffects()->effects.front()).bonus;
	const auto & clonedSecond = std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(
		clonedFirst->parameters->combatEffects()->effects.front()).bonus;
	std::weak_ptr<const Bonus> weakFirst = clonedFirst;
	std::weak_ptr<const Bonus> weakSecond = clonedSecond;
	EXPECT_EQ(std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(
		clonedSecond->parameters->combatEffects()->effects.front()).bonus, clonedFirst);
	owner.reset(); // No borrowed values are used after retirement.
	EXPECT_TRUE(weakFirst.expired());
	EXPECT_TRUE(weakSecond.expired());
	root->parameters.reset();
	first->parameters.reset();
	second->parameters.reset();
}
TEST(OwnedBonusPayloadsTest, RejectsModifierCyclesBeforeCodecOrCanonicalEvaluation)
{
	auto source = value();
	auto updater = std::make_shared<CompositeUpdater>();
	updater->updaters.push_back(updater);
	source->updater = updater;
	EXPECT_THROW(ownedBonusPayloads::copy({source}), std::invalid_argument);
	updater->updaters.clear();
	source->updater.reset();
	auto limiter = std::make_shared<AllOfLimiter>();
	limiter->limiters.push_back(limiter);
	source->limiter = limiter;
	EXPECT_THROW(ownedBonusPayloads::copy({source}), std::invalid_argument);
	limiter->limiters.clear();
}
