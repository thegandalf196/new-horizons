/*
 * NewHorizonsDispelProfileTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

namespace
{
JsonNode magicRulesForVersion(int version)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	if(version == newHorizonsMagic::CURRENT_RULESET_VERSION)
		return rules;

	rules["rulesetVersion"].Integer() = version;
	rules.Struct().erase("schoolRankPowerCoefficientPercent");
	rules.Struct().erase("spellcraftEfficiencyPercent");
	if(version == newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION)
	{
		newHorizonsMagic::validateRules(rules);
		return rules;
	}

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
	newHorizonsMagic::validateRules(rules);
	return rules;
}
}

class NewHorizonsDispelProfileTest : public HeroCommandFixture
{
protected:
	int magicVersion = newHorizonsMagic::CURRENT_RULESET_VERSION;
	const CSpell * dispel = nullptr;
	CStack * friendly = nullptr;
	CStack * enemy = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRulesForVersion(magicVersion));
	}

	void prepare()
	{
		startGame();
		dispel = SpellID(SpellID::DISPEL).toSpell();
		ASSERT_NE(dispel, nullptr);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		const auto knownSpells = attackerSideHero->getSpellsInSpellbook();
		for(const auto knownSpell : knownSpells)
			attackerSideHero->removeSpellFromSpellbook(knownSpell);
		attackerSideHero->addSpellToSpellbook(SpellID::DISPEL);
		const int sorceryMagicId = SecondarySkill::decode("new-horizons:sorceryMagic");
		ASSERT_GE(sorceryMagicId, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(sorceryMagicId), MasteryLevel::EXPERT,
			ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
		startBattle();

		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 10);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 10);
		beginCombat();
		ASSERT_NE(friendly, nullptr);
		ASSERT_NE(enemy, nullptr);
		ASSERT_EQ(battle()->getMagicRules()["rulesetVersion"].Integer(), magicVersion);
	}

	void addTemporaryBlessAndCurse(CStack * stack)
	{
		Bonus bless(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
			BonusSource::SPELL_EFFECT, 2, BonusSourceID(SpellID(SpellID::BLESS)));
		bless.turnsRemain = 3;
		Bonus curse(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
			BonusSource::SPELL_EFFECT, -2, BonusSourceID(SpellID(SpellID::CURSE)));
		curse.turnsRemain = 3;

		SetStackEffect effect;
		effect.battleID = BattleID(0);
		effect.toAdd.emplace_back(stack->unitId(), std::vector<Bonus>{bless, curse});
		gameHandler->sendAndApply(effect);
		ASSERT_TRUE(hasEffect(stack, SpellID::BLESS));
		ASSERT_TRUE(hasEffect(stack, SpellID::CURSE));
	}

	bool hasEffect(const CStack * stack, SpellID spell) const
	{
		return stack->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell)));
	}

	void expectRank(int rank, bool smart, bool massive, int rangeLevel, int effectLevel,
		spells::AimType targetType)
	{
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, dispel);
		cast.setSpellLevel(rank);
		const auto mechanics = dispel->battleMechanics(&cast);
		EXPECT_EQ(mechanics->isSmart(), smart);
		EXPECT_EQ(mechanics->isMassive(), massive);
		EXPECT_EQ(mechanics->getRangeLevel(), rangeLevel);
		EXPECT_EQ(mechanics->getEffectLevel(), effectLevel);
		EXPECT_EQ(mechanics->getTargetTypes(), std::vector<spells::AimType>{targetType});
	}

	std::vector<std::string> effectNames(const spells::Mechanics & mechanics, bool * dispelOptional = nullptr)
	{
		std::vector<std::string> result;
		mechanics.forEachEffect([&](const spells::effects::Effect & effect)
		{
			result.push_back(effect.name);
			if(effect.name == "dispel" && dispelOptional)
				*dispelOptional = effect.optional;
			return false;
		});
		return result;
	}

	bool castOn(CStack * target)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::DISPEL;
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	bool castMass()
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::DISPEL;
		action.aimToHex(BattleHex::INVALID);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}
};

TEST_F(NewHorizonsDispelProfileTest, V1RetainsCoreTargetingAndExpertMassObstacleEffect)
{
	magicVersion = newHorizonsMagic::RULESET_VERSION;
	ASSERT_NO_FATAL_FAILURE(prepare());
	expectRank(MasteryLevel::BASIC, true, false, MasteryLevel::BASIC, MasteryLevel::BASIC,
		spells::AimType::CREATURE);
	expectRank(MasteryLevel::ADVANCED, false, false, MasteryLevel::ADVANCED, MasteryLevel::ADVANCED,
		spells::AimType::CREATURE);
	expectRank(MasteryLevel::EXPERT, false, true, MasteryLevel::EXPERT, MasteryLevel::EXPERT,
		spells::AimType::NOTHING);

	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, dispel);
	cast.setSpellLevel(MasteryLevel::EXPERT);
	const auto mechanics = dispel->battleMechanics(&cast);
	bool optional = false;
	EXPECT_EQ(effectNames(*mechanics, &optional), (std::vector<std::string>{"dispel", "removeObstacle"}));
	EXPECT_TRUE(optional);
}

TEST_F(NewHorizonsDispelProfileTest, V2ExpertCastRetainsCoreMassDispelEffectsAcrossBothSides)
{
	magicVersion = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	ASSERT_NO_FATAL_FAILURE(prepare());
	addTemporaryBlessAndCurse(friendly);
	addTemporaryBlessAndCurse(enemy);
	ASSERT_TRUE(castMass());
	EXPECT_FALSE(hasEffect(friendly, SpellID::BLESS));
	EXPECT_FALSE(hasEffect(friendly, SpellID::CURSE));
	EXPECT_FALSE(hasEffect(enemy, SpellID::BLESS));
	EXPECT_FALSE(hasEffect(enemy, SpellID::CURSE));
}

TEST_F(NewHorizonsDispelProfileTest, V2RetainsCoreTargetingAndExpertObstacleEffect)
{
	magicVersion = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	ASSERT_NO_FATAL_FAILURE(prepare());
	expectRank(MasteryLevel::BASIC, true, false, MasteryLevel::BASIC, MasteryLevel::BASIC,
		spells::AimType::CREATURE);
	expectRank(MasteryLevel::ADVANCED, false, false, MasteryLevel::ADVANCED, MasteryLevel::ADVANCED,
		spells::AimType::CREATURE);
	expectRank(MasteryLevel::EXPERT, false, true, MasteryLevel::EXPERT, MasteryLevel::EXPERT,
		spells::AimType::NOTHING);

	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, dispel);
	cast.setSpellLevel(MasteryLevel::EXPERT);
	const auto mechanics = dispel->battleMechanics(&cast);
	bool optional = false;
	EXPECT_EQ(effectNames(*mechanics, &optional), (std::vector<std::string>{"dispel", "removeObstacle"}));
	EXPECT_TRUE(optional);
}

TEST_F(NewHorizonsDispelProfileTest, V3DispelTargetsOneCreatureAtEveryRankWithoutDefaultMass)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	expectRank(MasteryLevel::BASIC, false, false, MasteryLevel::BASIC, MasteryLevel::BASIC,
		spells::AimType::CREATURE);
	expectRank(MasteryLevel::ADVANCED, false, false, MasteryLevel::ADVANCED, MasteryLevel::ADVANCED,
		spells::AimType::CREATURE);
	expectRank(MasteryLevel::EXPERT, false, false, MasteryLevel::ADVANCED, MasteryLevel::ADVANCED,
		spells::AimType::CREATURE);

	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, dispel);
	cast.setSpellLevel(MasteryLevel::EXPERT);
	const auto mechanics = dispel->battleMechanics(&cast);
	bool optional = true;
	EXPECT_EQ(effectNames(*mechanics, &optional), (std::vector<std::string>{"dispel"}));
	EXPECT_FALSE(optional);
	const auto description = newHorizonsMagic::spellDescriptionForHero(
		attackerSideHero, dispel, MasteryLevel::EXPERT);
	EXPECT_NE(description.find("one friendly or enemy stack"), std::string::npos);
	EXPECT_NE(description.find("does not make Dispel affect multiple stacks"), std::string::npos);

	addTemporaryBlessAndCurse(friendly);
	addTemporaryBlessAndCurse(enemy);
	ASSERT_TRUE(castOn(friendly));
	EXPECT_FALSE(hasEffect(friendly, SpellID::BLESS));
	EXPECT_FALSE(hasEffect(friendly, SpellID::CURSE));
	EXPECT_TRUE(hasEffect(enemy, SpellID::BLESS));
	EXPECT_TRUE(hasEffect(enemy, SpellID::CURSE));
}

TEST_F(NewHorizonsDispelProfileTest, V3DispelCanTargetAnEnemyStack)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addTemporaryBlessAndCurse(enemy);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, dispel);
	cast.setSpellLevel(MasteryLevel::EXPERT);
	const auto mechanics = dispel->battleMechanics(&cast);
	spells::Target target;
	target.emplace_back(enemy);
	EXPECT_TRUE(mechanics->canBeCastAt(target));
	ASSERT_TRUE(castOn(enemy));
	EXPECT_FALSE(hasEffect(enemy, SpellID::BLESS));
	EXPECT_FALSE(hasEffect(enemy, SpellID::CURSE));
}

TEST_F(NewHorizonsDispelProfileTest, V3RejectsUnrequestedExpertMassCastWithoutSpendingSpellPoints)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addTemporaryBlessAndCurse(friendly);
	const auto manaBefore = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(castMass());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
	EXPECT_TRUE(hasEffect(friendly, SpellID::BLESS));
	EXPECT_TRUE(hasEffect(friendly, SpellID::CURSE));
}
