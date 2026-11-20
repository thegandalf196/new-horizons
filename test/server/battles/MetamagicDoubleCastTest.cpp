/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../OverkillRandomPrayerRules.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/battle/MetamagicDoubleCast.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/nativeRules/CompiledMetamagicRules.h"
#include "../../../lib/spells/CSpell.h"

namespace
{
using Ledger = HeroActionAllowanceState;
constexpr auto ATTACKER = BattleSide::ATTACKER;

class MetamagicDoubleCastTest : public HeroCommandFixture
{
protected:
	bool pinOverkill = false;

	void SetUp() override
	{
		useRevisedOrders = true;
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons";
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		if(pinOverkill)
		{
			map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, overkillRandomPrayerRules());
			map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, overkillRandomPrayerHeroes());
		}
	}

	void prepare(int rank, const char * heroType = nullptr)
	{
		startGame();
		if(heroType)
			attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode(heroType)));
		if(rank > 0)
			attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:metamagic")),
				rank, ChangeValueMode::ABSOLUTE);
		if(!attackerSideHero->hasSpellbook())
			giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		for(const auto spell : {SpellID::BLESS, SpellID::CURSE, SpellID::SLOW})
			attackerSideHero->addSpellToSpellbook(spell);
		setTestSpellPointTotal(attackerSideHero, 200);
		startBattle();
		beginCombat();
		ASSERT_EQ(battle()->battleGetOwner(battle()->battleActiveUnit()), PlayerColor(0));
	}

	void learn(std::initializer_list<const char *> perks)
	{
		for(const auto * name : perks)
		{
			const auto id = std::string("new-horizons:metamagic.") + name;
			attackerSideHero->applyPerkSelection({"new-horizons:metamagic", id});
			ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:metamagic", id)) << name;
		}
	}

	bool cast(SpellID spell)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = ATTACKER;
		action.spell = spell;
		const battle::Unit * target = battle()->battleActiveUnit();
		if(spell == SpellID(SpellID::CURSE) || spell == SpellID(SpellID::SLOW))
			for(const auto * unit : battle()->battleGetAllStacks(false))
				if(unit->unitSide() == BattleSide::DEFENDER && unit->alive())
					target = unit;
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	const MetamagicDoubleCastState & state() const { return battle()->getSide(ATTACKER).metamagicDoubleCast; }
	const Ledger & ledger() const { return battle()->getHeroActionAllowances(ATTACKER); }
	bool offered() const { return metamagicDoubleCast::offerGranted(ledger()); }
};
}

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

