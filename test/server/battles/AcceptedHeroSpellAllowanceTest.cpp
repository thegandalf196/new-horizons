/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/gameState/AcceptedHeroSpellAllowance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include <stdexcept>

namespace
{
class AcceptedHeroSpellAllowanceTest : public HeroCommandFixture
{
protected:

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons";
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
		EXPECT_EQ(side.passiveDivineMandateUses, before.passiveDivineMandateUses);
		EXPECT_EQ(side.castSpellsCount, before.castSpellsCount);
		EXPECT_EQ(rng(), randomBefore);
	}
};
}

TEST_F(AcceptedHeroSpellAllowanceTest, OrdinaryStageCommitsOnlyAllowanceBeforeCallerEffects)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:metamagic")),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(attackerSideHero->getPerkSkillRank("new-horizons:metamagic"), 1);
	auto & side = battle()->getSide(BattleSide::ATTACKER);
	const auto before = side;
	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto randomBefore = rng();
	const auto result = prepareAndCommitAcceptedHeroSpellAllowance(*battle(), attackerSideHero, packet());
	ASSERT_TRUE(result.transition);
	EXPECT_FALSE(result.automaticHeroCast);
	EXPECT_TRUE(result.sharedActionBudget);
	EXPECT_TRUE(result.spendsHeroAllowance);
	EXPECT_EQ(result.transition->allowance, HeroActionAllowanceState::AllowanceKind::HERO);
	EXPECT_NE(side.heroActionAllowances, before.heroActionAllowances);
	EXPECT_EQ(side.castSpellsCount, before.castSpellsCount);
	EXPECT_EQ(side.heroCommandUsed, before.heroCommandUsed);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(rng(), randomBefore);
	// Metamagic 6: the Hero Action spell opens a Double Cast offer for a different spell.
	const auto followUp = side.heroActionAllowances.eligibleAllowance(
		HeroActionAllowanceState::ActionKind::SPELL, battle()->getRound());
	ASSERT_TRUE(followUp);
	EXPECT_EQ(followUp->source, HeroActionAllowanceState::GrantSource::METAMAGIC_DOUBLE_CAST);
	EXPECT_EQ(side.metamagicDoubleCast.firstSpell, SpellID(SpellID::HASTE));
	expectRejected(packet(), "Double Cast follow-up repeats the first spell");
}

TEST_F(AcceptedHeroSpellAllowanceTest, AutomaticProvenanceRejectsBeforeCapacityOrAllowanceMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	auto cast = packet();
	cast.automaticHeroCast = {spells::AutomaticHeroCastKind::DIVINE_FURY, 0};
	expectRejected(cast, "Invalid or stale automatic hero spell receipt");
}


TEST_F(AcceptedHeroSpellAllowanceTest, StaleDivineMandateReceiptCannotSpendPerCombatBudget)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	auto cast = packet();
	cast.automaticHeroCast = {spells::AutomaticHeroCastKind::DIVINE_MANDATE, 1};
	expectRejected(cast, "Invalid or stale automatic hero spell receipt");
}
