/*
 * NewHorizonsCrusadeTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later; see license.txt file, in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../SpellPointTestUtils.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"

namespace
{
constexpr auto crusadeKey = "new-horizons:crusade";
constexpr auto lightMagicSkill = "new-horizons:lightMagic";
constexpr auto crusaderPerk = "new-horizons:lightMagic.crusader";
constexpr auto metamagicSkill = "new-horizons:metamagic";
constexpr auto arcaneAcquisitionPerk = "new-horizons:metamagic.arcaneAcquisition";
constexpr auto echoedDurationPerk = "new-horizons:metamagic.echoedDuration";

SpellID crusadeSpell()
{
	return SpellID(SpellID::decode(crusadeKey));
}

JsonNode legacyMagicRules(int version)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = version;
	rules.Struct().erase("schoolRankPowerCoefficientPercent");
	rules.Struct().erase("spellcraftEfficiencyPercent");
	for(auto & [name, spell] : rules["spells"].Struct())
	{
		(void)name;
		spell.Struct().erase("selectedPlacement");
		if(spell.Struct().contains("variant"))
		{
			spell.Struct().erase("variant");
			spell["active"].Bool() = false;
		}
	}
	rules["spells"].Struct().erase(crusadeKey);
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
	return rules;
}

TConstBonusListPtr crusadeBonuses(const CStack * unit)
{
	return unit->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(crusadeSpell())));
}

std::shared_ptr<Bonus> flatDamageReduction(int value)
{
	return std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELL_DAMAGE_REDUCTION,
		BonusSource::OTHER, value, BonusSourceID(), BonusSubtypeID(SpellSchool::ANY));
}
}

class NewHorizonsCrusadeTest : public HeroCommandFixture
{
protected:
	int magicRulesVersion = newHorizonsMagic::CURRENT_RULESET_VERSION;
	int spellPower = 50;
	int lightRank = MasteryLevel::NONE;
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
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void removeDeployedUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
	}

	void prepare(int power = 50, int lightMagicRank = MasteryLevel::NONE)
	{
		spellPower = power;
		lightRank = lightMagicRank;
		startGame();
		ASSERT_NE(crusadeSpell(), SpellID::NONE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(crusadeSpell());
		attackerSideHero->addSpellToSpellbook(SpellID::HASTE);
		attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
		if(lightRank != MasteryLevel::NONE)
			attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(lightMagicSkill)),
				lightRank, ChangeValueMode::ABSOLUTE);

		startBattle();
		removeDeployedUnits();
		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 1000);
		otherFriendly = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(5, 5), 1000);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 1000);
		beginCombat();
	}

	BattleAction crusadeAction(bool followup = false) const
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = crusadeSpell();
		action.metamagicFollowup = followup;
		action.aimToHex(BattleHex::INVALID);
		return action;
	}

	bool castCrusade(bool followup = false)
	{
		return gameHandler->battles->makePlayerBattleAction(
			BattleID(0), PlayerColor(0), crusadeAction(followup));
	}

	bool castHaste()
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::HASTE;
		action.aimToUnit(friendly);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	void selectEchoedDuration()
	{
		const auto decoded = SecondarySkill::decode(metamagicSkill);
		ASSERT_GE(decoded, 0);
		const SecondarySkill skill(decoded);
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({metamagicSkill, arcaneAcquisitionPerk});
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({metamagicSkill, echoedDurationPerk});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(metamagicSkill, echoedDurationPerk));
	}

	void selectLightCrusader()
	{
		const auto decoded = SecondarySkill::decode(lightMagicSkill);
		ASSERT_GE(decoded, 0);
		const SecondarySkill skill(decoded);
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({lightMagicSkill, "new-horizons:lightMagic.healer"});
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({lightMagicSkill, "new-horizons:lightMagic.purifier"});
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({lightMagicSkill, crusaderPerk});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(lightMagicSkill, crusaderPerk));
	}

	void advanceRound()
	{
		BattleNextRound next;
		next.battleID = BattleID(0);
		gameHandler->sendAndApply(next);
	}

	std::shared_ptr<Bonus> bonusOfType(const CStack * unit, BonusType type, BonusSubtypeID subtype = {}) const
	{
		const auto bonuses = unit->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(crusadeSpell())).And(Selector::typeSubtype(type, subtype)));
		if(bonuses->empty())
			return {};
		return std::make_shared<Bonus>(*bonuses->front());
	}
};

TEST_F(NewHorizonsCrusadeTest, AuthoritativeCastEmpowersAlliesAndUsesOneHeroAction)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto spell = crusadeSpell();
	ASSERT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(attackerSideHero->getMagicRules(), spell));
	EXPECT_EQ(newHorizonsMagic::spellCost(attackerSideHero->getMagicRules(), spell, MasteryLevel::NONE), 24);

	const auto manaBefore = attackerSideHero->getManaAvailable();
	const auto initiativeBefore = friendly->getInitiative();
	const auto movementBefore = friendly->getMovementRange();
	Bonus moralePenalty(BonusDuration::PERMANENT, BonusType::MORALE, BonusSource::OTHER, -20, BonusSourceID());
	friendly->addNewBonus(std::make_shared<Bonus>(moralePenalty));
	ASSERT_LT(friendly->moraleVal(), 0);

	ASSERT_TRUE(castCrusade());
	EXPECT_EQ(manaBefore - attackerSideHero->getManaAvailable(), 24);
	EXPECT_EQ(friendly->moraleVal(), 0);
	EXPECT_EQ(friendly->getInitiative(), initiativeBefore + 1)
		<< "Crusade contributes flat Initiative without changing movement Speed";
	EXPECT_EQ(friendly->getMovementRange(), movementBefore);

	const auto coefficient = newHorizonsMagic::spellPowerCoefficientBasisPoints(
		battle()->getMagicRules(), attackerSideHero, spell);
	const auto expectedAttribute = std::min<int64_t>(6, 3
		+ spells::scaleSpellPowerComponentWithCoefficientBasisPoints(spellPower, 75, coefficient, 0, 0));
	const auto expectedInitiative = std::min<int64_t>(3, 1
		+ spells::scaleSpellPowerComponentWithCoefficientBasisPoints(spellPower, 100, coefficient, 0, 0));
	const auto expectedMDR = std::min<int64_t>(2500, 1200
		+ spells::scaleSpellPowerComponentWithCoefficientBasisPoints(13 * spellPower, 2, coefficient, 0, 0));
	EXPECT_EQ(expectedMDR, 1525) << "The unranked SP 50 profile matches the canonical 15.25% example";

	for(const auto * unit : {friendly, otherFriendly})
	{
		const auto attack = bonusOfType(unit, BonusType::PRIMARY_SKILL,
			BonusSubtypeID(PrimarySkill::ATTACK));
		const auto defense = bonusOfType(unit, BonusType::PRIMARY_SKILL,
			BonusSubtypeID(PrimarySkill::DEFENSE));
		const auto initiative = bonusOfType(unit, BonusType::STACKS_INITIATIVE_FLAT);
		const auto reduction = bonusOfType(unit, BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS,
			BonusSubtypeID(SpellSchool::ANY));
		const auto moraleFloor = bonusOfType(unit, BonusType::MINIMUM_MORALE);
		ASSERT_NE(attack, nullptr);
		ASSERT_NE(defense, nullptr);
		ASSERT_NE(initiative, nullptr);
		ASSERT_NE(reduction, nullptr);
		ASSERT_NE(moraleFloor, nullptr);
		EXPECT_EQ(attack->val, expectedAttribute);
		EXPECT_EQ(defense->val, expectedAttribute);
		EXPECT_EQ(initiative->val, expectedInitiative);
		EXPECT_EQ(reduction->val, expectedMDR);
		EXPECT_EQ(moraleFloor->val, 0);
		EXPECT_EQ(attack->turnsRemain, 3);
		EXPECT_EQ(reduction->turnsRemain, 3);
		EXPECT_EQ(crusadeBonuses(unit)->size(), 5u);
	}
	EXPECT_TRUE(crusadeBonuses(enemy)->empty());

	const auto afterFirstCast = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(castCrusade()) << "A second hero spell in the same battle turn is not available";
	EXPECT_EQ(attackerSideHero->getManaAvailable(), afterFirstCast);
}

TEST_F(NewHorizonsCrusadeTest, SpellPowerTableAndSchoolRankScaleOnlyDerivedTerms)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, MasteryLevel::NONE));
	ASSERT_TRUE(castCrusade());
	EXPECT_EQ(bonusOfType(friendly, BonusType::PRIMARY_SKILL,
		BonusSubtypeID(PrimarySkill::ATTACK))->val, 4);
	EXPECT_EQ(bonusOfType(friendly, BonusType::PRIMARY_SKILL,
		BonusSubtypeID(PrimarySkill::DEFENSE))->val, 4);
	EXPECT_EQ(bonusOfType(friendly, BonusType::STACKS_INITIATIVE_FLAT)->val, 2);
	EXPECT_EQ(bonusOfType(friendly, BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS,
		BonusSubtypeID(SpellSchool::ANY))->val, 1850);
}

TEST_F(NewHorizonsCrusadeTest, AuthoredAttackDefenseInitiativeAndReductionCapsAreEnforced)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1000, MasteryLevel::NONE));
	ASSERT_TRUE(castCrusade());
	EXPECT_EQ(bonusOfType(friendly, BonusType::PRIMARY_SKILL,
		BonusSubtypeID(PrimarySkill::ATTACK))->val, 6);
	EXPECT_EQ(bonusOfType(friendly, BonusType::PRIMARY_SKILL,
		BonusSubtypeID(PrimarySkill::DEFENSE))->val, 6);
	EXPECT_EQ(bonusOfType(friendly, BonusType::STACKS_INITIATIVE_FLAT)->val, 3);
	EXPECT_EQ(bonusOfType(friendly, BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS,
		BonusSubtypeID(SpellSchool::ANY))->val, 2500);
}

TEST_F(NewHorizonsCrusadeTest, BasicLightScalesOnlyTheSpellPowerReductionTerm)
{
	ASSERT_NO_FATAL_FAILURE(prepare(50, MasteryLevel::BASIC));
	ASSERT_TRUE(castCrusade());
	EXPECT_EQ(bonusOfType(friendly, BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS,
		BonusSubtypeID(SpellSchool::ANY))->val, 1573)
		<< "Basic Light's 115% coefficient scales only 0.065% x Spell Power; the 12% base is fixed";
}

TEST_F(NewHorizonsCrusadeTest, ExpertLightScalesOnlyTheSpellPowerReductionTerm)
{
	ASSERT_NO_FATAL_FAILURE(prepare(50, MasteryLevel::EXPERT));
	ASSERT_TRUE(castCrusade());
	EXPECT_EQ(bonusOfType(friendly, BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS,
		BonusSubtypeID(SpellSchool::ANY))->val, 1671)
		<< "Expert Light's 145% coefficient retains the fractional 471.25-basis-point term until its final floor";
}

TEST_F(NewHorizonsCrusadeTest, CrusaderExtendsEveryAppliedBonusToFourRounds)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	selectLightCrusader();
	ASSERT_TRUE(castCrusade());
	for(const auto * unit : {friendly, otherFriendly})
	{
		EXPECT_EQ(bonusOfType(unit, BonusType::PRIMARY_SKILL,
			BonusSubtypeID(PrimarySkill::ATTACK))->turnsRemain, 4);
		EXPECT_EQ(bonusOfType(unit, BonusType::STACKS_INITIATIVE_FLAT)->turnsRemain, 4);
		EXPECT_EQ(bonusOfType(unit, BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS,
			BonusSubtypeID(SpellSchool::ANY))->turnsRemain, 4);
	}
}

TEST_F(NewHorizonsCrusadeTest, EchoedDurationAddsOneRoundToAllCrusadeBonuses)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	selectEchoedDuration();
	ASSERT_TRUE(castHaste());
	ASSERT_TRUE(castCrusade(true));
	const auto applied = crusadeBonuses(friendly);
	ASSERT_EQ(applied->size(), 5u);
	for(const auto & bonus : *applied)
		EXPECT_EQ(bonus->turnsRemain, 4) << "Echoed Duration adjusts the fixed 3-round base exactly once";
}

TEST_F(NewHorizonsCrusadeTest, EchoedDurationAndCrusaderEachAddOneRoundToAllCrusadeBonuses)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	selectEchoedDuration();
	selectLightCrusader();
	ASSERT_TRUE(castHaste());
	ASSERT_TRUE(castCrusade(true));
	const auto applied = crusadeBonuses(friendly);
	ASSERT_EQ(applied->size(), 5u);
	for(const auto & bonus : *applied)
		EXPECT_EQ(bonus->turnsRemain, 5)
			<< "Echoed Duration adjusts the literal 3-round base once, then Crusader adds its own round";
}

TEST_F(NewHorizonsCrusadeTest, RecastRefreshesTheSameSourceAndTheNormalDurationExpires)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(castCrusade());
	auto reduction = bonusOfType(friendly, BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS,
		BonusSubtypeID(SpellSchool::ANY));
	ASSERT_NE(reduction, nullptr);
	ASSERT_EQ(reduction->turnsRemain, 3);
	while(battle()->getRound() == 0)
		advanceRound();
	advanceRound();
	reduction = bonusOfType(friendly, BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS,
		BonusSubtypeID(SpellSchool::ANY));
	ASSERT_NE(reduction, nullptr);
	ASSERT_EQ(reduction->turnsRemain, 2);

	ASSERT_TRUE(castCrusade()) << "The hero can refresh the N_TURNS source on a later round";
	EXPECT_EQ(crusadeBonuses(friendly)->size(), 5u) << "Refresh must update, not stack, the same spell source";
	reduction = bonusOfType(friendly, BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS,
		BonusSubtypeID(SpellSchool::ANY));
	ASSERT_NE(reduction, nullptr);
	ASSERT_EQ(reduction->turnsRemain, 3);

	advanceRound();
	reduction = bonusOfType(friendly, BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS,
		BonusSubtypeID(SpellSchool::ANY));
	ASSERT_NE(reduction, nullptr);
	EXPECT_EQ(reduction->turnsRemain, 2);
	advanceRound();
	reduction = bonusOfType(friendly, BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS,
		BonusSubtypeID(SpellSchool::ANY));
	ASSERT_NE(reduction, nullptr);
	EXPECT_EQ(reduction->turnsRemain, 1);
	advanceRound();
	EXPECT_TRUE(crusadeBonuses(friendly)->empty());
	EXPECT_TRUE(crusadeBonuses(otherFriendly)->empty());
}

TEST_F(NewHorizonsCrusadeTest, FractionalReductionMultipliesWithExistingMagicalProtection)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(castCrusade());
	const auto crusadeReduction = bonusOfType(friendly, BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS,
		BonusSubtypeID(SpellSchool::ANY));
	ASSERT_NE(crusadeReduction, nullptr);
	friendly->addNewBonus(flatDamageReduction(50));

	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast event(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&event);
	const auto rawDamage = mechanics->getEffectValue();
	ASSERT_GT(rawDamage, 0);
	const auto expectedDamage = rawDamage * 5000 * (10000 - crusadeReduction->val) / 100'000'000;
	EXPECT_EQ(mechanics->adjustEffectValue(friendly), expectedDamage)
		<< "A 50% integer reduction and Crusade's fractional reduction are independent multiplicative sources";
}

TEST_F(NewHorizonsCrusadeTest, VersionOneSavedRulesDoNotGainCrusade)
{
	magicRulesVersion = newHorizonsMagic::RULESET_VERSION;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(newHorizonsMagic::spellAllowedBySavedRoster(attackerSideHero->getMagicRules(), crusadeSpell()));
	const auto manaBefore = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(castCrusade());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_TRUE(crusadeBonuses(friendly)->empty());
}

TEST_F(NewHorizonsCrusadeTest, VersionTwoSavedRulesDoNotGainCrusade)
{
	magicRulesVersion = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(newHorizonsMagic::spellAllowedBySavedRoster(attackerSideHero->getMagicRules(), crusadeSpell()));
	const auto manaBefore = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(castCrusade());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_TRUE(crusadeBonuses(friendly)->empty());
}

TEST(NewHorizonsCrusadeSerializationTest, FractionalReductionRoundTripsInCurrentSavesAndRejectsDownsave)
{
	const Bonus source(BonusDuration::N_TURNS, BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS,
		BonusSource::SPELL_EFFECT, 1573, BonusSourceID(crusadeSpell()), BonusSubtypeID(SpellSchool::ANY));
	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.oser & source;

	CMemorySerializer restored(current.extractBuffer());
	restored.iser.version = ESerializationVersion::CURRENT;
	Bonus loaded;
	restored.iser & loaded;
	EXPECT_EQ(loaded.type, BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS);
	EXPECT_EQ(loaded.val, 1573);
	EXPECT_EQ(loaded.subtype, BonusSubtypeID(SpellSchool::ANY));

	CMemorySerializer oldSave;
	oldSave.oser.version = ESerializationVersion::NEW_HORIZONS_PURIFY;
	EXPECT_THROW(oldSave.oser & source, std::runtime_error);
}
