/*
 * NewHorizonsHolyWrathTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in the main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"

namespace
{
constexpr auto holyWrathKey = "new-horizons:holyWrath";

SpellID holyWrathSpell()
{
	return SpellID(SpellID::decode(holyWrathKey));
}

JsonNode savedMagicRules(int version)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = version;
	for(const auto * key : {"schoolRankPowerCoefficientPercent", "spellcraftEfficiencyPercent",
		"morale", "creatureAbilities", "protectedAdventureBarriers", "heroLuck", "heroMorale"})
		rules.Struct().erase(key);
	for(const auto * spell : {"core:waterWalk", "core:fly"})
		rules["adventureSpells"][spell].Struct().erase("requireLegalDayEnd");
	if(version == newHorizonsMagic::RULESET_VERSION)
	{
		for(const auto * key : {"warcasting", "spellPoints", "mageGuildGeneration", "physicalDamageReductionCapPercent"})
			rules.Struct().erase(key);
		for(auto & [factionId, faction] : rules["factions"].Struct())
		{
			(void)factionId;
			faction["major"] = faction["preferredA"];
			faction["minor"] = faction["preferredB"];
			faction.Struct().erase("preferredA");
			faction.Struct().erase("preferredB");
		}
	}
	for(auto it = rules["spells"].Struct().begin(); it != rules["spells"].Struct().end();)
	{
		if(it->first == holyWrathKey || (version == newHorizonsMagic::RULESET_VERSION
			&& it->first.starts_with(GameConstants::NEW_HORIZONS_MOD_SCOPE + ':')))
		{
			it = rules["spells"].Struct().erase(it);
			continue;
		}
		auto & spell = it->second;
		for(const auto * key : {"selectedPlacement", "rawSpellPowerPerPatch", "earthquake", "structures", "heroAccess",
			"restoration", "propagationLimit", "implosion", "ignoreInterveningBarriers", "temporaryMagicalEffectsOnly",
			"schoolRankDurations", "burnGroundedFlyers"})
			spell.Struct().erase(key);
		if(spell.Struct().contains("variant"))
		{
			spell.Struct().erase("variant");
			spell["active"].Bool() = false;
		}
		if(version == newHorizonsMagic::RULESET_VERSION)
			for(const auto * key : {"active", "directDamage", "cureAfflictions"})
				spell.Struct().erase(key);
		++it;
	}
	rules.setOverrideFlag(true); // Do not reintroduce omitted current fields through base-settings merge.
	return rules;
}
}

class NewHorizonsHolyWrathTest : public HeroCommandFixture
{
protected:
	int magicRulesVersion = newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION;
	CStack * target = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires separate native curated preset";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
		if(magicRulesVersion != newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
		{
			rules = savedMagicRules(magicRulesVersion);
			JsonNode heroes(JsonPath::builtin("config/newHorizonsHeroes"));
			heroes["startingSkills"].Struct().erase("startingBookReplacements");
			heroes.setOverrideFlag(true);
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, heroes);
		}
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
	}

	void prepare(int spellPower = 110, const std::string & targetCreature = "core:pikeman",
		int32_t targetCount = 1000, BattleSide targetSide = BattleSide::DEFENDER)
	{
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		const auto spell = holyWrathSpell();
		ASSERT_NE(spell, SpellID::NONE);
		attackerSideHero->addSpellToSpellbook(spell);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		removeDeployedUnits();
		addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 1);
		if(targetSide == BattleSide::ATTACKER)
		{
			target = addStack(BattleSide::ATTACKER, creatureByName(targetCreature), BattleHex(5, 5), targetCount);
			addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 1);
		}
		else
			target = addStack(BattleSide::DEFENDER, creatureByName(targetCreature), BattleHex(12, 5), targetCount);
		beginCombat();
		ASSERT_NE(target, nullptr);
	}

	void removeDeployedUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
	}

	BattleAction holyWrathAction(const CStack * unit) const
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = holyWrathSpell();
		action.aimToUnit(unit);
		return action;
	}
};

TEST_F(NewHorizonsHolyWrathTest, CanonicalRosterAndSchoolRankUseTheFortyPlusTwoTimesSpellPowerFormula)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spellId = holyWrathSpell();
	const auto * spell = spellId.toSpell();
	ASSERT_NE(spell, nullptr);
	ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(attackerSideHero->getMagicRules(), spellId));
	EXPECT_EQ(newHorizonsMagic::spellCost(attackerSideHero->getMagicRules(), spellId, MasteryLevel::NONE), 11);
	ASSERT_EQ(attackerSideHero->getEffectPowerDivisor(spell), 10);

	const auto lightMagic = SecondarySkill(SecondarySkill::decode("new-horizons:lightMagic"));
	ASSERT_TRUE(lightMagic.hasValue());
	const std::array<int64_t, 4> expectedDamage{260, 293, 326, 359};
	const std::array<int, 4> expectedCoefficient{100, 115, 130, 145};
	for(size_t rank = 0; rank < expectedDamage.size(); ++rank)
	{
		attackerSideHero->setSecSkillLevel(lightMagic, static_cast<MasteryLevel::Type>(rank), ChangeValueMode::ABSOLUTE);
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
		const auto mechanics = spell->battleMechanics(&cast);
		EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientPercent(
			battle()->getMagicRules(), attackerSideHero, spellId), expectedCoefficient[rank]);
		EXPECT_EQ(mechanics->getEffectValue(), expectedDamage[rank])
			<< "School rank scales only the Spell Power coefficient; the 40 damage base remains fixed";
		EXPECT_EQ(mechanics->adjustEffectValue(target), expectedDamage[rank]);
	}

	const auto description = newHorizonsMagic::spellDescriptionForHero(
		attackerSideHero, spell, MasteryLevel::EXPERT);
	EXPECT_NE(description.find("40 + 2 x displayed Spell Power"), std::string::npos);
	EXPECT_NE(description.find("Undead"), std::string::npos);
	EXPECT_NE(description.find("base faction is Inferno"), std::string::npos);
	EXPECT_NE(description.find("Expert School: 145% Spell Power damage coefficient."), std::string::npos);
}

TEST_F(NewHorizonsHolyWrathTest, AuthoritativeCastDamagesOrdinaryEnemyAndSpendsElevenMana)
{
	ASSERT_NO_FATAL_FAILURE(prepare(110, "core:pikeman", 1000));
	const auto beforeHealth = target->getAvailableHealth();
	const auto beforeMana = attackerSideHero->getManaAvailable();

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), holyWrathAction(target)));
	EXPECT_EQ(beforeHealth - target->getAvailableHealth(), 260);
	EXPECT_EQ(beforeMana - attackerSideHero->getManaAvailable(), 11);
	ASSERT_EQ(server.castsOf(holyWrathSpell()).size(), 1u);
	EXPECT_EQ(server.castsOf(holyWrathSpell()).front().damage, 260);
}

TEST_F(NewHorizonsHolyWrathTest, AuthoritativeCastDealsOneHundredFiftyPercentToUndead)
{
	ASSERT_NO_FATAL_FAILURE(prepare(110, "core:skeleton", 1000));
	const auto undeadBefore = target->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), holyWrathAction(target)));
	EXPECT_EQ(undeadBefore - target->getAvailableHealth(), 390);
}

TEST_F(NewHorizonsHolyWrathTest, FinalDamageBonusIsAppliedBeforeDamageReceivedCap)
{
	ASSERT_NO_FATAL_FAILURE(prepare(110, "core:pikeman", 1000));
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::UNDEAD, BonusSource::OTHER, 1, BonusSourceID()));
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::DAMAGE_RECEIVED_CAP, BonusSource::OTHER, 50, BonusSourceID()));
	ASSERT_EQ(target->getMaxHealth(), 10);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, holyWrathSpell().toSpell());
	EXPECT_EQ(holyWrathSpell().toSpell()->battleMechanics(&cast)->adjustEffectValue(target), 5);

	const auto beforeHealth = target->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), holyWrathAction(target)));
	EXPECT_EQ(beforeHealth - target->getAvailableHealth(), 5)
		<< "The 50% damage cap on a 10-HP creature must apply after Holy Wrath's 150% multiplier";
}

TEST_F(NewHorizonsHolyWrathTest, CreatureWithBothQualifyingTraitsReceivesOneMultiplier)
{
	ASSERT_NO_FATAL_FAILURE(prepare(110, "core:imp", 1000));
	ASSERT_EQ(target->getFactionID(), FactionID::INFERNO);
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::UNDEAD, BonusSource::OTHER, 1, BonusSourceID()));
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, holyWrathSpell().toSpell());
	EXPECT_EQ(holyWrathSpell().toSpell()->battleMechanics(&cast)->adjustEffectValue(target), 390);
}

TEST_F(NewHorizonsHolyWrathTest, InfernoBaseFactionReceivesBonusAndOddDamageRoundsDownAfterResistance)
{
	ASSERT_NO_FATAL_FAILURE(prepare(110, "core:imp", 1000));
	ASSERT_EQ(target->getFactionID(), FactionID::INFERNO);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, holyWrathSpell().toSpell());
	EXPECT_EQ(holyWrathSpell().toSpell()->battleMechanics(&cast)->adjustEffectValue(target), 390);

	// Basic Light at displayed Spell Power 113 gives 40 + floor(226 * 1.15) = 299.
	// Mitigation first gives floor(299 / 2) = 149, then floor(149 * 1.5) = 223.
	// Reversing that order would give floor(floor(299 * 1.5) / 2) = 224.
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 113, ChangeValueMode::ABSOLUTE);
	const auto lightMagic = SecondarySkill(SecondarySkill::decode("new-horizons:lightMagic"));
	attackerSideHero->setSecSkillLevel(lightMagic, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(attackerSideHero->getSecSkillLevel(lightMagic), MasteryLevel::BASIC);
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_DAMAGE_REDUCTION, BonusSource::OTHER, 50, BonusSourceID(),
		BonusSubtypeID(SpellSchool::ANY)));
	spells::BattleCast resisted(battle(), attackerSideHero, spells::Mode::HERO, holyWrathSpell().toSpell());
	const auto mechanics = holyWrathSpell().toSpell()->battleMechanics(&resisted);
	ASSERT_EQ(mechanics->getEffectPower(), 113);
	ASSERT_EQ(mechanics->getEffectPowerDivisor(), 10);
	ASSERT_EQ(mechanics->getSpellPowerCoefficientBasisPoints(), 11500);
	EXPECT_EQ(mechanics->getEffectValue(), 299);
	EXPECT_EQ(mechanics->adjustEffectValue(target), 223);
}

TEST_F(NewHorizonsHolyWrathTest, SpellImmunityRejectsCastWithoutManaOrDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_IMMUNITY, BonusSource::OTHER, 1, BonusSourceID(),
		BonusSubtypeID(holyWrathSpell())));
	const auto beforeHealth = target->getAvailableHealth();
	const auto beforeMana = attackerSideHero->getManaAvailable();

	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, holyWrathSpell().toSpell());
	EXPECT_FALSE(holyWrathSpell().toSpell()->battleMechanics(&cast)->canBeCastAt(
		spells::Target{spells::Destination(target)}));
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), holyWrathAction(target)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), beforeMana);
	EXPECT_EQ(target->getAvailableHealth(), beforeHealth);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
}

TEST_F(NewHorizonsHolyWrathTest, FriendlyTargetIsRejectedWithoutManaOrDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare(110, "core:pikeman", 1000, BattleSide::ATTACKER));
	const auto beforeHealth = target->getAvailableHealth();
	const auto beforeMana = attackerSideHero->getManaAvailable();

	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, holyWrathSpell().toSpell());
	EXPECT_FALSE(holyWrathSpell().toSpell()->battleMechanics(&cast)->canBeCastAt(
		spells::Target{spells::Destination(target)}));
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), holyWrathAction(target)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), beforeMana);
	EXPECT_EQ(target->getAvailableHealth(), beforeHealth);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
}

TEST_F(NewHorizonsHolyWrathTest, V1SavedRosterDoesNotGainTheInstalledSpell)
{
	magicRulesVersion = newHorizonsMagic::RULESET_VERSION;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = holyWrathSpell();
	ASSERT_FALSE(newHorizonsMagic::spellAllowedBySavedRoster(attackerSideHero->getMagicRules(), spell));
	const auto beforeHealth = target->getAvailableHealth();
	const auto beforeMana = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), holyWrathAction(target)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), beforeMana);
	EXPECT_EQ(target->getAvailableHealth(), beforeHealth);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
}

TEST_F(NewHorizonsHolyWrathTest, V2SavedRosterDoesNotGainTheInstalledSpell)
{
	magicRulesVersion = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = holyWrathSpell();
	ASSERT_FALSE(newHorizonsMagic::spellAllowedBySavedRoster(attackerSideHero->getMagicRules(), spell));
	const auto beforeHealth = target->getAvailableHealth();
	const auto beforeMana = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), holyWrathAction(target)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), beforeMana);
	EXPECT_EQ(target->getAvailableHealth(), beforeHealth);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
}
