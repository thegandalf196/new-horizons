/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/bonuses/BonusEvaluationContext.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/battle/BattleBonusAcceptance.h"
#include "../../lib/mapObjects/army/CStackInstance.h"
#include "../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
namespace
{
struct WithoutLibrary
{
	GameLibrary * original = LIBRARY;
	WithoutLibrary() { LIBRARY = nullptr; }
	~WithoutLibrary() { LIBRARY = original; }
};
class RankCounter final : public CStackInstance
{
public:
	mutable int calls = 0;
	int rank = 5;
	explicit RankCounter(BonusNodeType type = BonusNodeType::STACK_INSTANCE)
		: CStackInstance(nullptr, type, true) {}
	int getExpRank() const override { ++calls; return rank; }
};
Bonus value(BonusSource source = BonusSource::OTHER)
{
	return Bonus(BonusDuration::PERMANENT, BonusType::STACKS_SPEED, source, 2, BonusSourceID());
}
}
TEST(BonusTypedContextSeamsTest, RankPreservesLazyTwoReadAndShortCircuitBoundaries)
{
	RankCounter native;
	BonusEvaluationContext context(native);
	EXPECT_EQ(context.matchesRank(3, 7), true);
	EXPECT_EQ(native.calls, 2);
	native.calls = 0;
	EXPECT_EQ(context.matchesRank(5, 7), false);
	EXPECT_EQ(native.calls, 1);
}
TEST(BonusTypedContextSeamsTest, CommanderDirectAbsenceDiffersFromBattleCommanderDiscard)
{
	RankCounter commander(BonusNodeType::COMMANDER);
	EXPECT_FALSE(BonusEvaluationContext(commander).matchesRank(0, 10));
	CStackBasicDescriptor descriptor(CreatureID(0), 8);
	CStack battle(&descriptor, PlayerColor(0), 17, BattleSide::ATTACKER);
	battle.base = &commander;
	EXPECT_EQ(BonusEvaluationContext(battle).matchesRank(0, 10), false);
	EXPECT_EQ(commander.calls, 0);
}
TEST(BonusTypedContextSeamsTest, CreatureExactMatchNeedsNoUnrelatedFamilyLookup)
{
	CStackInstance native(nullptr, CreatureID(0), 8, true);
	BonusEvaluationContext context(native);
	EXPECT_EQ(context.matchesCreature(CreatureID(0), false), true);
	EXPECT_EQ(context.matchesCreature(CreatureID(0), true), true);
	EXPECT_EQ(context.matchesCreature(CreatureID::NONE, false), false);
}
TEST(BonusTypedContextSeamsTest, CapturedAdventureNoneRemainsAnEngagedTerrainBeforeFallback)
{
	CStackInstance native(nullptr, BonusNodeType::STACK_INSTANCE, true);
	Bonus bonus = value();
	BonusList accepted, undecided;
	BonusLimitationContext context(bonus, native, accepted, undecided);
	TerrainLimiter limiter(TerrainId::NONE);
	WithoutLibrary absent;
	ASSERT_TRUE(context.evaluation.adventureTerrain());
	EXPECT_EQ(*context.evaluation.adventureTerrain(), TerrainId::NONE);
	EXPECT_EQ(limiter.limit(context), ILimiter::EDecision::ACCEPT);
}
TEST(BonusTypedContextSeamsTest, NativeTerrainAcceptedDependencyPrecedesFactionResolution)
{
	CStackInstance native(nullptr, BonusNodeType::STACK_INSTANCE, true);
	Bonus bonus = value();
	BonusList accepted, undecided;
	auto flag = std::make_shared<Bonus>(value());
	flag->type = BonusType::TERRAIN_NATIVE;
	accepted.push_back(flag);
	BonusLimitationContext context(bonus, native, accepted, undecided);
	TerrainLimiter limiter;
	WithoutLibrary absent;
	EXPECT_EQ(limiter.limit(context), ILimiter::EDecision::ACCEPT);
}
TEST(BonusTypedContextSeamsTest, NativeUnrelatedSourceAcceptanceDoesNotResolveHeroOrRegistry)
{
	CStackBasicDescriptor descriptor(CreatureID(0), 8);
	CStack native(&descriptor, PlayerColor(0), 17, BattleSide::ATTACKER);
	const Bonus bonus = value();
	WithoutLibrary absent;
	EXPECT_TRUE(battle::acceptsBattleBonus(native, native, bonus));
}
TEST(BonusTypedContextSeamsTest, CopiedAcceptanceUsesNumericSkillAndLazyFormSourceRules)
{
	battle::BonusAcceptanceFacts::Static metadata;
	metadata.sylvanSkill = SecondarySkill(123);
	metadata.heroPresent = true;
	metadata.wildChance = true;
	metadata.slot = SlotID(0);
	battle::BonusAcceptanceFacts::State state;
	state.summoned = state.natureSummoned = true;
	Bonus bonus = value(BonusSource::SECONDARY_SKILL);
	bonus.sid = BonusSourceID(SecondarySkill(123));
	WithoutLibrary absent;
	EXPECT_TRUE(battle::acceptsBattleBonus(bonus, metadata, state));
	metadata.heroPresent = false;
	EXPECT_FALSE(battle::acceptsBattleBonus(bonus, metadata, state));
	metadata.tacticsEnabled = true;
	metadata.slot = SlotID::WAR_MACHINES_SLOT;
	EXPECT_FALSE(battle::acceptsBattleBonus(bonus, metadata, state));
	state.hasForm = true;
	state.currentForm = CreatureID(2);
	state.originalForm = CreatureID(1);
	bonus.source = BonusSource::CREATURE_ABILITY;
	bonus.sid = BonusSourceID(CreatureID(1));
	EXPECT_FALSE(battle::acceptsBattleBonus(bonus, metadata, state));
	bonus.sid = BonusSourceID(CreatureID(2));
	EXPECT_TRUE(battle::acceptsBattleBonus(bonus, metadata, state));
}
