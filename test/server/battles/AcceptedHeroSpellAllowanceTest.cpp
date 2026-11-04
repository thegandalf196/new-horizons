/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../HistoricalFactionPerkRules.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/gameState/AcceptedHeroSpellAllowance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/NewHorizonsMetamagicMarks.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include <stdexcept>

namespace
{
class AcceptedHeroSpellAllowanceTest : public HeroCommandFixture
{
protected:
	// Exact in-memory captured policy; no old-format reader or migration fixture.
	bool useCapturedLegacyPolicy = false;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		if(useCapturedLegacyPolicy)
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, historicalFactionPerkRules());
	}

	BattleSpellCast packet() const
	{
		BattleSpellCast result;
		result.battleID = battle()->getBattleID();
		result.side = BattleSide::ATTACKER;
		result.spellID = SpellID::HASTE;
		return result;
	}

	std::vector<std::byte> rng() const
	{
		CMemorySerializer bytes;
		bytes.oser & *gameHandler->randomizer;
		return bytes.extractBuffer();
	}

	void expectRejected(const BattleSpellCast & pack, const char * message)
	{
		const auto & side = battle()->getSide(pack.side);
		const auto before = side;
		const auto randomBefore = rng();
		try
		{
			prepareAndCommitAcceptedHeroSpellAllowance(*battle(), attackerSideHero, pack);
			FAIL() << "Forged allowance metadata was accepted";
		}
		catch(const std::runtime_error & error)
		{
			EXPECT_STREQ(error.what(), message);
		}
		EXPECT_EQ(side.heroActionAllowances, before.heroActionAllowances);
		EXPECT_EQ(side.metamagicUsesConsumed, before.metamagicUsesConsumed);
		EXPECT_EQ(side.metamagicPendingCount, before.metamagicPendingCount);
		EXPECT_EQ(side.metamagicGrandUsed, before.metamagicGrandUsed);
		EXPECT_EQ(side.passiveDivineMandateUses, before.passiveDivineMandateUses);
		EXPECT_EQ(side.castSpellsCount, before.castSpellsCount);
		EXPECT_EQ(side.metamagicSequenceSpells, before.metamagicSequenceSpells);
		EXPECT_EQ(side.metamagicPointState, before.metamagicPointState);
		EXPECT_EQ(rng(), randomBefore);
	}
};
}

TEST_F(AcceptedHeroSpellAllowanceTest, OrdinaryStageCommitsOnlyAllowanceBeforeCallerEffects)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	ASSERT_TRUE(newHorizonsMetamagic::usesPoints(attackerSideHero->getPerkState().rules));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:metamagic")),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(attackerSideHero->getPerkSkillRank("new-horizons:metamagic"), 1);
	EXPECT_EQ(newHorizonsMagic::metamagicRank(attackerSideHero), 0);
	EXPECT_EQ(newHorizonsMagic::metamagicCapacity(attackerSideHero), 0);
	auto & side = battle()->getSide(BattleSide::ATTACKER);
	side.counterspellArmed = true;
	side.metamagicCountersequenceArmed = true;
	const auto before = side;
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto randomBefore = rng();
	const auto result = prepareAndCommitAcceptedHeroSpellAllowance(*battle(), attackerSideHero, packet());
	ASSERT_TRUE(result.transition);
	EXPECT_FALSE(result.automaticHeroCast);
	EXPECT_TRUE(result.sharedActionBudget);
	EXPECT_TRUE(result.spendsHeroAllowance);
	EXPECT_EQ(result.transition->receipt.allowance, HeroActionAllowanceState::AllowanceKind::HERO);
	EXPECT_NE(side.heroActionAllowances, before.heroActionAllowances);
	EXPECT_EQ(side.castSpellsCount, before.castSpellsCount);
	EXPECT_EQ(side.heroCommandUsed, before.heroCommandUsed);
	EXPECT_EQ(side.metamagicSequenceSpells, before.metamagicSequenceSpells);
	EXPECT_TRUE(side.counterspellArmed);
	EXPECT_TRUE(side.metamagicCountersequenceArmed);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(rng(), randomBefore);
	EXPECT_EQ(side.metamagicPendingCount, 0);
	EXPECT_EQ(side.metamagicUsesConsumed, 0);
	EXPECT_EQ(side.metamagicPointState, before.metamagicPointState);
	EXPECT_FALSE(side.heroActionAllowances.eligibleAllowance(
		HeroActionAllowanceState::ActionKind::SPELL, battle()->getRound()));
	expectRejected(packet(), "Accepted Hero spell has no eligible action allowance");
}

TEST_F(AcceptedHeroSpellAllowanceTest, PointPolicyRejectsLegacyMetadataBeforeAutomaticProvenance)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	ASSERT_TRUE(newHorizonsMetamagic::usesPoints(attackerSideHero->getPerkState().rules));
	auto cast = packet();
	cast.metamagicFollowup = true;
	cast.automaticHeroCast = {spells::AutomaticHeroCastKind::DIVINE_FURY, 0};
	expectRejected(cast, "Passive Metamagic cannot spend a legacy Metamagic Spell Action");
}

TEST_F(AcceptedHeroSpellAllowanceTest, AutomaticProvenanceRejectsBeforeCapacityOrAllowanceMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	battle()->getSide(BattleSide::ATTACKER).metamagicUsesConsumed = 1;
	auto cast = packet();
	cast.automaticHeroCast = {spells::AutomaticHeroCastKind::DIVINE_FURY, 0};
	expectRejected(cast, "Invalid or stale automatic hero spell receipt");
}

TEST_F(AcceptedHeroSpellAllowanceTest, WrongFollowupCannotCommitLocalHeroAllowancePlan)
{
	useCapturedLegacyPolicy = true;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	auto cast = packet();
	cast.metamagicFollowup = true;
	expectRejected(cast, "Accepted Hero spell has forged or inconsistent allowance metadata");
}

TEST_F(AcceptedHeroSpellAllowanceTest, GrandShapeRejectsBeforeRefundAndLocalPlanning)
{
	useCapturedLegacyPolicy = true;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	auto cast = packet();
	cast.metamagicGrand = true;
	cast.metamagicManaRefund = 1;
	expectRejected(cast, "Grand Metamagic metadata requires a follow-up cast");
}

TEST_F(AcceptedHeroSpellAllowanceTest, RefundRejectsBeforeLocalAllowancePlanning)
{
	useCapturedLegacyPolicy = true;
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	auto cast = packet();
	cast.metamagicManaRefund = 1;
	expectRejected(cast, "Invalid Formula Reserve Metamagic refund");
}
