/*
 * HeroActionSelectionTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroActionSelectionObserver.h"
#include "../../AI/BattleAI/PossibleSpellcast.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../lib/spells/CSpell.h"
#include "../mock/mock_battle_Unit.h"
#include <type_traits>
#include <array>
#include <bit>
#include <cmath>
#include <limits>

namespace
{
using heroActionSelection::Disposition;
PossibleSpellcast value(float score, HeroCommand command = HeroCommand::NONE)
{
	PossibleSpellcast result;
	result.command = command;
	result.value = score;
	return result; // Synthetic finalized values, not a legal-action generator.
}
uint32_t bits(float value) { return std::bit_cast<uint32_t>(value); }
}

TEST(HeroActionSelectionTest, EmptyInputProducesNoSelectionOrObservation)
{
	heroSelectionTest::Observer observer;
	EXPECT_FALSE(heroActionSelection::choose({}, 10.0f, false, &observer));
	EXPECT_EQ(observer.count, 0u);
	EXPECT_EQ(observer.selections, 0u);
	EXPECT_FALSE(observer.selection);
}

TEST(HeroActionSelectionTest, BaselineRetainsExactSentinelThresholdAndFloatBits)
{
	const float threshold = static_cast<float>(EvaluationResult::INEFFECTIVE_SCORE / 2);
	EXPECT_EQ(bits(heroActionSelection::noCastBaseline(threshold)), bits(0.0f));
	EXPECT_EQ(bits(heroActionSelection::noCastBaseline(
		static_cast<float>(EvaluationResult::INEFFECTIVE_SCORE))), bits(0.0f));
	const float above = std::nextafter(threshold, 0.0f);
	EXPECT_EQ(bits(heroActionSelection::noCastBaseline(above)), bits(above));
	EXPECT_EQ(bits(heroActionSelection::noCastBaseline(-0.0f)), bits(-0.0f));
	EXPECT_EQ(bits(heroActionSelection::noCastBaseline(17.125f)), bits(17.125f));
	EXPECT_EQ(bits(heroActionSelection::noCastBaseline(
		std::numeric_limits<float>::quiet_NaN())), bits(0.0f));
}

TEST(HeroActionSelectionTest, FirstExactTieRespectsSpellOrderRosterAndCopiedIdentity)
{
	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	ASSERT_NE(spell, nullptr);
	auto cast = value(7.0f);
	cast.spell = spell;
	cast.spellOvercharge = 2;
	const auto order = value(7.0f, HeroCommand::FLANK);
	for(const bool orderFirst : {false, true})
	{
		SCOPED_TRACE(orderFirst);
		const std::array<PossibleSpellcast, 2> candidates = orderFirst
			? std::array{order, cast} : std::array{cast, order};
		heroSelectionTest::Observer observer;
		const auto selected = heroActionSelection::choose(candidates, 0.0f, false, &observer);
		ASSERT_TRUE(selected);
		EXPECT_EQ(selected->ordinal, 0u);
		EXPECT_EQ(selected->disposition, Disposition::PERFORM);
		EXPECT_EQ(observer.count, 2u);
		EXPECT_EQ(observer.selections, 1u);
		ASSERT_TRUE(observer.selection);
		EXPECT_EQ(observer.selection->ordinal, 0u);
		EXPECT_EQ(observer.best.command, candidates.front().command);
		EXPECT_EQ(observer.best.spell, orderFirst ? SpellID::NONE : spell->getId());
		EXPECT_EQ(observer.best.overcharge, orderFirst ? 0 : 2);
		EXPECT_EQ(bits(observer.best.value), bits(7.0f));
	}
}

TEST(HeroActionSelectionTest, MaximumUsesStrictComparisonNotApproximateTie)
{
	const float larger = std::nextafter(7.0f, 8.0f);
	ASSERT_TRUE(vstd::isAlmostEqual(7.0f, larger));
	const std::array candidates{value(7.0f), value(larger, HeroCommand::FLANK)};
	const auto selected = heroActionSelection::choose(candidates, 0.0f, false);
	ASSERT_TRUE(selected);
	EXPECT_EQ(selected->ordinal, 1u);
	EXPECT_EQ(bits(candidates[selected->ordinal].value), bits(larger));
}

TEST(HeroActionSelectionTest, OrdinaryMandatoryAndBothTypedFollowupsKeepAdmission)
{
	struct Case { float score; bool mandatory; bool meta; bool divine; bool grand; Disposition expected; };
	const std::array cases{
		Case{9, false, false, false, false, Disposition::USELESS},
		Case{10, false, false, false, false, Disposition::USELESS},
		Case{11, false, false, false, false, Disposition::PERFORM},
		Case{9, false, true, false, false, Disposition::RETAIN_FOLLOWUP},
		Case{10, false, true, false, false, Disposition::RETAIN_FOLLOWUP},
		Case{11, false, true, false, false, Disposition::PERFORM},
		Case{9, false, false, true, false, Disposition::RETAIN_FOLLOWUP},
		Case{10, false, false, true, false, Disposition::RETAIN_FOLLOWUP},
		Case{11, false, false, true, false, Disposition::PERFORM},
		Case{-2, true, false, false, false, Disposition::PERFORM},
		Case{-2, true, true, false, false, Disposition::PERFORM},
		Case{-2, true, false, true, false, Disposition::PERFORM},
		Case{9, false, true, true, false, Disposition::RETAIN_FOLLOWUP},
		Case{9, false, false, false, true, Disposition::USELESS}};
	for(const auto & c : cases)
	{
		SCOPED_TRACE(::testing::Message() << c.score << '/' << c.mandatory << '/' << c.meta << '/' << c.divine);
		auto candidate = value(c.score);
		candidate.metamagicFollowup = c.meta;
		candidate.divineMandateFollowup = c.divine;
		candidate.metamagicGrand = c.grand;
		const std::array candidates{candidate};
		heroSelectionTest::Observer observer;
		const auto selected = heroActionSelection::choose(candidates, 10.0f, c.mandatory, &observer);
		const auto unobserved = heroActionSelection::choose(candidates, 10.0f, c.mandatory);
		ASSERT_TRUE(selected);
		ASSERT_TRUE(unobserved);
		EXPECT_EQ(selected->ordinal, 0u);
		EXPECT_EQ(selected->disposition, c.expected);
		EXPECT_EQ(unobserved->disposition, c.expected);
		ASSERT_TRUE(observer.selection);
		EXPECT_EQ(observer.selection->disposition, c.expected);
		EXPECT_EQ(bits(observer.selection->baseline), bits(10.0f));
	}
}

TEST(HeroActionSelectionTest, NearBaselineToleranceAppliesToAdmissionOnly)
{
	const float near = std::nextafter(10.0f, 11.0f);
	ASSERT_GT(near, 10.0f);
	ASSERT_TRUE(vstd::isAlmostEqual(near, 10.0f));
	for(const bool followup : {false, true})
	{
		auto candidate = value(near);
		candidate.metamagicFollowup = followup;
		const std::array candidates{candidate};
		const auto selected = heroActionSelection::choose(candidates, 10.0f, false);
		ASSERT_TRUE(selected);
		EXPECT_EQ(selected->disposition, followup ? Disposition::RETAIN_FOLLOWUP : Disposition::USELESS);
	}
}

TEST(HeroActionSelectionTest, NonfiniteAndSignedZeroPreserveStrictMaximumAndAdmission)
{
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float infinity = std::numeric_limits<float>::infinity();
	struct Case { std::array<float, 3> scores; size_t ordinal; Disposition expected; };
	const std::array cases{
		Case{{-0.0f, 0.0f, -1.0f}, 0, Disposition::PERFORM},
		Case{{nan, 50.0f, infinity}, 0, Disposition::USELESS},
		Case{{-1.0f, nan, 5.0f}, 2, Disposition::PERFORM},
		Case{{-infinity, infinity, infinity}, 1, Disposition::USELESS},
		Case{{-3.0f, -2.0f, -4.0f}, 1, Disposition::PERFORM}};
	for(const auto & c : cases)
	{
		const std::array candidates{value(c.scores[0]), value(c.scores[1]), value(c.scores[2])};
		heroSelectionTest::Observer observer;
		const auto observed = heroActionSelection::choose(candidates, -10.0f, false, &observer);
		const auto plain = heroActionSelection::choose(candidates, -10.0f, false);
		ASSERT_TRUE(observed);
		ASSERT_TRUE(plain);
		EXPECT_EQ(observed->ordinal, c.ordinal);
		EXPECT_EQ(plain->ordinal, c.ordinal);
		EXPECT_EQ(plain->disposition, observed->disposition);
		EXPECT_EQ(observed->disposition, c.expected);
		EXPECT_EQ(observer.count, 3u);
		EXPECT_FALSE(observer.invalidOrdinal);
		for(size_t index = 0; index < 3; ++index)
			EXPECT_EQ(bits(candidates[index].value), bits(c.scores[index]));
	}
	for(const bool typed : {false, true})
	{
		auto candidate = value(nan);
		candidate.divineMandateFollowup = typed;
		const std::array candidates{candidate};
		const auto selected = heroActionSelection::choose(candidates, 10.0f, false);
		ASSERT_TRUE(selected);
		EXPECT_EQ(selected->disposition, typed ? Disposition::PERFORM : Disposition::USELESS);
		const auto mandatory = heroActionSelection::choose(candidates, 10.0f, true);
		ASSERT_TRUE(mandatory);
		EXPECT_EQ(mandatory->disposition, Disposition::PERFORM);
	}
}

TEST(HeroActionSelectionTest, GenerationPriorCannotOverrideFinalCommonUtility)
{
	auto spell = value(11.0f);
	auto flank = value(10.0f, HeroCommand::FLANK);
	flank.commandHeuristicValue = 1000000.0f;
	const std::array candidates{spell, flank};
	heroSelectionTest::Observer observer;
	const auto selected = heroActionSelection::choose(candidates, 9.0f, false, &observer);
	ASSERT_TRUE(selected);
	EXPECT_EQ(selected->ordinal, 0u);
	EXPECT_EQ(observer.count, 2u);
	EXPECT_FALSE(observer.invalidOrdinal);
	EXPECT_TRUE(observer.hasFlank);
	EXPECT_EQ(bits(observer.bestFlank), bits(10.0f));
	EXPECT_EQ(bits(observer.best.value), bits(11.0f));
	EXPECT_EQ(flank.commandHeuristicValue, 1000000.0f);
}

TEST(HeroActionSelectionTest, NullAndLegacyObserversDoNotInspectOriginalUnitTargets)
{
	::testing::StrictMock<UnitMock> target;
	EXPECT_CALL(target, unitId()).Times(0);
	auto cast = value(7.0f);
	cast.dest = {spells::Destination(&target, BattleHex(8, 5))};
	cast.commandTargets = {10, 11};
	const std::array candidates{cast};
	EXPECT_TRUE(heroActionSelection::choose(candidates, 0.0f, false));
	heroSelectionTest::Observer legacy;
	EXPECT_TRUE(heroActionSelection::choose(candidates, 0.0f, false, &legacy));
	EXPECT_EQ(legacy.count, 1u);
}

TEST(HeroActionSelectionTest, OptInIdentityCopiesOrderedTargetsAndAllOptionValuesWithoutRetainingViews)
{
	class Detailed final : public heroActionSelection::Observer
	{
	public:
		std::array<heroActionSelection::CandidateIdentity, 2> identities;
		size_t received = 0;
		bool invalid = false;
		void candidate(heroActionSelection::CandidateValue) noexcept override {}
		void selected(heroActionSelection::SelectionValue) noexcept override {}
		bool observeIdentity() const noexcept override { return true; }
		void candidateIdentity(size_t ordinal, heroActionSelection::CandidateIdentity identity) noexcept override
		{
			static_assert(std::is_nothrow_move_assignable_v<heroActionSelection::CandidateIdentity>);
			if(ordinal >= identities.size() || ordinal != received)
				invalid = true;
			else
				identities[received++] = std::move(identity);
		}
	} observer;
	::testing::NiceMock<UnitMock> enemy, first, second;
	ON_CALL(enemy, unitId()).WillByDefault(::testing::Return(10u));
	ON_CALL(first, unitId()).WillByDefault(::testing::Return(20u));
	ON_CALL(second, unitId()).WillByDefault(::testing::Return(21u));
	auto a = value(7.0f);
	a.dest = {spells::Destination(&enemy, BattleHex(8, 5)), spells::Destination(&first, BattleHex(3, 3))};
	a.commandTargets = {20, 10};
	a.spellCurePhysicalAffliction = "frozen";
	a.spellPurifyChoices = {{10, SpellID::SLOW}};
	a.spellHandOfFateExpectedValue = -0.0f;
	a.metamagicFollowup = true;
	auto b = a;
	b.dest[1] = spells::Destination(&second, BattleHex(4, 7));
	b.commandTargets = {10, 20};
	b.spellHandOfFateExpectedValue = 0.0f;
	const std::array candidates{a, b};
	ASSERT_TRUE(heroActionSelection::choose(candidates, 0.0f, false, &observer));
	ASSERT_FALSE(observer.invalid);
	ASSERT_EQ(observer.received, 2u);
	EXPECT_EQ(observer.identities[0].targets, (std::vector<heroActionSelection::TargetIdentity>{
		{10u, BattleHex(8, 5)}, {20u, BattleHex(3, 3)}}));
	EXPECT_EQ(observer.identities[1].targets[1].unitId, 21u);
	EXPECT_EQ(observer.identities[1].targets[1].hex, BattleHex(4, 7));
	EXPECT_EQ(observer.identities[0].options, heroActionSelection::candidateOptions(a));
	EXPECT_EQ(observer.identities[1].options, heroActionSelection::candidateOptions(b));
	EXPECT_NE(observer.identities[0].options, observer.identities[1].options);
}
