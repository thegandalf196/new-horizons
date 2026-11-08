/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "../../AI/BattleAI/BattleAIDiagnosticAdmission.h"
#include "../../AI/BattleAI/DiagnosticBattleAIFactory.h"
#include "../../AI/BattleAI/HeroOrderCandidates.h"
#include "../../AI/BattleAI/BattleAI.h"
#include <algorithm>
#include <stdexcept>
using namespace battleAIDiagnostic;
using namespace heroQueueTest;
namespace
{
class CountingValuation final : public heroOrderCandidates::Valuation
{
public:
	mutable unsigned calls = 0;
	float score(const heroOrderCandidates::ValuationRequest &) const override { ++calls; return 1.0f; }
};
}
class BattleAIDiagnosticAdmissionTest : public HeroQueueSelectorTest
{
protected:
	BattleAIDiagnosticAdmissionTest() { useRevisedOrders = true; }
	void prepare(bool arrow = false)
	{
		ASSERT_NO_FATAL_FAILURE(world());
		if(arrow) ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::MAGIC_ARROW, "new-horizons:sorceryMagic"));
		ASSERT_NO_FATAL_FAILURE(arena());
		callback = std::make_shared<QueueCallback>();
		callback->onBattleStarted(battle());
		live = callback->getBattle(BattleID(0));
		environment = std::make_shared<QueueEnvironment>(gameState());
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	}
};
TEST(BattleAIDiagnosticPolicyTest, EmptyMaskKeepsNormalFactoryAndUnsupportedProfilesReject)
{
	EXPECT_EQ(Mask::prepare({}), nullptr);
	EXPECT_NE(std::dynamic_pointer_cast<CBattleAI>(battleAIDiagnostic::createBattleAI("BattleAI", {})), nullptr);
	EXPECT_NE(std::dynamic_pointer_cast<CBattleAI>(battleAIDiagnostic::createBattleAI("BattleAIQuick", {})), nullptr);
	EXPECT_THROW(battleAIDiagnostic::createBattleAI("BattleAIBeamOne", {}), std::invalid_argument);
	EXPECT_THROW(battleAIDiagnostic::createBattleAI("StupidAI", {}), std::invalid_argument);
}
TEST_F(BattleAIDiagnosticAdmissionTest, PerIdMasksPreserveOtherKindsAndCountOnlyThisAdmission)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	Spec spec;
	spec.orders = {HeroCommand::FOCUS_FIRE};
	spec.manualHeroSpells = {SpellID::CURSE};
	const auto mask = Mask::prepare(spec);
	Counts first, second;
	Admission a(*mask, first), b(*mask, second);
	EXPECT_FALSE(a.permits(HeroCommand::FOCUS_FIRE));
	EXPECT_TRUE(a.permits(HeroCommand::CHARGE));
	EXPECT_FALSE(a.permits(SpellID(SpellID::CURSE)));
	EXPECT_TRUE(a.permits(SpellID(SpellID::CURE)));
	EXPECT_EQ(first.orderKindsVisited, 2u); EXPECT_EQ(first.orderKindsPruned, 1u);
	EXPECT_EQ(first.spellRowsVisited, 2u); EXPECT_EQ(first.spellRowsPruned, 1u);
	EXPECT_TRUE(b.permits(HeroCommand::CHARGE));
	EXPECT_EQ(second.orderKindsPruned, 0u);
	EXPECT_EQ(second.spellRowsVisited, 0u);
}
TEST(BattleAIDiagnosticPolicyTest, InvalidRetiredDuplicateAndConflictingIdsReject)
{
	Spec spec; spec.orders = {HeroCommand::PROTECT};
	EXPECT_THROW(Mask::prepare(spec), std::invalid_argument);
	spec.orders = {HeroCommand::NONE};
	EXPECT_THROW(Mask::prepare(spec), std::invalid_argument);
	spec.orders = {HeroCommand::CHARGE, HeroCommand::CHARGE};
	EXPECT_THROW(Mask::prepare(spec), std::invalid_argument);
	spec.orders = {HeroCommand::CHARGE}; spec.suppressAllOrders = true;
	EXPECT_THROW(Mask::prepare(spec), std::invalid_argument);
	spec = {}; spec.manualHeroSpells = {SpellID(-1)};
	EXPECT_THROW(Mask::prepare(spec), std::invalid_argument);
}
TEST(BattleAIDiagnosticPolicyTest, MandatoryFollowupsRefuseBeforeAnyPruning)
{
	Spec orders; orders.suppressAllOrders = true;
	const auto orderMask = Mask::prepare(orders);
	Counts counts; Admission orderAdmission(*orderMask, counts);
	EXPECT_THROW(orderAdmission.requireSupportedContext(true, false), std::invalid_argument);
	EXPECT_EQ(counts.orderKindsVisited, 0u);
	Spec spells; spells.suppressAllManualHeroSpells = true;
	const auto spellMask = Mask::prepare(spells);
	Admission spellAdmission(*spellMask, counts);
	EXPECT_THROW(spellAdmission.requireSupportedContext(false, true), std::invalid_argument);
	EXPECT_EQ(counts.spellRowsVisited, 0u);
	EXPECT_NO_THROW(orderAdmission.requireSupportedContext(false, true));
}
TEST_F(BattleAIDiagnosticAdmissionTest, AllOrdersPruneBeforeValuationAndPreserveSpellPrefixAndLiveState)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	Spec spec; spec.suppressAllOrders = true;
	const auto mask = Mask::prepare(spec);
	Counts counts; Admission admission(*mask, counts); CountingValuation valuation;
	const auto before = observation();
	PossibleSpellcast prefix; prefix.spell = SpellID(SpellID::CURE).toSpell();
	std::vector<PossibleSpellcast> candidates{prefix};
	heroOrderCandidates::appendOrders(candidates, *callback, BattleID(0), BattleSide::ATTACKER, false, valuation, &admission);
	ASSERT_EQ(candidates.size(), 1u); EXPECT_EQ(candidates.front().spell, prefix.spell);
	EXPECT_EQ(valuation.calls, 0u);
	EXPECT_EQ(counts.orderKindsPruned, 8u);
	EXPECT_EQ(observation(), before);
}
TEST_F(BattleAIDiagnosticAdmissionTest, AllHeroCandidatesPruneWithoutRequestManaRngOrCommandMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	Spec spec; spec.suppressAllOrders = true; spec.suppressAllManualHeroSpells = true;
	const auto mask = Mask::prepare(spec);
	Counts counts; Admission admission(*mask, counts);
	const auto before = observation();
	BattleEvaluator evaluator(environment, callback, active, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1, 2);
	EXPECT_FALSE(evaluator.attemptCastingSpell(active, true, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &admission));
	EXPECT_GT(counts.spellRowsPruned, 0u); EXPECT_EQ(counts.orderKindsPruned, 8u);
	EXPECT_TRUE(callback->heroRequests.empty());
	EXPECT_EQ(observation(), before);
}
TEST_F(BattleAIDiagnosticAdmissionTest, OneOrderIdPrunesOnlyItsKindAndKeepsOtherCandidateOrdering)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	CountingValuation baselineValuation, filteredValuation;
	std::vector<PossibleSpellcast> baseline, filtered;
	heroOrderCandidates::appendOrders(baseline, *callback, BattleID(0), BattleSide::ATTACKER, false, baselineValuation);
	Spec spec; spec.orders = {HeroCommand::CHARGE};
	const auto mask = Mask::prepare(spec);
	Counts counts; Admission admission(*mask, counts);
	const auto before = observation();
	heroOrderCandidates::appendOrders(filtered, *callback, BattleID(0), BattleSide::ATTACKER, false, filteredValuation, &admission);
	ASSERT_TRUE(std::ranges::any_of(baseline, [](const auto & c) { return c.command == HeroCommand::CHARGE; }));
	std::erase_if(baseline, [](const auto & c) { return c.command == HeroCommand::CHARGE; });
	ASSERT_FALSE(baseline.empty()); ASSERT_EQ(filtered.size(), baseline.size());
	for(size_t index = 0; index < baseline.size(); ++index)
	{
		EXPECT_EQ(filtered[index].command, baseline[index].command);
		EXPECT_EQ(filtered[index].commandTargets, baseline[index].commandTargets);
	}
	EXPECT_EQ(counts.orderKindsPruned, 1u);
	EXPECT_EQ(observation(), before);
}
TEST_F(BattleAIDiagnosticAdmissionTest, OneManualSpellIdPrunesAnOtherwiseSubmittedCastBeforeTargets)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	Spec control; control.suppressAllOrders = true;
	const auto controlMask = Mask::prepare(control);
	Counts controlCounts; Admission controlAdmission(*controlMask, controlCounts);
	const auto before = observation();
	BattleEvaluator original(environment, callback, active, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1, 2);
	ASSERT_TRUE(original.attemptCastingSpell(active, true, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &controlAdmission));
	ASSERT_EQ(callback->heroRequests.size(), 1u);
	EXPECT_EQ(callback->heroRequests.front().spell, SpellID(SpellID::MAGIC_ARROW));
	callback->heroRequests.clear();
	Spec filtered = control; filtered.manualHeroSpells = {SpellID::MAGIC_ARROW};
	const auto mask = Mask::prepare(filtered);
	Counts counts; Admission admission(*mask, counts);
	BattleEvaluator pruned(environment, callback, active, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1, 2);
	EXPECT_FALSE(pruned.attemptCastingSpell(active, true, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &admission));
	EXPECT_EQ(counts.spellRowsPruned, 1u);
	EXPECT_TRUE(callback->heroRequests.empty());
	EXPECT_EQ(observation(), before);
}
