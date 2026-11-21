/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "MetamagicDoubleCastFixture.h"

using namespace metamagicDoubleCastTest;

TEST_F(MetamagicDoubleCastTest, OfferOpensOnlyAfterAHeroActionSpellWithUsesRemaining)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	ASSERT_NE(nativeRules::metamagicRules(attackerSideHero)->doubleCastPolicy(), nullptr);
	EXPECT_FALSE(offered());
	const auto serial = battle()->getActivationSerial();
	ASSERT_TRUE(cast(SpellID::BLESS));
	ASSERT_TRUE(offered());
	EXPECT_TRUE(state().offerOpen());
	EXPECT_EQ(state().firstSpell, SpellID(SpellID::BLESS));
	EXPECT_EQ(state().offerRound, battle()->getRound());
	EXPECT_EQ(state().offerActivationSerial, serial);
	EXPECT_EQ(state().usesSpent, 0);
	const auto selected = battle()->battleGetSpellActionAllowance(ATTACKER, SpellID::CURSE);
	ASSERT_TRUE(selected);
	EXPECT_EQ(selected->source, Ledger::GrantSource::METAMAGIC_DOUBLE_CAST);
	EXPECT_EQ(selected->allowance, Ledger::AllowanceKind::SPELL);
	EXPECT_EQ(selected->expiryRound, battle()->getRound());
}

TEST_F(MetamagicDoubleCastTest, NoOfferWithoutMetamagicRank)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0));
	ASSERT_TRUE(cast(SpellID::BLESS));
	EXPECT_FALSE(offered());
	EXPECT_TRUE(state().defaultState());
	EXPECT_FALSE(cast(SpellID::CURSE));
}

TEST_F(MetamagicDoubleCastTest, FollowUpIsAcceptedCountedAndOffersNoThirdSpell)
{
	ASSERT_NO_FATAL_FAILURE(prepare(3));
	const auto mana = attackerSideHero->getManaAvailable();
	const auto curseCost = battle()->battleGetSpellCost(SpellID(SpellID::CURSE).toSpell(), attackerSideHero);
	ASSERT_TRUE(cast(SpellID::BLESS));
	const auto afterFirst = attackerSideHero->getManaAvailable();
	EXPECT_LT(afterFirst, mana);
	ASSERT_TRUE(cast(SpellID::CURSE));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), afterFirst - curseCost);
	EXPECT_FALSE(offered());
	EXPECT_FALSE(state().offerOpen());
	EXPECT_EQ(state().usesSpent, 1);
	EXPECT_EQ(state().completedDoubleCasts, 1);
	EXPECT_EQ(metamagicDoubleCast::remainingUses(attackerSideHero, state()), 2);
	EXPECT_FALSE(cast(SpellID::SLOW));
	EXPECT_EQ(state().usesSpent, 1);
}

TEST_F(MetamagicDoubleCastTest, SameSpellFollowUpIsRejectedAndKeepsTheOffer)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	ASSERT_TRUE(cast(SpellID::BLESS));
	const auto before = state();
	const auto allowances = ledger();
	const auto mana = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(cast(SpellID::BLESS));
	EXPECT_EQ(state(), before);
	EXPECT_EQ(ledger(), allowances);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
}

TEST_F(MetamagicDoubleCastTest, RepeatPredicateMatchesTheAuthorityRefusal)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	EXPECT_FALSE(metamagicDoubleCast::followUpRepeatsFirstSpell(*battle(), ATTACKER, SpellID::BLESS)) << "No offer yet";
	ASSERT_TRUE(cast(SpellID::BLESS));
	ASSERT_TRUE(offered());
	BattleAction request;
	request.actionType = EActionType::HERO_SPELL;
	request.side = ATTACKER;
	for(const auto spell : {SpellID::BLESS, SpellID::CURSE, SpellID::SLOW})
	{
		request.spell = SpellID(spell);
		const bool repeats = metamagicDoubleCast::followUpRepeatsFirstSpell(*battle(), ATTACKER, SpellID(spell));
		EXPECT_EQ(repeats, spell == SpellID::BLESS) << SpellID(spell).toSpell()->getJsonKey();
		// The frontends' predicate and the authority's admission agree on every spell.
		EXPECT_EQ(repeats, metamagicDoubleCast::actionRefusal(*battle(), request).has_value());
	}
	EXPECT_FALSE(metamagicDoubleCast::followUpRepeatsFirstSpell(*battle(), BattleSide::DEFENDER, SpellID::BLESS));
	EXPECT_FALSE(metamagicDoubleCast::followUpRepeatsFirstSpell(*battle(), BattleSide::NONE, SpellID::BLESS));
	ASSERT_TRUE(cast(SpellID::CURSE));
	EXPECT_FALSE(metamagicDoubleCast::followUpRepeatsFirstSpell(*battle(), ATTACKER, SpellID::BLESS)) << "Offer consumed";
}

TEST_F(MetamagicDoubleCastTest, RepeatPredicateNeedsAnOpenOffer)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0));
	ASSERT_TRUE(cast(SpellID::BLESS));
	ASSERT_FALSE(offered());
	EXPECT_FALSE(metamagicDoubleCast::followUpRepeatsFirstSpell(*battle(), ATTACKER, SpellID::BLESS));
}

