/*
 * HeroActionAdmissionTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroExpectedSpellFixture.h"
#include "../../AI/BattleAI/HeroActionAdmission.h"
#include "../../AI/BattleAI/PossibleSpellcast.h"
#include "../../lib/spells/CSpell.h"

namespace heroExpectedTest
{
namespace
{
// Independent preserved BEFORE branch oracle. This is component parity, not a
// selector, authoritative validation, or gameplay-quality measurement.
heroActionProjection::Admission originalAdmission(HypotheticBattle & state,
	BattleSide side, const PossibleSpellcast & ps)
{
	heroActionProjection::Admission result;
	if(ps.command == HeroCommand::NONE)
	{
		result.spellAllowance = state.prepareHeroSpellAllowance(side, ps.spell->getId());
		if(!result.spellAllowance)
			return result;
		if(!state.beginProjectedHeroAction(side, *result.spellAllowance))
			return result;
	}
	else
	{
		result.orderAllowance = state.prepareHeroOrderAllowance(side);
		if(!result.orderAllowance)
			return result;
		if(!state.beginProjectedHeroAction(side, *result.orderAllowance))
			return result;
	}
	result.admitted = true;
	return result;
}
}

class HeroActionAdmissionTest : public HeroExpectedSpellTest
{
protected:
	void prepare()
	{
		ASSERT_NO_FATAL_FAILURE(prepareExpected("new-horizons:handOfFate"));
	}

	void compare(bool order, bool expectedAdmission, bool blocked = false, bool exhausted = false)
	{
		auto before = detached(), after = detached();
		const auto outside = observation();
		for(const auto & branch : {before, after})
		{
			if(blocked || exhausted)
			{
				const auto allowance = branch->prepareHeroOrderAllowance(BattleSide::ATTACKER);
				ASSERT_TRUE(allowance);
				ASSERT_TRUE(branch->beginProjectedHeroAction(BattleSide::ATTACKER, *allowance));
				if(exhausted)
					ASSERT_TRUE(branch->projectAcceptedHeroOrder(BattleSide::ATTACKER, *allowance));
			}
		}
		PossibleSpellcast candidate;
		candidate.command = order ? HeroCommand::CHARGE : HeroCommand::NONE;
		candidate.spell = order ? nullptr : selectedSpell.toSpell();
		candidate.value = 123.0f;
		const auto oldResult = originalAdmission(*before, BattleSide::ATTACKER, candidate);
		const auto result = heroActionProjection::admitCandidate(*after, BattleSide::ATTACKER, candidate);
		EXPECT_EQ(result.admitted, expectedAdmission);
		EXPECT_EQ(result.admitted, oldResult.admitted);
		EXPECT_EQ(result.spellAllowance, oldResult.spellAllowance);
		EXPECT_EQ(result.orderAllowance, oldResult.orderAllowance);
		EXPECT_EQ(candidate.value, 123.0f); // Scoring remains exclusively in the caller.
		EXPECT_EQ(state(*after), state(*before));
		// Observe the otherwise private begun-action latch through its real API.
		if(result.spellAllowance)
		{
			EXPECT_FALSE(after->beginProjectedHeroAction(BattleSide::ATTACKER, *result.spellAllowance));
			EXPECT_EQ(after->beginProjectedHeroAction(BattleSide::ATTACKER, *result.spellAllowance),
				before->beginProjectedHeroAction(BattleSide::ATTACKER, *oldResult.spellAllowance));
		}
		if(result.orderAllowance)
		{
			EXPECT_FALSE(after->beginProjectedHeroAction(BattleSide::ATTACKER, *result.orderAllowance));
			EXPECT_EQ(after->beginProjectedHeroAction(BattleSide::ATTACKER, *result.orderAllowance),
				before->beginProjectedHeroAction(BattleSide::ATTACKER, *oldResult.orderAllowance));
		}
		EXPECT_EQ(state(*after), state(*before));
		EXPECT_EQ(observation(), outside); // Live/parent/sibling, RNG, traffic and mana.
	}
};

TEST_F(HeroActionAdmissionTest, SpellBeginsWithoutScoring)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(compare(false, true));
}

TEST_F(HeroActionAdmissionTest, OrderBeginsWithoutDereferencingSpell)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(compare(true, true));
}

TEST_F(HeroActionAdmissionTest, AlreadyBegunActionRefusesSpell)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(compare(false, false, true));
}

TEST_F(HeroActionAdmissionTest, AlreadyBegunActionRefusesOrder)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(compare(true, false, true));
}

TEST_F(HeroActionAdmissionTest, ExhaustedAllowanceRefusesSpell)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(compare(false, false, false, true));
}

TEST_F(HeroActionAdmissionTest, ExhaustedAllowanceRefusesOrder)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(compare(true, false, false, true));
}

}
