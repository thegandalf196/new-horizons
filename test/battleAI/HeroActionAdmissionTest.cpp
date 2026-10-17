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
		result.spellAllowance = state.prepareHeroSpellAllowance(side, ps.spell->getId(),
			ps.metamagicFollowup, ps.metamagicGrand);
		if(!result.spellAllowance)
			return result;
		if(!state.beginProjectedHeroAction(side, *result.spellAllowance))
			return result;
		result.counterspell = state.resolveProjectedCounterspell(side, ps.spell);
		if(!result.counterspell.resolutionKnown || !result.counterspell.negated.has_value())
			return result;
		result.counterspellNegated = *result.counterspell.negated;
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
		EXPECT_EQ(result.counterspell.wardSide, oldResult.counterspell.wardSide);
		EXPECT_EQ(result.counterspell.wardActive, oldResult.counterspell.wardActive);
		EXPECT_EQ(result.counterspell.resolutionKnown, oldResult.counterspell.resolutionKnown);
		EXPECT_EQ(result.counterspell.negated, oldResult.counterspell.negated);
		EXPECT_EQ(result.counterspell.manaCost, oldResult.counterspell.manaCost);
		EXPECT_EQ(result.counterspellNegated, oldResult.counterspellNegated);
		if(!order && !blocked && !exhausted)
		{
			EXPECT_EQ(result.counterspell.resolutionKnown, expectedAdmission);
			EXPECT_EQ(result.counterspell.wardActive,
				battle()->getSide(BattleSide::DEFENDER).counterspellArmed);
			if(expectedAdmission && result.counterspell.wardActive)
				EXPECT_EQ(result.counterspellNegated, defenderSideHero->getManaAvailable() > 0);
		}
		EXPECT_EQ(candidate.value, 123.0f); // Scoring remains exclusively in the caller.
		EXPECT_EQ(state(*after), state(*before));
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			EXPECT_EQ(after->getCounterspellArmed(side), before->getCounterspellArmed(side));
		if(!blocked && !exhausted)
			EXPECT_FALSE(after->getCounterspellArmed(BattleSide::ATTACKER));
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

TEST_F(HeroActionAdmissionTest, SpellBeginsBeforeWardResolutionWithoutScoring)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(compare(false, true));
}

TEST_F(HeroActionAdmissionTest, OrderBeginsWithoutDereferencingSpellOrResolvingWard)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	battle()->getSide(BattleSide::DEFENDER).counterspellArmed = true;
	ASSERT_NO_FATAL_FAILURE(compare(true, true));
}

TEST_F(HeroActionAdmissionTest, AlreadyBegunActionRefusesSpellBeforeWardResolution)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	battle()->getSide(BattleSide::DEFENDER).counterspellArmed = true;
	ASSERT_NO_FATAL_FAILURE(compare(false, false, true));
}

TEST_F(HeroActionAdmissionTest, BeginningSpellAndOrderExpiresOwnArmedWard)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	battle()->getSide(BattleSide::ATTACKER).counterspellArmed = true;
	ASSERT_NO_FATAL_FAILURE(compare(false, true));
	ASSERT_NO_FATAL_FAILURE(compare(true, true));
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

TEST_F(HeroActionAdmissionTest, ArmedWardWithSufficientManaRetainsKnownNegation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	battle()->getSide(BattleSide::DEFENDER).counterspellArmed = true;
	setTestSpellPointTotal(defenderSideHero, 100000);
	ASSERT_NO_FATAL_FAILURE(compare(false, true));
}

TEST_F(HeroActionAdmissionTest, ArmedWardWithoutManaRetainsKnownFailure)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	battle()->getSide(BattleSide::DEFENDER).counterspellArmed = true;
	setTestSpellPointTotal(defenderSideHero, 0);
	ASSERT_NO_FATAL_FAILURE(compare(false, true));
}

TEST_F(HeroActionAdmissionTest, MissingWardingHeroRefusesUnknownResultAfterBeginning)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	// Exercise the real missing-information branch, not a stubbed counterspell.
	// This deliberately inconsistent ward snapshot is not proof that ordinary
	// player visibility hides either hero in the current runtime implementation.
	battle()->getSide(BattleSide::DEFENDER).counterspellArmed = true;
	battle()->getSide(BattleSide::DEFENDER).heroID = ObjectInstanceID::NONE;
	ASSERT_NO_FATAL_FAILURE(compare(false, false));
}
}
