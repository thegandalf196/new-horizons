/*
 * HeroExpectedSpellComponentTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroExpectedSpellFixture.h"
#include "../../AI/BattleAI/HeroSpellExpectedValueProjection.h"
#include "../../AI/BattleAI/HeroTargetHeuristicProjection.h"
#include "../../AI/BattleAI/PossibleSpellcast.h"
#include "../../lib/spells/CSpell.h"

#include <array>
#include <bit>
#include <cfenv>
#include <limits>

namespace heroExpectedTest
{
namespace
{
// Independent preserved BEFORE expression/branch oracle; never counted as a selector path.
bool originalProjection(PossibleSpellcast & ps, const std::shared_ptr<HypotheticBattle> & state,
	BattleSide side, const float & baseline, const HypotheticBattle::ProjectedCounterspellOutcome & counterspell,
	bool counterspellNegated, const std::optional<HypotheticBattle::ProjectedSpellAllowance> & spellAllowance,
	const std::vector<uint32_t> & sharedPurposeSpellRecipients, uint32_t & targetId)
{
// Hand of Fate's secondary recipient is random by design. Its signed
// expected value was computed across the complete pool during target
// enumeration; never replace that expectation with castEval's one
// RNGStub-selected recipient.
if(ps.command == HeroCommand::NONE && ps.spellHandOfFateExpectedValue.has_value())
{
	if(ps.dest.size() != 1 || !ps.dest.front().unitValue)
	{
		ps.value = std::numeric_limits<float>::lowest();
		return true;
	}
	targetId = ps.dest.front().unitValue->unitId();
	if(counterspellNegated
		|| !state->projectAcceptedHeroSpell(side, ps.spell->getId(), targetId,
			ps.metamagicFollowup, ps.metamagicGrand, counterspell.wardActive,
			counterspellNegated, *spellAllowance, sharedPurposeSpellRecipients))
		ps.value = std::numeric_limits<float>::lowest();
	else
		ps.value = baseline + *ps.spellHandOfFateExpectedValue;
	return true;
}
// Battle-form identity is an effect mechanic, not a registered spell
// ID. Its signed expectation covers the full shared form pool; skip the
// generic castEval projection so RNGStub cannot replace it with one form.
if(ps.command == HeroCommand::NONE && ps.spellBattleFormExpectedValue.has_value())
{
	if(ps.dest.size() != 1 || !ps.dest.front().unitValue)
	{
		ps.value = std::numeric_limits<float>::lowest();
		return true;
	}
	targetId = ps.dest.front().unitValue->unitId();
	if(counterspellNegated
		|| !state->projectAcceptedHeroSpell(side, ps.spell->getId(), targetId,
			ps.metamagicFollowup, ps.metamagicGrand, counterspell.wardActive,
			counterspellNegated, *spellAllowance, sharedPurposeSpellRecipients))
		ps.value = std::numeric_limits<float>::lowest();
	else
		ps.value = baseline + *ps.spellBattleFormExpectedValue;
	return true;
}
if(ps.command == HeroCommand::NONE && ps.spellConfusionExpectedValue.has_value())
{
	if(ps.dest.size() != 1 || !ps.dest.front().unitValue)
	{
		ps.value = std::numeric_limits<float>::lowest();
		return true;
	}
	targetId = ps.dest.front().unitValue->unitId();
	if(counterspellNegated
		|| !state->projectAcceptedHeroSpell(side, ps.spell->getId(), targetId,
			ps.metamagicFollowup, ps.metamagicGrand, counterspell.wardActive,
			counterspellNegated, *spellAllowance, sharedPurposeSpellRecipients))
		ps.value = std::numeric_limits<float>::lowest();
	else
		ps.value = baseline + *ps.spellConfusionExpectedValue;
	return true;
}
// Puppet Master has no immediate HP delta. Value the target's
// projected controlled activation from target enumeration, then keep
// that candidate-specific score instead of letting generic castEval
// collapse the control marker to zero.
if(ps.command == HeroCommand::NONE && ps.spellPuppetMasterExpectedValue.has_value())
{
	if(ps.dest.size() != 1 || !ps.dest.front().unitValue)
	{
		ps.value = std::numeric_limits<float>::lowest();
		return true;
	}
	targetId = ps.dest.front().unitValue->unitId();
	if(counterspellNegated
		|| !state->projectAcceptedHeroSpell(side, ps.spell->getId(), targetId,
			ps.metamagicFollowup, ps.metamagicGrand, counterspell.wardActive,
			counterspellNegated, *spellAllowance, sharedPurposeSpellRecipients))
		ps.value = std::numeric_limits<float>::lowest();
	else
		ps.value = baseline + *ps.spellPuppetMasterExpectedValue;
	return true;
}
	return false;
}

constexpr std::array fields{&PossibleSpellcast::spellHandOfFateExpectedValue,
	&PossibleSpellcast::spellBattleFormExpectedValue, &PossibleSpellcast::spellConfusionExpectedValue,
	&PossibleSpellcast::spellPuppetMasterExpectedValue};
constexpr std::array<uint32_t, 10> patterns{0x00000000, 0x80000000, 0x3f800000, 0xbf800000,
	0x7f800000, 0xff800000, 0x7fc00001, 0x7fc12345, 0x7f800001, 0x00000001};

class SavedFloatingEnvironment final
{
	std::fenv_t saved;
public:
	const bool valid = std::feholdexcept(&saved) == 0;
	~SavedFloatingEnvironment() { if(valid) std::fesetenv(&saved); }
};

// Preserved terminal body shared by the original Hydra/Blink branches. Canonical
// admission and positive-value gates stay in the selector and are source-checked.
void originalTargetProjection(PossibleSpellcast & ps, HypotheticBattle & state,
	BattleSide side, const float & baseline, const HypotheticBattle::ProjectedCounterspellOutcome & counterspell,
	bool counterspellNegated, const std::optional<HypotheticBattle::ProjectedSpellAllowance> & spellAllowance,
	const std::vector<uint32_t> & sharedPurposeSpellRecipients, uint32_t & targetId)
{
	if(ps.dest.size() != 1 || !ps.dest.front().unitValue)
	{
		ps.value = std::numeric_limits<float>::lowest();
		return;
	}
	targetId = ps.dest.front().unitValue->unitId();
	if(counterspellNegated
		|| !state.projectAcceptedHeroSpell(side, ps.spell->getId(), targetId,
			ps.metamagicFollowup, ps.metamagicGrand, counterspell.wardActive,
			counterspellNegated, *spellAllowance, sharedPurposeSpellRecipients))
		ps.value = std::numeric_limits<float>::lowest();
	else
		ps.value = baseline + ps.spellPlacementHeuristicValue;
}
}

class HeroExpectedComponentTest : public HeroExpectedSpellTest
{
	std::string retained;
protected:
	void beginControls()
	{
		ASSERT_NO_FATAL_FAILURE(prepareExpected("new-horizons:handOfFate"));
		(void)detached();
		retained = observation();
		RecordProperty("componentOutsideBefore", retained);
	}
	void finishControls()
	{
		const auto after = observation();
		RecordProperty("componentOutsideAfter", after);
		EXPECT_EQ(after, retained);
	}
	void compare(PossibleSpellcast & input, const float & baseline, bool begin = true,
		bool negated = false, int * flags = nullptr)
	{
		auto oldState = detached(), newState = detached();
		const auto outside = observation();
		std::optional<HypotheticBattle::ProjectedSpellAllowance> oldPrepared, newPrepared;
		ASSERT_NO_FATAL_FAILURE(prepareTransition(oldState, oldPrepared, begin));
		ASSERT_NO_FATAL_FAILURE(prepareTransition(newState, newPrepared, begin));
		PossibleSpellcast oldCandidate = input, newCandidate = input;
		HypotheticBattle::ProjectedCounterspellOutcome counterspell;
		counterspell.resolutionKnown = true;
		counterspell.negated = negated;
		if(negated)
		{
			// Both target and negation guards must precede spell/allowance dereference.
			oldCandidate.spell = newCandidate.spell = nullptr;
			oldPrepared.reset();
			newPrepared.reset();
		}
		uint32_t oldTarget = 0xffffffff, newTarget = 0xffffffff;
		SavedFloatingEnvironment floating;
		ASSERT_TRUE(floating.valid);
		ASSERT_EQ(std::fesetround(FE_TONEAREST), 0);
		ASSERT_EQ(std::feclearexcept(FE_ALL_EXCEPT), 0);
		const bool oldHandled = originalProjection(oldCandidate, oldState, BattleSide::ATTACKER, baseline,
			counterspell, negated, oldPrepared, {}, oldTarget);
		const int oldFlags = std::fetestexcept(FE_ALL_EXCEPT);
		ASSERT_EQ(std::feclearexcept(FE_ALL_EXCEPT), 0);
		const bool newHandled = heroSpellProjection::projectPrecomputedValue(newCandidate, *newState,
			BattleSide::ATTACKER, baseline, counterspell, negated, newPrepared, {}, newTarget);
		const int newFlags = std::fetestexcept(FE_ALL_EXCEPT);
		EXPECT_EQ(newHandled, oldHandled);
		EXPECT_EQ(newTarget, oldTarget);
		EXPECT_EQ(std::bit_cast<uint32_t>(newCandidate.value), std::bit_cast<uint32_t>(oldCandidate.value));
		EXPECT_EQ(newFlags, oldFlags);
		if(flags)
			*flags = newFlags;
		for(const auto field : fields)
		{
			ASSERT_EQ((newCandidate.*field).has_value(), (input.*field).has_value());
			if(input.*field)
			{
				EXPECT_EQ(std::bit_cast<uint32_t>(*(newCandidate.*field)), std::bit_cast<uint32_t>(*(input.*field)));
			}
		}
		EXPECT_EQ(state(*newState), state(*oldState));
		EXPECT_EQ(observation(), outside);
	}

	PossibleSpellcast candidate()
	{
		PossibleSpellcast value;
		value.spell = selectedSpell.toSpell();
		value.dest = {spells::Destination(enemy)};
		return value;
	}

	void compareTarget(PossibleSpellcast input, bool blink, const float & baseline,
		bool begin = true, bool negated = false, bool changeAfterBinding = false)
	{
		auto oldState = detached(), newState = detached();
		const auto outside = observation();
		std::optional<HypotheticBattle::ProjectedSpellAllowance> oldPrepared, newPrepared;
		ASSERT_NO_FATAL_FAILURE(prepareTransition(oldState, oldPrepared, begin));
		ASSERT_NO_FATAL_FAILURE(prepareTransition(newState, newPrepared, begin));
		PossibleSpellcast oldCandidate = input, newCandidate = input;
		heroSpellProjection::TargetHeuristicForecast forecast = blink
			? heroSpellProjection::TargetHeuristicForecast{heroSpellProjection::BlinkLandingForecast{newCandidate.spellPlacementHeuristicValue}}
			: heroSpellProjection::TargetHeuristicForecast{heroSpellProjection::HydraRegenerationForecast{newCandidate.spellPlacementHeuristicValue}};
		if(changeAfterBinding)
			oldCandidate.spellPlacementHeuristicValue = newCandidate.spellPlacementHeuristicValue = 7.0f;
		HypotheticBattle::ProjectedCounterspellOutcome counterspell;
		counterspell.resolutionKnown = true;
		counterspell.negated = negated;
		if(negated || input.dest.size() != 1 || !input.dest.front().unitValue)
		{
			oldCandidate.spell = newCandidate.spell = nullptr;
			oldPrepared.reset();
			newPrepared.reset();
		}
		uint32_t oldTarget = 0xffffffff, newTarget = 0xffffffff;
		SavedFloatingEnvironment floating;
		ASSERT_TRUE(floating.valid);
		ASSERT_EQ(std::fesetround(FE_TONEAREST), 0);
		ASSERT_EQ(std::feclearexcept(FE_ALL_EXCEPT), 0);
		originalTargetProjection(oldCandidate, *oldState, BattleSide::ATTACKER, baseline,
			counterspell, negated, oldPrepared, {}, oldTarget);
		const int oldFlags = std::fetestexcept(FE_ALL_EXCEPT);
		ASSERT_EQ(std::feclearexcept(FE_ALL_EXCEPT), 0);
		heroSpellProjection::projectTargetHeuristic(newCandidate, forecast, *newState,
			BattleSide::ATTACKER, baseline, counterspell, negated, newPrepared, {}, newTarget);
		const int newFlags = std::fetestexcept(FE_ALL_EXCEPT);
		EXPECT_EQ(newTarget, oldTarget);
		EXPECT_EQ(std::bit_cast<uint32_t>(newCandidate.value), std::bit_cast<uint32_t>(oldCandidate.value));
		EXPECT_EQ(newFlags, oldFlags);
		EXPECT_EQ(state(*newState), state(*oldState));
		EXPECT_EQ(observation(), outside);
		if(negated || !begin || input.dest.size() != 1 || !input.dest.front().unitValue)
		{
			EXPECT_EQ(newCandidate.value, std::numeric_limits<float>::lowest());
			EXPECT_EQ(newFlags & FE_INVALID, 0);
		}
		else if(changeAfterBinding)
		{
			EXPECT_EQ(newCandidate.value, baseline + 7.0f);
		}
	}
};

TEST_F(HeroExpectedComponentTest, AfterOnlyAllMasksUseOriginalFirstFieldPrecedenceAndOrdersFallThrough)
{
	ASSERT_NO_FATAL_FAILURE(beginControls());
	for(unsigned mask = 0; mask < 16; ++mask)
	{
		auto value = candidate();
		for(size_t index = 0; index < fields.size(); ++index)
			if(mask & (1u << index))
				value.*fields[index] = static_cast<float>(index + 1);
		ASSERT_NO_FATAL_FAILURE(compare(value, 10.0f));
		value.command = HeroCommand::CHARGE;
		ASSERT_NO_FATAL_FAILURE(compare(value, 10.0f));
	}
	ASSERT_NO_FATAL_FAILURE(finishControls());
}

TEST_F(HeroExpectedComponentTest, AfterOnlyRejectedTargetsAndNegationNeverEvaluateNonfiniteOperands)
{
	ASSERT_NO_FATAL_FAILURE(beginControls());
	for(const auto field : fields)
	{
		auto value = candidate();
		value.*field = std::bit_cast<float>(uint32_t{0x7f800001});
		ASSERT_EQ(std::bit_cast<uint32_t>(*(value.*field)), 0x7f800001u);
		for(const auto & target : std::vector<spells::Target>{{}, {spells::Destination()},
			{spells::Destination(enemy), spells::Destination(enemy)}})
		{
			value.dest = target;
			int flags = 0;
			ASSERT_NO_FATAL_FAILURE(compare(value, 0.0f, true, true, &flags));
			EXPECT_EQ(flags & FE_INVALID, 0);
		}
		value.dest = {spells::Destination(enemy)};
		int flags = 0;
		ASSERT_NO_FATAL_FAILURE(compare(value, 0.0f, true, true, &flags));
		EXPECT_EQ(flags & FE_INVALID, 0);
	}
	ASSERT_NO_FATAL_FAILURE(finishControls());
}

TEST_F(HeroExpectedComponentTest, AfterOnlyUnbegunTransitionRejectsBeforeReadingExpectedValue)
{
	ASSERT_NO_FATAL_FAILURE(beginControls());
	for(const auto field : fields)
	{
		auto value = candidate();
		value.*field = std::bit_cast<float>(uint32_t{0x7f800001});
		ASSERT_EQ(std::bit_cast<uint32_t>(*(value.*field)), 0x7f800001u);
		int flags = 0;
		ASSERT_NO_FATAL_FAILURE(compare(value, 0.0f, false, false, &flags));
		EXPECT_EQ(flags & FE_INVALID, 0);
		// A changed source operand on a fresh accepted call is not cached or copied into persistent state.
		value.*field = -0.0f;
		ASSERT_NO_FATAL_FAILURE(compare(value, -0.0f));
	}
	ASSERT_NO_FATAL_FAILURE(finishControls());
}

TEST_F(HeroExpectedComponentTest, AfterOnlyFloatBitsAndExceptionFlagsMatchOriginalExpression)
{
	ASSERT_NO_FATAL_FAILURE(beginControls());
	for(const auto field : fields)
		for(const auto operand : patterns)
			for(const auto baseline : patterns)
			{
				auto value = candidate();
				value.*field = std::bit_cast<float>(operand);
				ASSERT_EQ(std::bit_cast<uint32_t>(*(value.*field)), operand);
				ASSERT_NO_FATAL_FAILURE(compare(value, std::bit_cast<float>(baseline)));
			}
	ASSERT_NO_FATAL_FAILURE(finishControls());
}

TEST_F(HeroExpectedComponentTest, TargetForecastRejectsMalformedTargetsBeforeSpellAndAllowanceAccess)
{
	ASSERT_NO_FATAL_FAILURE(beginControls());
	for(bool blink : {false, true})
	{
		for(const auto & target : std::vector<spells::Target>{{}, {spells::Destination()},
			{spells::Destination(enemy), spells::Destination(enemy)}})
		{
			auto value = candidate();
			value.dest = target;
			value.spellPlacementHeuristicValue = std::bit_cast<float>(uint32_t{0x7f800001});
			ASSERT_NO_FATAL_FAILURE(compareTarget(value, blink, 0.0f));
		}
	}
	ASSERT_NO_FATAL_FAILURE(finishControls());
}

TEST_F(HeroExpectedComponentTest, TargetForecastNegationAndUnbegunActionRejectBeforeValuation)
{
	ASSERT_NO_FATAL_FAILURE(beginControls());
	for(bool blink : {false, true})
	{
		auto value = candidate();
		value.spellPlacementHeuristicValue = std::bit_cast<float>(uint32_t{0x7f800001});
		ASSERT_NO_FATAL_FAILURE(compareTarget(value, blink, 0.0f, true, true));
		ASSERT_NO_FATAL_FAILURE(compareTarget(value, blink, 0.0f, false));
	}
	ASSERT_NO_FATAL_FAILURE(finishControls());
}

TEST_F(HeroExpectedComponentTest, TargetForecastUsesLateBoundOperandAndOriginalAcceptedTransition)
{
	ASSERT_NO_FATAL_FAILURE(beginControls());
	for(bool blink : {false, true})
	{
		auto value = candidate();
		value.spellPlacementHeuristicValue = 1.0f;
		ASSERT_NO_FATAL_FAILURE(compareTarget(value, blink, 31.0f, true, false, true));
		value.spellPlacementHeuristicValue = std::numeric_limits<float>::max();
		ASSERT_NO_FATAL_FAILURE(compareTarget(value, blink, std::numeric_limits<float>::max()));
	}
	ASSERT_NO_FATAL_FAILURE(finishControls());
}
}
