/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/bonuses/BonusEvaluationContext.h"
#include "../../lib/bonuses/CBonusSystemNode.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/army/CStackInstance.h"
#include "../../lib/GameLibrary.h"
#include <stdexcept>
namespace
{
OwnedBonusEvaluationFacts heroFacts(int level = 11)
{
	OwnedBonusEvaluationFacts facts;
	facts.nodeType = BonusNodeType::HERO;
	facts.owner = PlayerColor(1);
	facts.heroLevel = level;
	facts.armySlots.emplace();
	return facts;
}
std::shared_ptr<Bonus> input(int value = 25)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::STACKS_SPEED,
		BonusSource::OTHER, value, BonusSourceID());
}
struct WithoutLibrary
{
	GameLibrary * original = LIBRARY;
	WithoutLibrary() { LIBRARY = nullptr; }
	~WithoutLibrary() { LIBRARY = original; }
};
}
TEST(OwnedBonusEvaluationFactsTest, RejectsMissingApplicableFactsAndMismatchedShell)
{
	for(const auto type : {BonusNodeType::HERO, BonusNodeType::ARMY, BonusNodeType::TOWN,
		BonusNodeType::STACK_BATTLE, BonusNodeType::STACK_INSTANCE, BonusNodeType::COMMANDER})
	{
		OwnedBonusEvaluationFacts missing;
		missing.nodeType = type;
		CBonusSystemNode shell(type, true);
		EXPECT_THROW(BonusEvaluationContext(shell, missing), std::invalid_argument);
	}
	CBonusSystemNode wrong(BonusNodeType::UNKNOWN, true);
	const auto facts = heroFacts();
	EXPECT_THROW(BonusEvaluationContext(wrong, facts), std::invalid_argument);
}
TEST(OwnedBonusEvaluationFactsTest, InapplicableFactsStayAbsentRatherThanZeroOrForgedApplicability)
{
	CBonusSystemNode shell(BonusNodeType::UNKNOWN, true);
	OwnedBonusEvaluationFacts facts;
	facts.heroLevel = 9;
	facts.stackCount = 8;
	facts.stableStackLevel = 7;
	BonusEvaluationContext context(shell, facts);
	EXPECT_FALSE(context.heroLevel());
	EXPECT_FALSE(context.stackCount());
	EXPECT_FALSE(context.stackLevel());
	EXPECT_FALSE(context.filteredArmySize(CreatureID(), FactionID(), -1));
}
TEST(OwnedBonusEvaluationFactsTest, OwnedStaticCountAndStableLevelUseNoLibraryOrNativeUnit)
{
	CBonusSystemNode shell(BonusNodeType::STACK_BATTLE, true);
	OwnedBonusEvaluationFacts facts;
	facts.nodeType = BonusNodeType::STACK_BATTLE;
	facts.owner = PlayerColor(0);
	facts.stackCount = 7;
	facts.stableStackLevel = 3; // Captured base level, not effective transformed-family level.
	BonusEvaluationContext context(shell, facts);
	WithoutLibrary absent;
	EXPECT_EQ(context.stackCount(), 7);
	EXPECT_EQ(context.stackLevel(), 3);
	EXPECT_EQ(context.getOwner(), PlayerColor(0));
}
TEST(OwnedBonusEvaluationFactsTest, HeroGrowthAndMultiplicationMatchLiveCanonicalContext)
{
	CGHeroInstance source(nullptr);
	source.level = 11;
	BonusEvaluationContext live(source);
	CBonusSystemNode shell(BonusNodeType::HERO, true);
	const auto facts = heroFacts();
	BonusEvaluationContext owned(shell, facts);
	const auto original = input();
	GrowsWithLevelUpdater growth(13, 2);
	TimesHeroLevelUpdater multiply(2);
	EXPECT_EQ(growth.createUpdatedBonus(original, owned)->val, growth.createUpdatedBonus(original, live)->val);
	EXPECT_EQ(multiply.createUpdatedBonus(original, owned)->val, multiply.createUpdatedBonus(original, live)->val);
	EXPECT_EQ(original->val, 25);
}
TEST(OwnedBonusEvaluationFactsTest, ArmyFilteringPreservesCreatureFactionLevelOrderAndClamp)
{
	CBonusSystemNode shell(BonusNodeType::ARMY, true);
	OwnedBonusEvaluationFacts facts;
	facts.nodeType = BonusNodeType::ARMY;
	facts.armySlots = std::vector<OwnedBonusEvaluationFacts::ArmySlot>{
		{CreatureID(1), FactionID(2), 3, 5}, {CreatureID(2), FactionID(2), 3, 80},
		{CreatureID(1), FactionID(3), 3, 90}, {CreatureID(1), FactionID(2), 4, 70},
		{CreatureID(1), FactionID(2), 3, 3}};
	BonusEvaluationContext context(shell, facts);
	TimesArmySizeUpdater updater;
	updater.filteredCreature = CreatureID(1);
	updater.filteredFaction = FactionID(2);
	updater.filteredLevel = 3;
	updater.stepSize = 3;
	updater.minimum = 0;
	updater.maximum = 4;
	WithoutLibrary absent;
	EXPECT_EQ(updater.createUpdatedBonus(input(5), context)->val, 10);
	EXPECT_EQ(facts.armySlots->front().count, 5);
	EXPECT_EQ(facts.armySlots->back().count, 3);
}
TEST(OwnedBonusEvaluationFactsTest, CompositeUsesOneOwnedContextAndDoesNotMutateInput)
{
	CBonusSystemNode shell(BonusNodeType::HERO, true);
	const auto facts = heroFacts(3);
	BonusEvaluationContext context(shell, facts);
	CompositeUpdater composite;
	composite.updaters = {std::make_shared<TimesHeroLevelUpdater>(), std::make_shared<OwnerUpdater>()};
	const auto original = input(2);
	WithoutLibrary absent;
	const auto result = composite.createUpdatedBonus(original, context);
	EXPECT_EQ(result->val, 6);
	EXPECT_EQ(result->bonusOwner, PlayerColor(1));
	EXPECT_EQ(original->val, 2);
	EXPECT_EQ(original->bonusOwner, PlayerColor::CANNOT_DETERMINE);
}
TEST(OwnedBonusEvaluationFactsTest, HeroThenStableBaseDivisionPreservesNativeFloorAndConsumption)
{
	CBonusSystemNode hero(BonusNodeType::HERO, true);
	const auto h = heroFacts(11);
	BonusEvaluationContext heroContext(hero, h);
	CBonusSystemNode stack(BonusNodeType::STACK_BATTLE, true);
	OwnedBonusEvaluationFacts s;
	s.nodeType = BonusNodeType::STACK_BATTLE;
	s.stackCount = 10;
	s.stableStackLevel = 3;
	BonusEvaluationContext stackContext(stack, s);
	TimesHeroLevelDivideStackLevelUpdater updater;
	WithoutLibrary absent;
	const auto pending = updater.createUpdatedBonus(input(), heroContext);
	ASSERT_NE(pending->updater, nullptr);
	const auto result = pending->updater->createUpdatedBonus(pending, stackContext);
	EXPECT_EQ(result->val, 75); //25 * floor(11/3), not floor(25*11/3).
	EXPECT_EQ(result->updater, nullptr);
}
TEST(OwnedBonusEvaluationFactsTest, NonIdempotentMultiplierDistinguishesRawFromAlreadyUpdatedInput)
{
	CBonusSystemNode shell(BonusNodeType::HERO, true);
	const auto facts = heroFacts(3);
	BonusEvaluationContext context(shell, facts);
	TimesHeroLevelUpdater updater;
	const auto raw = input(2);
	WithoutLibrary absent;
	const auto first = updater.createUpdatedBonus(raw, context);
	EXPECT_EQ(first->val, 6);
	EXPECT_EQ(updater.createUpdatedBonus(raw, context)->val, 6);
	EXPECT_EQ(updater.createUpdatedBonus(first, context)->val, 18);
}

