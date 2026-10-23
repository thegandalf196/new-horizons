/*
 * LifeDrainCandidateValueReuseTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../AI/BattleAI/LifeDrainCandidateValueReuse.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../lib/spells/CSpell.h"
#include "../mock/mock_battle_Unit.h"
#include <bit>
#include <functional>
#include <limits>

class LifeDrainCandidateValueReuseTest : public ::testing::Test
{
protected:
	::testing::NiceMock<UnitMock> enemy, friendA, friendB;
	PossibleSpellcast cast;
	heroActionProjection::Admission admission;
	newHorizonsLifeDrain::ProjectionReceipt receipt{};
	void SetUp() override
	{
		ON_CALL(enemy, unitId()).WillByDefault(::testing::Return(10u));
		ON_CALL(friendA, unitId()).WillByDefault(::testing::Return(20u));
		ON_CALL(friendB, unitId()).WillByDefault(::testing::Return(21u));
		ON_CALL(enemy, getPosition()).WillByDefault(::testing::Return(BattleHex(12, 5)));
		ON_CALL(friendA, getPosition()).WillByDefault(::testing::Return(BattleHex(3, 3)));
		ON_CALL(friendB, getPosition()).WillByDefault(::testing::Return(BattleHex(4, 7)));
		cast.spell = SpellID(SpellID::decode(std::string(newHorizonsLifeDrain::SPELL_KEY))).toSpell();
		ASSERT_NE(cast.spell, nullptr);
		cast.dest = {spells::Destination(&enemy), spells::Destination(&friendA)};
		cast.value = -0.0f;
		admission.admitted = true;
		admission.spellAllowance.emplace();
		admission.spellAllowance->spell = cast.spell->getId();
		admission.counterspell.resolutionKnown = true;
		admission.counterspell.negated = false;
		// Container-only synthetic proof. Integration tests use actual casts.
		receipt.primaryTarget = 10;
		receipt.primaryPosition = BattleHex(12, 5);
		receipt.effectName = "lifeDrain";
	}
};

TEST_F(LifeDrainCandidateValueReuseTest, OnlyCertifiedSecondaryDiffersWithoutReplacingOriginalAim)
{
	LifeDrainCandidateValueReuse::TestControl control;
	LifeDrainCandidateValueReuse values(&control);
	values.remember(cast, admission, {}, receipt);
	cast.dest[1] = spells::Destination(&friendB);
	const auto hit = values.find(cast, admission, {}, receipt);
	ASSERT_TRUE(hit);
	EXPECT_EQ(std::bit_cast<uint32_t>(*hit), std::bit_cast<uint32_t>(-0.0f));
	EXPECT_EQ(cast.dest[1].unitValue, &friendB);
	EXPECT_EQ(control.hits, 1u);
	EXPECT_EQ(control.completedReceipts, 1u);
	LifeDrainCandidateValueReuse nextInvocation;
	EXPECT_FALSE(nextInvocation.find(cast, admission, {}, receipt));
}

TEST_F(LifeDrainCandidateValueReuseTest, EnemyEffectFullOptionsAllowanceAndCounterspellMustMatch)
{
	LifeDrainCandidateValueReuse values;
	values.remember(cast, admission, {}, receipt);
	auto changedReceipt = receipt;
	++changedReceipt.effectValue;
	EXPECT_FALSE(values.find(cast, admission, {}, changedReceipt));
	changedReceipt = receipt;
	++changedReceipt.primaryTarget;
	EXPECT_FALSE(values.find(cast, admission, {}, changedReceipt));
	values.remember(cast, admission, {}, receipt);
	for(const auto & change : std::vector<std::function<void(PossibleSpellcast &)>>{
		[](auto & c) { c.metamagicGrand = true; }, [](auto & c) { ++c.spellOvercharge; },
		[](auto & c) { c.spellSelectiveDispel = true; },
		[](auto & c) { c.spellHandOfFateExpectedValue = 0.0f; },
		[](auto & c) { c.spellNaturePoisonValue = -0.0f; },
		[](auto & c) { c.commandTargets.push_back(20); }})
	{
		auto changed = cast;
		change(changed);
		EXPECT_FALSE(values.find(changed, admission, {}, receipt));
	}
	auto changedAdmission = admission;
	++changedAdmission.spellAllowance->action.epoch;
	EXPECT_FALSE(values.find(cast, changedAdmission, {}, receipt));
	changedAdmission = admission;
	changedAdmission.counterspell.manaCost = 0;
	ASSERT_NE(changedAdmission.counterspell.manaCost, admission.counterspell.manaCost);
	EXPECT_FALSE(values.find(cast, changedAdmission, {}, receipt));
}

TEST_F(LifeDrainCandidateValueReuseTest, MissingProofUnsupportedWorkOrContextClearsPriorValue)
{
	LifeDrainCandidateValueReuse values;
	values.remember(cast, admission, {}, receipt);
	EXPECT_FALSE(values.find(cast, admission, {}, std::nullopt));
	EXPECT_FALSE(values.find(cast, admission, {}, receipt));
	values.remember(cast, admission, {}, receipt);
	EXPECT_FALSE(values.find(cast, admission, {20}, receipt));
	EXPECT_FALSE(values.find(cast, admission, {}, receipt));
	values.remember(cast, admission, {}, receipt);
	cast.command = HeroCommand::CHARGE;
	values.beginCandidate(cast);
	cast.command = HeroCommand::NONE;
	EXPECT_FALSE(values.find(cast, admission, {}, receipt));
	admission.admitted = false;
	values.remember(cast, admission, {}, receipt);
	EXPECT_FALSE(values.find(cast, admission, {}, receipt));
	admission.admitted = true;
	admission.counterspellNegated = true;
	values.remember(cast, admission, {}, receipt);
	EXPECT_FALSE(values.find(cast, admission, {}, receipt));
}

TEST_F(LifeDrainCandidateValueReuseTest, DisabledAndNonfinalValuesCannotBecomeEntries)
{
	LifeDrainCandidateValueReuse values;
	for(const float invalid : {std::numeric_limits<float>::lowest(),
		static_cast<float>(EvaluationResult::INEFFECTIVE_SCORE),
		std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		cast.value = invalid;
		values.remember(cast, admission, {}, receipt);
		EXPECT_FALSE(values.find(cast, admission, {}, receipt));
	}
	cast.value = 1.25f;
	LifeDrainCandidateValueReuse::TestControl control{false};
	LifeDrainCandidateValueReuse disabled(&control);
	disabled.remember(cast, admission, {}, receipt);
	EXPECT_FALSE(disabled.find(cast, admission, {}, receipt));
	EXPECT_EQ(control.hits, 0u);
}
