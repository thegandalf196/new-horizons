/*
 * HeroExpectedSpellSelectorTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroExpectedSpellFixture.h"

namespace heroExpectedTest
{
TEST_F(HeroExpectedSpellTest, HandOfFateRealSelectorCompetesWithCanonicalOrders)
{
	ASSERT_NO_FATAL_FAILURE(prepareExpected("new-horizons:handOfFate"));
	ASSERT_NO_FATAL_FAILURE(expectSelectedSpell());
}

TEST_F(HeroExpectedSpellTest, BattleFormRealSelectorCompetesWithCanonicalOrders)
{
	ASSERT_NO_FATAL_FAILURE(prepareExpected("new-horizons:polymorph"));
	ASSERT_NO_FATAL_FAILURE(expectSelectedSpell());
}

TEST_F(HeroExpectedSpellTest, ConfusionRealSelectorCompetesWithCanonicalOrders)
{
	ASSERT_NO_FATAL_FAILURE(prepareExpected("new-horizons:confusion"));
	ASSERT_NO_FATAL_FAILURE(expectSelectedSpell());
}

TEST_F(HeroExpectedSpellTest, PuppetMasterRealSelectorCompetesWithCanonicalOrders)
{
	ASSERT_NO_FATAL_FAILURE(prepareExpected("new-horizons:puppetMaster"));
	ASSERT_NO_FATAL_FAILURE(expectSelectedSpell());
}
}
