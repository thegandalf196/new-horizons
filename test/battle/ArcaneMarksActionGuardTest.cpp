/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/battle/HeroActionAllowanceState.h"
#include <gtest/gtest.h>

namespace
{
void captureMarksPolicy(CGHeroInstance & hero)
{
	auto & state = const_cast<newHorizonsHeroes::PerkState &>(hero.getPerkState());
	state.rules["metamagicPolicy"]["version"].Integer() = 2;
	hero.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::METAMAGIC_USES_PER_COMBAT, BonusSource::OTHER, 100, BonusSourceID()));
}
}

TEST(ArcaneMarksActionGuard, RetainedLegacyBonusCarrierCannotBuyAdditionalActions)
{
	CGHeroInstance hero(nullptr);
	captureMarksPolicy(hero);
	EXPECT_EQ(hero.valOfBonuses(BonusType::METAMAGIC_USES_PER_COMBAT), 100);
	EXPECT_EQ(newHorizonsMagic::metamagicRank(&hero), 0);
	EXPECT_EQ(newHorizonsMagic::metamagicCapacity(&hero), 0);
}

TEST(ArcaneMarksActionGuard, PaidHeroSpellDoesNotCreateLegacySpellGrant)
{
	CGHeroInstance hero(nullptr);
	captureMarksPolicy(hero);
	HeroActionAllowanceState ledger;
	ledger.resetForRound(1);
	const auto selected = ledger.eligibleAllowance(HeroActionAllowanceState::ActionKind::SPELL, 1);
	ASSERT_TRUE(selected);
	uint8_t used = 0;
	uint8_t pending = 0;
	bool grand = false;
	const auto result = HeroSpellAllowanceTransition::commitAcceptedCast(ledger, selected->grantId,
		1, false, false, newHorizonsMagic::metamagicRank(&hero), false, used, pending, grand,
		0, newHorizonsMagic::metamagicCapacity(&hero));
	ASSERT_TRUE(result);
	EXPECT_TRUE(result->grantedGrantIds.empty());
	EXPECT_EQ(pending, 0);
	EXPECT_EQ(used, 0);
	EXPECT_FALSE(ledger.eligibleAllowance(HeroActionAllowanceState::ActionKind::SPELL, 1));
}

TEST(ArcaneMarksActionGuard, ForgedOldTypedFollowupFailsAtomically)
{
	CGHeroInstance hero(nullptr);
	captureMarksPolicy(hero);
	HeroActionAllowanceState ledger;
	ledger.resetForRound(1);
	const auto token = ledger.grantAllowance(HeroActionAllowanceState::AllowanceKind::SPELL,
		HeroActionAllowanceState::GrantSource::METAMAGIC, 1);
	const auto before = ledger;
	uint8_t used = 0;
	uint8_t pending = 1;
	bool grand = false;
	const auto result = HeroSpellAllowanceTransition::commitAcceptedCast(ledger, token,
		1, true, false, newHorizonsMagic::metamagicRank(&hero), false, used, pending, grand,
		1, newHorizonsMagic::metamagicCapacity(&hero));
	EXPECT_FALSE(result);
	EXPECT_EQ(ledger, before);
	EXPECT_EQ(pending, 1);
	EXPECT_EQ(used, 0);
}

TEST(ArcaneMarksActionGuard, AbsentPolicyPreservesHistoricalBonusOnlyCapacity)
{
	CGHeroInstance hero(nullptr);
	hero.addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::METAMAGIC_USES_PER_COMBAT, BonusSource::OTHER, 100, BonusSourceID()));
	EXPECT_EQ(newHorizonsMagic::metamagicRank(&hero), 3);
	EXPECT_EQ(newHorizonsMagic::metamagicCapacity(&hero), 3);
}