TEST(OwnedBonusEvaluationFactsTest, LiveFilteredPlaceholderSkipsUnsafeLevelGetter)
{
	CGHeroInstance source(nullptr);
	source.putStack(SlotID(0), std::make_unique<CStackInstance>(nullptr));
	BonusEvaluationContext live(source);
	TimesArmySizeUpdater updater;
	updater.minimum = 0;
	updater.maximum = 100;
	updater.filteredCreature = CreatureID(0);
	EXPECT_EQ(updater.createUpdatedBonus(input(), live)->val, 0);
	updater.filteredCreature = CreatureID();
	updater.filteredFaction = FactionID(0);
	EXPECT_EQ(updater.createUpdatedBonus(input(), live)->val, 0);
}

TEST(OwnedBonusEvaluationFactsTest, CapturedPlaceholderLevelIsRequiredOnlyAfterEarlierCanonicalFilters)
{
	CBonusSystemNode shell(BonusNodeType::ARMY, true);
	OwnedBonusEvaluationFacts facts; facts.nodeType = BonusNodeType::ARMY;
	facts.armySlots = std::vector<OwnedBonusEvaluationFacts::ArmySlot>{{CreatureID(), FactionID(2), std::nullopt, 7}};
	BonusEvaluationContext context(shell, facts);
	WithoutLibrary absent;
	EXPECT_EQ(context.filteredArmySize(CreatureID(1), FactionID(), 3), 0);
	EXPECT_EQ(context.filteredArmySize(CreatureID(), FactionID(1), 3), 0);
	EXPECT_THROW(context.filteredArmySize(CreatureID(), FactionID(2), 3), std::logic_error);
	EXPECT_EQ(context.filteredArmySize(CreatureID(), FactionID(2), -1), 7); // No level filter never reads unknown level.
	facts.armySlots->front().level = 0;
	EXPECT_EQ(context.filteredArmySize(CreatureID(), FactionID(2), 0), 7); // Captured zero is concrete, not unknown.
}
