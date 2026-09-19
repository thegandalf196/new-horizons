/*
 * CounterspellPerkTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"

#include "HeroCommandFixture.h"

#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

namespace
{
constexpr auto counterspellKey = "new-horizons:counterspell";
constexpr auto sorcerySkill = "new-horizons:sorceryMagic";
constexpr auto countermagePerk = "new-horizons:sorceryMagic.countermage";

SpellID counterspell()
{
	return SpellID(SpellID::decode(counterspellKey));
}
}

class CounterspellPerkTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
		useCommands = false;
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare(int counterMana, bool selectCountermage)
	{
		startGame();
		const auto decoded = SecondarySkill::decode(sorcerySkill);
		ASSERT_GE(decoded, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(decoded), 2, ChangeValueMode::ABSOLUTE);
		defenderSideHero->setSecSkillLevel(SecondarySkill(decoded), 1, ChangeValueMode::ABSOLUTE);
		if(selectCountermage)
			attackerSideHero->applyPerkSelection({sorcerySkill, countermagePerk});

		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(counterspell());
		attackerSideHero->addSpellToSpellbook(SpellID::HASTE);
		defenderSideHero->addSpellToSpellbook(SpellID::HASTE);
		attackerSideHero->mana = counterMana;
		defenderSideHero->mana = 100;

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

	bool castEnemyHaste()
	{
		activate(defender);
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::DEFENDER;
		action.spell = SpellID::HASTE;
		action.aimToUnit(defender);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), action);
	}

	bool castOwnHaste()
	{
		activate(attacker);
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::HASTE;
		action.aimToUnit(attacker);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	bool castOwnHasteWithoutMana()
	{
		activate(attacker);
		attackerSideHero->mana = 0;
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
	const auto & entries = perks["new-horizons:sorceryMagic"]["perks"].Vector();
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
	const auto counterManaBefore = attackerSideHero->mana;
	const auto enemyManaBefore = defenderSideHero->mana;

	ASSERT_TRUE(castCounterspell());
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).counterspellArmed);
	EXPECT_EQ(attackerSideHero->mana, counterManaBefore - newHorizonsMagic::COUNTERSPELL_LISTED_COST);

	ASSERT_TRUE(castEnemyHaste());
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).counterspellArmed);
	EXPECT_FALSE(hasHaste());
	EXPECT_EQ(attackerSideHero->mana, counterManaBefore - newHorizonsMagic::COUNTERSPELL_LISTED_COST
		- newHorizonsMagic::counterspellCost(enemyCost, false));
	EXPECT_EQ(defenderSideHero->mana, enemyManaBefore - enemyCost);

	const auto casts = server.castsOf(SpellID::HASTE);
	ASSERT_FALSE(casts.empty());
	EXPECT_EQ(casts.back().announcement.counterspellSide, BattleSide::ATTACKER);
	EXPECT_TRUE(casts.back().announcement.counterspellNegated);
}

TEST_F(CounterspellPerkTest, CountermageUsesCeiledOnePointSeventyFiveMultiplier)
{
	prepare(100, true);
	const auto enemyCost = defenderSideHero->getSpellCost(SpellID(SpellID::HASTE).toSpell());
	ASSERT_EQ(enemyCost, 4);
	ASSERT_TRUE(castCounterspell());
	ASSERT_TRUE(castEnemyHaste());

	EXPECT_FALSE(hasHaste());
	EXPECT_EQ(attackerSideHero->mana,
		100 - newHorizonsMagic::COUNTERSPELL_LISTED_COST - newHorizonsMagic::counterspellCost(enemyCost, true));
	EXPECT_EQ(defenderSideHero->mana, 100 - enemyCost);
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
	ASSERT_EQ(attackerSideHero->mana, 18 - newHorizonsMagic::COUNTERSPELL_LISTED_COST);

	ASSERT_TRUE(castEnemyHaste());
	EXPECT_FALSE(battle()->getSide(BattleSide::ATTACKER).counterspellArmed);
	EXPECT_TRUE(hasHaste());
	EXPECT_EQ(attackerSideHero->mana, 18 - newHorizonsMagic::COUNTERSPELL_LISTED_COST);
	EXPECT_EQ(defenderSideHero->mana, 100 - enemyCost);
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
