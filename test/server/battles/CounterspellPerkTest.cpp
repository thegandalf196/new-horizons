/*
 * CounterspellPerkTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"

#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/battle/HeroActionAllowanceState.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

namespace
{
constexpr auto counterspellKey = "new-horizons:counterspell";
constexpr auto sorcerySkill = "new-horizons:sorceryMagic";
constexpr auto countermagePerk = "new-horizons:sorceryMagic.countermage";
constexpr auto metamagicSkill = "new-horizons:metamagic";

SpellID counterspell()
{
	return SpellID(SpellID::decode(counterspellKey));
}

std::shared_ptr<Bonus> testTimeStopMarker(BattleSide side)
{
	auto marker = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP,
		BonusSource::OTHER, 1, BonusSourceID());
	marker->parameters = std::make_shared<BonusParameters>(static_cast<int32_t>(side));
	return marker;
}
}

class CounterspellPerkTest : public HeroCommandFixture
{
protected:
	bool typedAllowanceFixture = false;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
		useCommands = typedAllowanceFixture;
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare(int counterMana, bool selectCountermage,
		bool attackerMetamagic = false, bool defenderMetamagic = false)
	{
		startGame();
		const auto decoded = SecondarySkill::decode(sorcerySkill);
		ASSERT_GE(decoded, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(decoded), 2, ChangeValueMode::ABSOLUTE);
		defenderSideHero->setSecSkillLevel(SecondarySkill(decoded), 1, ChangeValueMode::ABSOLUTE);
		if(attackerMetamagic || defenderMetamagic)
		{
			const auto metamagic = SecondarySkill::decode(metamagicSkill);
			ASSERT_GE(metamagic, 0);
			if(attackerMetamagic)
				attackerSideHero->setSecSkillLevel(SecondarySkill(metamagic), 1, ChangeValueMode::ABSOLUTE);
			if(defenderMetamagic)
				defenderSideHero->setSecSkillLevel(SecondarySkill(metamagic), 1, ChangeValueMode::ABSOLUTE);
		}
		if(selectCountermage)
			attackerSideHero->applyPerkSelection({sorcerySkill, countermagePerk});

		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(counterspell());
		attackerSideHero->addSpellToSpellbook(SpellID::HASTE);
		defenderSideHero->addSpellToSpellbook(SpellID::HASTE);
		setTestSpellPointTotal(attackerSideHero, counterMana);
		setTestSpellPointTotal(defenderSideHero, 100);

		startBattle();
		attacker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 10);
		defender = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 10);
		beginCombat();

		// Keep the fixture deterministic: remove the token stacks from the hero
		// garrisons and make our explicitly created attacker active.
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit != attacker && unit != defender)
				remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		activate(attacker);

		ASSERT_EQ(attackerSideHero->hasActivePerk(sorcerySkill, countermagePerk), selectCountermage);
	}

	void activate(const CStack * stack)
	{
		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.stack = stack->unitId();
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activate);
	}

	bool castCounterspell()
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = counterspell();
		action.aimToHex(BattleHex::INVALID);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	bool castEnemyHaste(bool followup = false)
	{
		activate(defender);
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::DEFENDER;
		action.spell = SpellID::HASTE;
		action.metamagicFollowup = followup;
		action.aimToUnit(defender);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), action);
	}

	bool castOwnHaste(bool followup = false)
	{
		activate(attacker);
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::HASTE;
		action.metamagicFollowup = followup;
		action.aimToUnit(attacker);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	bool castOwnHasteWithoutMana()
	{
		activate(attacker);
		setTestSpellPointTotal(attackerSideHero, 0);
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::HASTE;
		action.aimToUnit(attacker);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	bool hasHaste() const
	{
		return defender->hasBonus(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(SpellID(SpellID::HASTE))));
	}

	CStack * attacker = nullptr;
	CStack * defender = nullptr;
};

class TypedCounterspellPerkTest : public CounterspellPerkTest
{
protected:
	void SetUp() override
	{
		typedAllowanceFixture = true;
		CounterspellPerkTest::SetUp();
	}
};

TEST_F(CounterspellPerkTest, CanonicalSpellAndCountermageDataAreActive)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	const auto spell = counterspell();
	ASSERT_NE(spell, SpellID::NONE);
	EXPECT_EQ(spell.toSpell()->getJsonKey(), counterspellKey);
	EXPECT_EQ(newHorizonsMagic::spellLevel(rules, spell), 3);
	for(int mastery = 0; mastery < 4; ++mastery)
		EXPECT_EQ(newHorizonsMagic::spellCost(rules, spell, mastery), newHorizonsMagic::COUNTERSPELL_LISTED_COST);

	const JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
	const auto & entries = perks["skills"]["new-horizons:sorceryMagic"]["perks"].Vector();
	const auto found = std::find_if(entries.begin(), entries.end(), [](const JsonNode & entry)
	{
		return entry["id"].String() == countermagePerk;
	});
	ASSERT_NE(found, entries.end());
	EXPECT_EQ((*found)["effect"]["status"].String(), "active");
}

TEST_F(CounterspellPerkTest, ArmedWardNegatesEnemyHeroSpellAndChargesListedCost)
{
	prepare(100, false);
	const auto enemyCost = defenderSideHero->getSpellCost(SpellID(SpellID::HASTE).toSpell());
	ASSERT_EQ(enemyCost, 4);
	const auto counterManaBefore = attackerSideHero->getManaAvailable();
	const auto enemyManaBefore = defenderSideHero->getManaAvailable();

	ASSERT_TRUE(castCounterspell());
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).counterspellArmed);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), counterManaBefore - newHorizonsMagic::COUNTERSPELL_LISTED_COST);

	ASSERT_TRUE(castEnemyHaste());
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).counterspellArmed);
	EXPECT_FALSE(hasHaste());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), counterManaBefore - newHorizonsMagic::COUNTERSPELL_LISTED_COST
		- newHorizonsMagic::counterspellCost(enemyCost, false));
	EXPECT_EQ(defenderSideHero->getManaAvailable(), enemyManaBefore - enemyCost);

	const auto casts = server.castsOf(SpellID::HASTE);
	ASSERT_FALSE(casts.empty());
	EXPECT_EQ(casts.back().announcement.counterspellSide, BattleSide::ATTACKER);
	EXPECT_TRUE(casts.back().announcement.counterspellNegated);
}

TEST_F(CounterspellPerkTest, CounterspellUsesEnemyListedCostBeforeWisdomDiscount)
{
	prepare(100, false);
	const int wisdomId = SecondarySkill::decode("new-horizons:wisdom");
	ASSERT_GE(wisdomId, 0);
	defenderSideHero->setSecSkillLevel(SecondarySkill(wisdomId), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);

	const auto listedCost = defenderSideHero->getListedSpellCost(SpellID(SpellID::HASTE).toSpell());
	const auto discountedCost = defenderSideHero->getSpellCost(SpellID(SpellID::HASTE).toSpell());
	ASSERT_EQ(listedCost, 4);
	ASSERT_EQ(discountedCost, 3);
	const auto counterManaBefore = attackerSideHero->getManaAvailable();
	const auto enemyManaBefore = defenderSideHero->getManaAvailable();

	ASSERT_TRUE(castCounterspell());
	ASSERT_TRUE(castEnemyHaste());

	// The enemy pays discounted Wisdom mana, but Counterspell prices the ward
	// from the enemy spell's raw listed cost: ceil(4 * 2) = 8.
	EXPECT_EQ(attackerSideHero->getManaAvailable(), counterManaBefore - newHorizonsMagic::COUNTERSPELL_LISTED_COST
		- newHorizonsMagic::counterspellCost(listedCost, false));
	EXPECT_EQ(defenderSideHero->getManaAvailable(), enemyManaBefore - discountedCost);
	EXPECT_FALSE(hasHaste());
}

TEST_F(CounterspellPerkTest, InsufficientArmingManaIsRejectedBeforeStateChanges)
{
	prepare(newHorizonsMagic::COUNTERSPELL_LISTED_COST - 1, false);
	EXPECT_FALSE(castCounterspell());
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).counterspellArmed);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), newHorizonsMagic::COUNTERSPELL_LISTED_COST - 1);
}

TEST_F(CounterspellPerkTest, CountermageUsesCeiledOnePointSeventyFiveMultiplier)
{
	prepare(100, true);
	const auto enemyCost = defenderSideHero->getSpellCost(SpellID(SpellID::HASTE).toSpell());
	ASSERT_EQ(enemyCost, 4);
	ASSERT_TRUE(castCounterspell());
	ASSERT_TRUE(castEnemyHaste());

	EXPECT_FALSE(hasHaste());
	EXPECT_EQ(attackerSideHero->getManaAvailable(),
		100 - newHorizonsMagic::COUNTERSPELL_LISTED_COST - newHorizonsMagic::counterspellCost(enemyCost, true));
	EXPECT_EQ(defenderSideHero->getManaAvailable(), 100 - enemyCost);
	const auto casts = server.castsOf(SpellID::HASTE);
	ASSERT_FALSE(casts.empty());
	EXPECT_TRUE(casts.back().announcement.counterspellNegated);
}

TEST_F(CounterspellPerkTest, InsufficientCounterManaCollapsesWardWithoutAdditionalMana)
{
	prepare(18, false);
	const auto enemyCost = defenderSideHero->getSpellCost(SpellID(SpellID::HASTE).toSpell());
	ASSERT_EQ(enemyCost, 4);
	ASSERT_TRUE(castCounterspell());
	ASSERT_EQ(attackerSideHero->getManaAvailable(), 18 - newHorizonsMagic::COUNTERSPELL_LISTED_COST);

	ASSERT_TRUE(castEnemyHaste());
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).counterspellArmed);
	EXPECT_TRUE(hasHaste());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 18 - newHorizonsMagic::COUNTERSPELL_LISTED_COST);
	EXPECT_EQ(defenderSideHero->getManaAvailable(), 100 - enemyCost);
	const auto casts = server.castsOf(SpellID::HASTE);
	ASSERT_FALSE(casts.empty());
	EXPECT_EQ(casts.back().announcement.counterspellSide, BattleSide::ATTACKER);
	EXPECT_FALSE(casts.back().announcement.counterspellNegated);
}

TEST_F(CounterspellPerkTest, ArmedWardExpiresAtTheArmingHerosNextHeroAction)
{
	prepare(100, false);
	ASSERT_TRUE(castCounterspell());
	ASSERT_TRUE(battle()->getSide(BattleSide::ATTACKER).counterspellArmed);

	advanceRound();
	ASSERT_TRUE(castOwnHaste());
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).counterspellArmed);
}

TEST_F(CounterspellPerkTest, InvalidOwnHeroSpellDoesNotExpireArmedWard)
{
	prepare(100, false);
	ASSERT_TRUE(castCounterspell());
	ASSERT_TRUE(battle()->getSide(BattleSide::ATTACKER).counterspellArmed);

	advanceRound();
	EXPECT_FALSE(castOwnHasteWithoutMana());
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).counterspellArmed);
}

TEST_F(TypedCounterspellPerkTest, TypedOrderPreservesOwnWardAndTimeStopUntilHeroSpell)
{
	prepare(100, false);
	ASSERT_TRUE(castCounterspell());
	ASSERT_TRUE(battle()->getSide(BattleSide::ATTACKER).counterspellArmed);

	// Make both action-boundary effects visible on the same accepted typed Order.
	// Keep the currently active stack eligible; the stopped defender exposes
	// the side-scoped Time Stop expiry behavior.
	auto marker = testTimeStopMarker(BattleSide::ATTACKER);
	battle()->addOrUpdateUnitBonus(defender, *marker, true);
	battle()->notePendingTimeStopHeroAction(BattleSide::ATTACKER);
	ASSERT_TRUE(defender->isTimeStopped());
	battle()->getSide(BattleSide::ATTACKER).heroActionAllowances.grantAllowance(
		HeroActionAllowanceState::AllowanceKind::ORDER,
		HeroActionAllowanceState::GrantSource::PERK, battle()->getRound());
	activate(attacker);
	ASSERT_EQ(battle()->battleActiveUnit(), attacker);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).counterspellArmed);
	EXPECT_TRUE(defender->isTimeStopped());

	// The following round's flexible Hero Action is the actual boundary for both.
	advanceRound();
	ASSERT_TRUE(castOwnHaste());
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).counterspellArmed);
	EXPECT_FALSE(defender->isTimeStopped());
}

TEST_F(TypedCounterspellPerkTest, MetamagicFollowupDoesNotExpireOwnWard)
{
	prepare(100, false, true, false);
	ASSERT_TRUE(castCounterspell());
	ASSERT_TRUE(battle()->getSide(BattleSide::ATTACKER).counterspellArmed);
	ASSERT_TRUE(battle()->battleCanUseMetamagicFollowup(BattleSide::ATTACKER));
	auto * stopped = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(15, 5), 10);
	ASSERT_NE(stopped, nullptr);
	auto marker = testTimeStopMarker(BattleSide::ATTACKER);
	battle()->addOrUpdateUnitBonus(stopped, *marker, true);
	battle()->notePendingTimeStopHeroAction(BattleSide::ATTACKER);
	ASSERT_TRUE(castOwnHaste(true));
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).counterspellArmed);
	EXPECT_TRUE(stopped->isTimeStopped());
}

TEST_F(TypedCounterspellPerkTest, TypedCounterspellReplacementClearsOldCountersequenceLink)
{
	prepare(100, false);
	auto & side = battle()->getSide(BattleSide::ATTACKER);
	side.counterspellArmed = true;
	side.metamagicCountersequenceArmed = true;
	side.heroActionAllowances.grantAllowance(HeroActionAllowanceState::AllowanceKind::SPELL,
		HeroActionAllowanceState::GrantSource::PERK, battle()->getRound());

	ASSERT_TRUE(castCounterspell());
	EXPECT_TRUE(side.counterspellArmed);
	EXPECT_FALSE(side.metamagicCountersequenceArmed);
}

TEST_F(TypedCounterspellPerkTest, EnemyMetamagicSpellActionStillConsumesWard)
{
	prepare(100, false, false, true);
	ASSERT_TRUE(castEnemyHaste()); // Creates a round-long Spell Action for the defender.
	ASSERT_TRUE(battle()->battleCanUseMetamagicFollowup(BattleSide::DEFENDER));
	// The attacker may cast only during its active stack's owner window.
	activate(attacker);
	ASSERT_TRUE(castCounterspell());
	ASSERT_TRUE(battle()->getSide(BattleSide::ATTACKER).counterspellArmed);
	ASSERT_TRUE(castEnemyHaste(true));
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).counterspellArmed);

	const auto casts = server.castsOf(SpellID::HASTE);
	ASSERT_GE(casts.size(), 2u);
	EXPECT_EQ(casts.back().announcement.counterspellSide, BattleSide::ATTACKER);
	EXPECT_TRUE(casts.back().announcement.counterspellNegated);
}