TEST_F(MetamagicDoubleCastTest, OrderCannotBePaidFromTheFollowUpGrant)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	ASSERT_TRUE(cast(SpellID::BLESS));
	ASSERT_TRUE(offered());
	EXPECT_FALSE(battle()->battleGetOrderActionAllowance(ATTACKER));
	EXPECT_FALSE(issue(HeroCommand::CHARGE));
	EXPECT_TRUE(offered());
	EXPECT_EQ(state().usesSpent, 0);
}

TEST_F(MetamagicDoubleCastTest, AnyOtherActionForfeitsWithoutSpendingAUse)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	ASSERT_TRUE(cast(SpellID::BLESS));
	ASSERT_TRUE(offered());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeDefend(battle()->battleActiveUnit())));
	EXPECT_FALSE(offered());
	EXPECT_FALSE(state().offerOpen());
	EXPECT_EQ(state().usesSpent, 0);
	EXPECT_EQ(metamagicDoubleCast::remainingUses(attackerSideHero, state()), 1);
}

TEST_F(MetamagicDoubleCastTest, RoundEndExpiresTheOfferWithoutSpendingAUse)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	ASSERT_TRUE(cast(SpellID::BLESS));
	ASSERT_TRUE(offered());
	advanceRound();
	EXPECT_FALSE(offered());
	EXPECT_FALSE(state().offerOpen());
	EXPECT_EQ(state().usesSpent, 0);
}

TEST_F(MetamagicDoubleCastTest, NewActivationExpiresTheOffer)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	ASSERT_TRUE(cast(SpellID::BLESS));
	ASSERT_TRUE(offered());
	battle()->nextTurn(battle()->battleActiveUnit()->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_FALSE(offered());
	EXPECT_FALSE(state().offerOpen());
}

TEST_F(MetamagicDoubleCastTest, UsesFollowRankHalonAndUsePerks)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	for(int rank = 1; rank <= 3; ++rank)
	{
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:metamagic")),
			rank, ChangeValueMode::ABSOLUTE);
		EXPECT_EQ(metamagicDoubleCast::maximumUses(attackerSideHero), rank);
	}
	learn({"deepReserves"});
	EXPECT_EQ(metamagicDoubleCast::maximumUses(attackerSideHero), 4);
	learn({"arcaneCycle", "grandWeaving"});
	EXPECT_EQ(metamagicDoubleCast::maximumUses(attackerSideHero), 6);
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:halon")));
	EXPECT_EQ(metamagicDoubleCast::maximumUses(attackerSideHero), 7);
}

TEST_F(MetamagicDoubleCastTest, HalonGainsOneUse)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1, "core:halon"));
	EXPECT_EQ(metamagicDoubleCast::maximumUses(attackerSideHero), 2);
}

TEST_F(MetamagicDoubleCastTest, ArcaneCycleRegainsOneSpentUseOnEveryThirdRound)
{
	ASSERT_NO_FATAL_FAILURE(prepare(2));
	learn({"deepReserves", "arcaneCycle"});
	ASSERT_TRUE(cast(SpellID::BLESS));
	ASSERT_TRUE(cast(SpellID::CURSE));
	ASSERT_EQ(state().usesSpent, 1);
	while(battle()->getRound() < 2)
		advanceRound();
	EXPECT_EQ(state().usesRegained, 0);
	advanceRound();
	ASSERT_EQ(battle()->getRound(), 3);
	EXPECT_EQ(state().usesRegained, 1);
	EXPECT_EQ(metamagicDoubleCast::remainingUses(attackerSideHero, state()), 3);
	while(battle()->getRound() < 6)
		advanceRound();
	EXPECT_EQ(state().usesRegained, 1) << "Never regains above the combat maximum";
}

TEST_F(MetamagicDoubleCastTest, ArcaneCycleNeedsThePerk)
{
	ASSERT_NO_FATAL_FAILURE(prepare(2));
	ASSERT_TRUE(cast(SpellID::BLESS));
	ASSERT_TRUE(cast(SpellID::CURSE));
	while(battle()->getRound() < 3)
		advanceRound();
	EXPECT_EQ(state().usesRegained, 0);
}

TEST_F(MetamagicDoubleCastTest, ExhaustedUsesOpenNoOffer)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	ASSERT_TRUE(cast(SpellID::BLESS));
	ASSERT_TRUE(cast(SpellID::CURSE));
	advanceRound();
	ASSERT_TRUE(cast(SpellID::BLESS));
	EXPECT_FALSE(offered());
	EXPECT_EQ(metamagicDoubleCast::remainingUses(attackerSideHero, state()), 0);
}

TEST_F(MetamagicDoubleCastTest, PolicySixWorldsNeverOpenAnOffer)
{
	pinOverkill = true;
	ASSERT_NO_FATAL_FAILURE(prepare(3));
	ASSERT_EQ(nativeRules::metamagicRules(attackerSideHero)->doubleCastPolicy(), nullptr);
	ASSERT_TRUE(cast(SpellID::BLESS));
	EXPECT_FALSE(offered());
	EXPECT_TRUE(state().defaultState());
	EXPECT_EQ(metamagicDoubleCast::maximumUses(attackerSideHero), 0);
}

