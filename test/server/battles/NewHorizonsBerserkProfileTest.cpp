/*
 * NewHorizonsBerserkProfileTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/modding/CModHandler.h"
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

class NewHorizonsBerserkProfileTest : public HeroCommandFixture
{
protected:
	int magicVersion = newHorizonsMagic::CURRENT_RULESET_VERSION;
	const CSpell * berserk = nullptr;
	CStack * selectedTarget = nullptr;
	CStack * adjacentTarget = nullptr;
	CStack * friendlyTarget = nullptr;

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
		berserk = SpellID(SpellID::BERSERK).toSpell();
		ASSERT_NE(berserk, nullptr);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::BERSERK);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 1000, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
		startBattle();

		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);

		selectedTarget = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 1);
		adjacentTarget = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(13, 5), 1);
		friendlyTarget = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 1);
		beginCombat();
		ASSERT_NE(selectedTarget, nullptr);
		ASSERT_NE(adjacentTarget, nullptr);
		ASSERT_NE(friendlyTarget, nullptr);
	}

	void giveExpertChaosMagic()
	{
		const int chaosMagicId = SecondarySkill::decode("new-horizons:chaosMagic");
		ASSERT_GE(chaosMagicId, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(chaosMagicId), MasteryLevel::EXPERT,
			ChangeValueMode::ABSOLUTE);
	}

	void expectTargetingAtRank(int rank, spells::AimType type, bool smart, int rangeLevel)
	{
		const std::vector<int> expectedCoreRange = rank < MasteryLevel::ADVANCED
			? std::vector<int>{0}
			: rank == MasteryLevel::ADVANCED ? std::vector<int>{0, 1} : std::vector<int>{0, 1, 2};
		EXPECT_EQ(berserk->getLevelInfo(rank).range, expectedCoreRange);

		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, berserk);
		cast.setSpellLevel(rank);
		const auto mechanics = berserk->battleMechanics(&cast);
		EXPECT_EQ(mechanics->getTargetTypes(), std::vector<spells::AimType>{type});
		EXPECT_EQ(mechanics->isSmart(), smart);
		EXPECT_EQ(mechanics->getRangeLevel(), rangeLevel);
		EXPECT_EQ(mechanics->getEffectLevel(), rank);
		EXPECT_FALSE(mechanics->isMassive());
	}
};

TEST_F(NewHorizonsBerserkProfileTest, V1PreservesVanillaLocationAndMasteryRange)
{
	magicVersion = newHorizonsMagic::RULESET_VERSION;
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(int rank = MasteryLevel::NONE; rank <= MasteryLevel::EXPERT; ++rank)
		expectTargetingAtRank(rank, spells::AimType::LOCATION, false, rank);
	EXPECT_FALSE(newHorizonsMagic::berserkUsesSingleCreatureTarget(battle()->getMagicRules()));
}

TEST_F(NewHorizonsBerserkProfileTest, V1ExpertLocationCastAffectsAdjacentStack)
{
	magicVersion = newHorizonsMagic::RULESET_VERSION;
	ASSERT_NO_FATAL_FAILURE(prepare());
	giveExpertChaosMagic();

	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::BERSERK;
	action.aimToHex(selectedTarget->getPosition());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_TRUE(selectedTarget->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
	EXPECT_TRUE(adjacentTarget->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
}

TEST_F(NewHorizonsBerserkProfileTest, V2PreservesVanillaLocationAndMasteryRange)
{
	magicVersion = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(int rank = MasteryLevel::NONE; rank <= MasteryLevel::EXPERT; ++rank)
		expectTargetingAtRank(rank, spells::AimType::LOCATION, false, rank);
	EXPECT_FALSE(newHorizonsMagic::berserkUsesSingleCreatureTarget(battle()->getMagicRules()));
}

TEST_F(NewHorizonsBerserkProfileTest, V2ExpertLocationCastAffectsAdjacentStack)
{
	magicVersion = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	ASSERT_NO_FATAL_FAILURE(prepare());
	giveExpertChaosMagic();

	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::BERSERK;
	action.aimToHex(selectedTarget->getPosition());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_TRUE(selectedTarget->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
	EXPECT_TRUE(adjacentTarget->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
}

TEST_F(NewHorizonsBerserkProfileTest, V3UsesSingleSmartCreatureAimAtEveryMasteryAndCastsAuthoritatively)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(berserk->getTargetType(), spells::AimType::LOCATION)
		<< "The shared core spell definition remains unchanged for legacy worlds";
	for(int rank = MasteryLevel::NONE; rank <= MasteryLevel::EXPERT; ++rank)
		expectTargetingAtRank(rank, spells::AimType::CREATURE, true, 0);
	EXPECT_TRUE(newHorizonsMagic::berserkUsesSingleCreatureTarget(battle()->getMagicRules()));
	giveExpertChaosMagic();
	spells::BattleCast expertCast(battle(), attackerSideHero, spells::Mode::HERO, berserk);
	const auto expertMechanics = berserk->battleMechanics(&expertCast);
	EXPECT_EQ(expertMechanics->getRangeLevel(), 0);
	EXPECT_EQ(expertMechanics->getEffectLevel(), MasteryLevel::EXPERT);

	spells::BattleCast explicitMassCast(battle(), attackerSideHero, spells::Mode::HERO, berserk);
	explicitMassCast.setSpellLevel(MasteryLevel::EXPERT);
	explicitMassCast.forceMassive = true;
	const auto explicitMassMechanics = berserk->battleMechanics(&explicitMassCast);
	EXPECT_EQ(explicitMassMechanics->getTargetTypes(), std::vector<spells::AimType>{spells::AimType::NOTHING});
	EXPECT_EQ(explicitMassMechanics->getRangeLevel(), MasteryLevel::EXPERT);
	EXPECT_EQ(explicitMassMechanics->getEffectLevel(), MasteryLevel::EXPERT);
	EXPECT_TRUE(explicitMassMechanics->isMassive())
		<< "The saved-profile correction preserves an explicit event-level Mass override";

	const auto description = newHorizonsMagic::spellDescriptionForHero(attackerSideHero, berserk, MasteryLevel::EXPERT);
	EXPECT_NE(description.find("Target one enemy stack"), std::string::npos);
	EXPECT_NE(description.find("ordinary cast targets only the selected stack at every mastery rank"), std::string::npos);

	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::BERSERK;
	action.aimToUnit(selectedTarget);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_TRUE(selectedTarget->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
	EXPECT_FALSE(adjacentTarget->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
}

TEST_F(NewHorizonsBerserkProfileTest, V3RejectsFriendlyCreatureAimWithoutSpendingCast)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const int chaosMagicId = SecondarySkill::decode("new-horizons:chaosMagic");
	ASSERT_GE(chaosMagicId, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(chaosMagicId), MasteryLevel::EXPERT,
		ChangeValueMode::ABSOLUTE);
	const auto manaBefore = attackerSideHero->getManaAvailable();

	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::BERSERK;
	action.aimToUnit(friendlyTarget);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
	EXPECT_FALSE(friendlyTarget->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE));
}
