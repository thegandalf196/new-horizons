/*
 * NewHorizonsHolyArmorTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in the main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"

namespace
{
constexpr auto holyArmorKey = "new-horizons:holyArmor";
constexpr auto holyWrathKey = "new-horizons:holyWrath";

SpellID holyArmorSpell()
{
	return SpellID(SpellID::decode(holyArmorKey));
}

SpellID holyWrathSpell()
{
	return SpellID(SpellID::decode(holyWrathKey));
}

JsonNode legacyMagicRules(int version)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = version;
	rules.Struct().erase("schoolRankPowerCoefficientPercent");
	rules.Struct().erase("spellcraftEfficiencyPercent");
	if(version == newHorizonsMagic::RULESET_VERSION)
	{
		rules.Struct().erase("spellPoints");
		rules.Struct().erase("mageGuildGeneration");
		rules.Struct().erase("physicalDamageReductionCapPercent");
		rules.Struct().erase("warcasting");
		auto & spells = rules["spells"].Struct();
		for(auto it = spells.begin(); it != spells.end();)
		{
			if(it->first.starts_with(GameConstants::NEW_HORIZONS_MOD_SCOPE + ':'))
				it = spells.erase(it);
			else
				++it;
		}
		for(auto & [factionId, faction] : rules["factions"].Struct())
		{
			(void)factionId;
			faction["major"] = faction["preferredA"];
			faction["minor"] = faction["preferredB"];
			faction.Struct().erase("preferredA");
			faction.Struct().erase("preferredB");
		}
		for(auto & [name, spell] : rules["spells"].Struct())
		{
			(void)name;
			spell.Struct().erase("active");
			spell.Struct().erase("directDamage");
			spell.Struct().erase("cureAfflictions");
		}
	}
	else
		rules["spells"].Struct().erase(holyArmorKey);
	return rules;
}
}

class NewHorizonsHolyArmorTest : public HeroCommandFixture
{
protected:
	int magicRulesVersion = newHorizonsMagic::CURRENT_RULESET_VERSION;
	CStack * friendly = nullptr;
	CStack * otherFriendly = nullptr;
	CStack * enemy = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires separate native curated preset";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode rules = magicRulesVersion == newHorizonsMagic::CURRENT_RULESET_VERSION
			? JsonNode(JsonPath::builtin("config/newHorizonsMagic"))
			: legacyMagicRules(magicRulesVersion);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
	}

	void prepare(int spellPower = 50)
	{
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(holyArmorSpell());
		attackerSideHero->addSpellToSpellbook(holyWrathSpell());
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		removeDeployedUnits();
		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 1000);
		otherFriendly = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(5, 5), 1000);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 1000);
		beginCombat();
	}

	void removeDeployedUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
	}

	BattleAction holyArmorAction(const CStack * unit) const
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = holyArmorSpell();
		action.aimToUnit(unit);
		return action;
	}

	TConstBonusListPtr holyArmorBonuses(const CStack * unit) const
	{
		return unit->getAllBonuses(Selector::source(
			BonusSource::SPELL_EFFECT, BonusSourceID(holyArmorSpell())));
	}
};

TEST_F(NewHorizonsHolyArmorTest, AuthoritativeCastUsesSavedLightCoefficientAndKeepsTheTwoRoundBuffOnOneFriendly)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = holyArmorSpell();
	ASSERT_NE(spell, SpellID::NONE);
	ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(attackerSideHero->getMagicRules(), spell));
	EXPECT_EQ(newHorizonsMagic::spellCost(attackerSideHero->getMagicRules(), spell, MasteryLevel::NONE), 8);

	const auto lightMagic = SecondarySkill(SecondarySkill::decode("new-horizons:lightMagic"));
	ASSERT_TRUE(lightMagic.hasValue());
	attackerSideHero->setSecSkillLevel(lightMagic, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	const int coefficientBasisPoints = newHorizonsMagic::spellPowerCoefficientBasisPoints(
		battle()->getMagicRules(), attackerSideHero, spell);
	const int expectedReduction = 30 + std::min(30, 50 * coefficientBasisPoints / 50000);
	const auto beforeMana = attackerSideHero->getManaAvailable();

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), PlayerColor(0), holyArmorAction(friendly)));
	EXPECT_EQ(beforeMana - attackerSideHero->getManaAvailable(), 8);

	const auto applied = holyArmorBonuses(friendly);
	ASSERT_EQ(applied->size(), 1u);
	EXPECT_EQ(applied->front()->type, BonusType::SPELL_DAMAGE_REDUCTION);
	EXPECT_EQ(applied->front()->subtype, BonusSubtypeID(SpellSchool::ANY));
	EXPECT_EQ(applied->front()->val, expectedReduction)
		<< "School rank modifies only the Spell Power-derived term; the fixed 30% remains unchanged";
	EXPECT_EQ(applied->front()->turnsRemain, 2);
	EXPECT_TRUE(holyArmorBonuses(otherFriendly)->empty())
		<< "A single-target cast must leave the other friendly stack unaffected";
}

TEST_F(NewHorizonsHolyArmorTest, ReductionIsCappedAtSixtyPercent)
{
	ASSERT_NO_FATAL_FAILURE(prepare(150));
	const auto lightMagic = SecondarySkill(SecondarySkill::decode("new-horizons:lightMagic"));
	ASSERT_TRUE(lightMagic.hasValue());
	attackerSideHero->setSecSkillLevel(lightMagic, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), PlayerColor(0), holyArmorAction(friendly)));
	const auto applied = holyArmorBonuses(friendly);
	ASSERT_EQ(applied->size(), 1u);
	EXPECT_EQ(applied->front()->val, 60);
}

TEST_F(NewHorizonsHolyArmorTest, PositiveSmartTargetRejectsAnEnemyWithoutSpendingMana)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto beforeMana = attackerSideHero->getManaAvailable();
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, holyArmorSpell().toSpell());
	EXPECT_FALSE(holyArmorSpell().toSpell()->battleMechanics(&cast)->canBeCastAt(
		spells::Target{spells::Destination(enemy)}));
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), PlayerColor(0), holyArmorAction(enemy)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), beforeMana);
	EXPECT_TRUE(holyArmorBonuses(enemy)->empty());
}

TEST_F(NewHorizonsHolyArmorTest, HolyArmorReducesMagicalDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto lightMagic = SecondarySkill(SecondarySkill::decode("new-horizons:lightMagic"));
	ASSERT_TRUE(lightMagic.hasValue());
	attackerSideHero->setSecSkillLevel(lightMagic, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), PlayerColor(0), holyArmorAction(friendly)));

	spells::BattleCast damageCast(battle(), attackerSideHero, spells::Mode::HERO, holyWrathSpell().toSpell());
	const auto mechanics = holyWrathSpell().toSpell()->battleMechanics(&damageCast);
	const auto applied = holyArmorBonuses(friendly);
	ASSERT_EQ(applied->size(), 1u);
	EXPECT_EQ(mechanics->adjustEffectValue(friendly),
		mechanics->getEffectValue() * (100 - applied->front()->val) / 100)
		<< "Holy Armor's independent reduction applies to Holy Wrath damage";
}

TEST_F(NewHorizonsHolyArmorTest, V1SavedRosterDoesNotGainTheInstalledSpell)
{
	magicRulesVersion = newHorizonsMagic::RULESET_VERSION;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_FALSE(newHorizonsMagic::spellAllowedBySavedRoster(
		attackerSideHero->getMagicRules(), holyArmorSpell()));
	const auto beforeMana = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), PlayerColor(0), holyArmorAction(friendly)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), beforeMana);
	EXPECT_TRUE(holyArmorBonuses(friendly)->empty());
}

TEST_F(NewHorizonsHolyArmorTest, V2SavedRosterDoesNotGainTheInstalledSpell)
{
	magicRulesVersion = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_FALSE(newHorizonsMagic::spellAllowedBySavedRoster(
		attackerSideHero->getMagicRules(), holyArmorSpell()));
	const auto beforeMana = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(
		BattleID(0), PlayerColor(0), holyArmorAction(friendly)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), beforeMana);
	EXPECT_TRUE(holyArmorBonuses(friendly)->empty());
}
