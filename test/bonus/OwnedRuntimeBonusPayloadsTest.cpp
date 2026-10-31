/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/bonuses/OwnedBonusPayloads.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/bonuses/OwnedBonusEvaluationFacts.h"
#include "../../lib/GameLibrary.h"
namespace
{
struct WithoutLibrary
{
	GameLibrary * old = LIBRARY;
	WithoutLibrary() { LIBRARY = nullptr; }
	~WithoutLibrary() { LIBRARY = old; }
};
std::shared_ptr<Bonus> rawValue(int value)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED,
		BonusSource::OTHER, value, BonusSourceID(SpellID(SpellID::FORTUNE)));
}
struct DividerReader
{
	std::shared_ptr<DivideStackLevelUpdater> divider;
	template<class T> void operator&(T &) {}
	void operator&(std::shared_ptr<DivideStackLevelUpdater> & value) { divider = value; }
};
std::shared_ptr<DivideStackLevelUpdater> divider(const TimesHeroLevelDivideStackLevelUpdater & source)
{
	auto inspect = source; DividerReader reader; inspect.serialize(reader); return reader.divider;
}
}
TEST(OwnedRuntimeBonusPayloadsTest, NoLibraryCopyPreservesOwnerStepAndExactHexMembership)
{
	auto original = rawValue(100);
	original->bonusOwner = PlayerColor(0);
	original->parameters = std::make_shared<BonusParameters>(CreatureID(23));
	auto step = std::make_shared<TimesStackSizeUpdater>(0, 10000, 2, 7); original->updater = step;
	BattleHexArray hexes; hexes.insert(BattleHex(50)); hexes.set(0, BattleHex(51));
	ASSERT_TRUE(hexes.contains(BattleHex(50))); ASSERT_TRUE(hexes.contains(BattleHex(51)));
	original->limiter = std::make_shared<UnitOnHexLimiter>(hexes);
	WithoutLibrary absent;
	auto owner = ownedBonusPayloads::copyOwnedValues({original});
	const auto & copied = owner->roots().at(0);
	EXPECT_NE(copied, original); EXPECT_EQ(copied->bonusOwner, PlayerColor(0));
	EXPECT_EQ(copied->sid, original->sid);
	EXPECT_EQ(copied->parameters->toCreature(), CreatureID(23));
	const auto * positions = dynamic_cast<const UnitOnHexLimiter *>(copied->limiter.get());
	ASSERT_NE(positions, nullptr);
	EXPECT_TRUE(positions->applicableHexes.contains(BattleHex(50)));
	EXPECT_TRUE(positions->applicableHexes.contains(BattleHex(51)));
	OwnedBonusEvaluationFacts facts; facts.nodeType = BonusNodeType::STACK_BATTLE;
	facts.stackCount = 6; facts.stableStackLevel = 1;
	CBonusSystemNode shell(BonusNodeType::STACK_BATTLE, true);
	const BonusEvaluationContext context(shell, facts);
	EXPECT_EQ(copied->updater->createUpdatedBonus(copied, context)->val, 21);
	*step = TimesStackSizeUpdater(0, 10000, 2, 9); original->parameters.reset();
	EXPECT_EQ(copied->updater->createUpdatedBonus(copied, context)->val, 21);
	EXPECT_EQ(copied->parameters->toCreature(), CreatureID(23));
}
TEST(OwnedRuntimeBonusPayloadsTest, PrivateDividerAndNestedModifierAliasesShareOneNewIdentityMap)
{
	auto first = rawValue(12), second = rawValue(3);
	auto times = std::make_shared<TimesHeroLevelDivideStackLevelUpdater>();
	const auto originalDivider = divider(*times);
	auto composite = std::make_shared<CompositeUpdater>(); composite->updaters = {times, originalDivider};
	first->updater = composite; first->propagationUpdater = times; second->updater = originalDivider;
	auto same = std::make_shared<HasAnotherBonusLimiter>(BonusType::LUCK);
	first->limiter = second->limiter = same;
	first->propagator = second->propagator = std::make_shared<CPropagatorNodeType>(BonusNodeType::HERO);
	WithoutLibrary absent;
	auto owner = ownedBonusPayloads::copyOwnedValues({first, second, first});
	const auto & values = owner->roots(); ASSERT_EQ(values.size(), 3u);
	EXPECT_EQ(values[0], values[2]); EXPECT_NE(values[0], first);
	const auto * copiedComposite = dynamic_cast<const CompositeUpdater *>(values[0]->updater.get());
	ASSERT_NE(copiedComposite, nullptr);
	EXPECT_EQ(copiedComposite->updaters[0], values[0]->propagationUpdater);
	EXPECT_EQ(copiedComposite->updaters[1], values[1]->updater);
	const auto * copiedTimes = dynamic_cast<const TimesHeroLevelDivideStackLevelUpdater *>(values[0]->propagationUpdater.get());
	ASSERT_NE(copiedTimes, nullptr);
	EXPECT_EQ(divider(*copiedTimes), values[1]->updater);
	EXPECT_NE(divider(*copiedTimes), originalDivider);
	EXPECT_EQ(values[0]->limiter, values[1]->limiter); EXPECT_NE(values[0]->limiter, same);
	EXPECT_EQ(values[0]->propagator, values[1]->propagator); EXPECT_NE(values[0]->propagator, first->propagator);
}
TEST(OwnedRuntimeBonusPayloadsTest, NonrootBonusCyclesRetireWithoutConstCastOrOriginalMutation)
{
	auto root = rawValue(1), middle = rawValue(2), tail = rawValue(3);
	auto link = [](const std::shared_ptr<Bonus> & from, const std::shared_ptr<Bonus> & to)
	{
		BonusParametersOnCombatEvent events;
		events.effects.push_back(BonusParametersOnCombatEvent::CombatEffectBonus{to, false});
		from->parameters = std::make_shared<BonusParameters>(events);
	};
	link(root, middle); link(middle, tail); link(tail, middle);
	std::weak_ptr<const Bonus> copiedMiddle, copiedTail;
	WithoutLibrary absent;
	{
		auto owner = ownedBonusPayloads::copyOwnedValues({root});
		const auto * first = owner->roots()[0]->parameters->combatEffects();
		const auto child = std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(first->effects[0]).bonus;
		copiedMiddle = child;
		const auto last = std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(child->parameters->combatEffects()->effects[0]).bonus;
		copiedTail = last;
		EXPECT_EQ(std::get<BonusParametersOnCombatEvent::CombatEffectBonus>(last->parameters->combatEffects()->effects[0]).bonus, child);
	}
	EXPECT_TRUE(copiedMiddle.expired()); EXPECT_TRUE(copiedTail.expired());
	EXPECT_NE(middle->parameters, nullptr); EXPECT_NE(tail->parameters, nullptr);
	root->parameters.reset(); middle->parameters.reset(); tail->parameters.reset(); // Original fixture owns its own cycle cleanup.
}
TEST(OwnedRuntimeBonusPayloadsTest, ExistingTypeAndModifierDagAdmissionRemainsRequired)
{
	struct UnknownUpdater final : IUpdater {};
	auto original = rawValue(1);
	original->updater = std::make_shared<UnknownUpdater>();
	WithoutLibrary absent;
	EXPECT_THROW(ownedBonusPayloads::copyOwnedValues({original}), std::invalid_argument);
	auto cycle = std::make_shared<CompositeUpdater>();
	cycle->updaters.push_back(cycle);
	original->updater = cycle;
	EXPECT_THROW(ownedBonusPayloads::copyOwnedValues({original}), std::invalid_argument);
	EXPECT_EQ(cycle->updaters.front(), cycle); // Admission must not mutate its input.
	cycle->updaters.clear();
	auto limiter = std::make_shared<AllOfLimiter>();
	limiter->limiters.push_back(limiter);
	original->updater.reset(); original->limiter = limiter;
	EXPECT_THROW(ownedBonusPayloads::copyOwnedValues({original}), std::invalid_argument);
	EXPECT_EQ(limiter->limiters.front(), limiter);
	limiter->limiters.clear();
}
