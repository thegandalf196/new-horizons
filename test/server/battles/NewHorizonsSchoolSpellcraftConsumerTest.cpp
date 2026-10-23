/*
 * NewHorizonsSchoolSpellcraftConsumerTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/battle/NewHorizonsPlague.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include <array>

namespace
{
SpellID identified(const char * name) { return SpellID(SpellID::decode(name)); }

class NewHorizonsSchoolSpellcraftConsumerTest : public HeroCommandFixture
{
protected:
	bool capturePolicy = true;
	CStack * ally = nullptr;
	CStack * enemy = nullptr;
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
		magic.setOverrideFlag(true);
		ASSERT_EQ(magic["schoolSkills"].Struct().size(), 6u);
		ASSERT_NO_THROW(newHorizonsMagic::validateRules(magic));
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magic);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		rules.setOverrideFlag(true);
		rules.Struct().erase("schoolSpellcraftSpecialties");
		if(capturePolicy)
		{
			auto & policy = rules["schoolSpellcraftSpecialties"];
			policy["version"].Integer() = 1;
			for(const auto key : {"overcaster", "darkSavant", "primalFormula", "mireHexer"})
			{
				policy[key]["percentPerLevel"].Integer() = 5;
				policy[key]["maximumPercent"].Integer() = 100;
			}
		}
		auto & individual = rules["individualSpecialties"];
		individual["version"].Integer() = 1;
		individual["graveReaper"]["percentagePointsPerLevel"].Integer() = 1;
		individual["graveReaper"]["maximumPercentagePoints"].Integer() = 20;
		individual["warlocksFormula"]["percentPerLevel"].Integer() = 5;
		individual["warlocksFormula"]["maximumPercent"].Integer() = 100;
		ASSERT_NO_THROW(newHorizonsHeroes::validateHeroRules(rules, true));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}

	void prepare(const char * hero, SpellID spell)
	{
		startGame();
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode(hero)));
		attackerSideHero->level = 20;
		for(const auto & [school, skill] : attackerSideHero->getMagicRules()["schoolSkills"].Struct())
			for(auto * caster : {attackerSideHero, defenderSideHero})
				caster->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skill.String())), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:spellcraft")), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->removeAllSpells();
		const auto required = newHorizonsMagic::requiredSchoolRank(attackerSideHero->getMagicRules(), spell);
		for(const auto skill : newHorizonsMagic::spellSchoolSkills(attackerSideHero->getMagicRules(), spell))
			attackerSideHero->setSecSkillLevel(skill, required, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(newHorizonsMagic::hasSchoolProficiency(attackerSideHero, spell));
		attackerSideHero->addSpellToSpellbook(spell);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 200, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 100);
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		ally = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 9), 1000);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(8, 5), 1000);
		ASSERT_NE(ally, nullptr);
		ASSERT_NE(enemy, nullptr);
		beginCombat();
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = ally->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
		ASSERT_TRUE(battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER, spell));
	}

	BattleAction action(SpellID spell, const CStack * target) const
	{
		BattleAction result;
		result.actionType = EActionType::HERO_SPELL;
		result.side = BattleSide::ATTACKER;
		result.spell = spell;
		result.aimToUnit(target);
		return result;
	}

	void reflectedOriginal(const char * identity)
	{
		const auto spell = identified(identity);
		ASSERT_NO_FATAL_FAILURE(prepare("core:malekith", spell));
		if(spell == SpellID::IMPLOSION)
			attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 20, ChangeValueMode::ABSOLUTE);
		enemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MAGIC_MIRROR,
			BonusSource::OTHER, 100, BonusSourceID()));
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
		const auto mechanics = cast.mechanicsForTarget({spells::Destination(enemy)});
		if(mechanics->isNewHorizonsStormOfDaggers())
		{
			ASSERT_TRUE(mechanics->setStormOfDaggersTargetCount(1));
		}
		EXPECT_EQ(mechanics->getDirectDamageSpellPowerCoefficientBasisPoints(), spell == SpellID::IMPLOSION ? 20800 : 16000);
		const auto expected = mechanics->adjustEffectValue(ally);
		spells::BattleCast standalone(battle(), attackerSideHero, spells::Mode::MAGIC_MIRROR, spell.toSpell());
		const auto unrelatedMirror = spell.toSpell()->battleMechanics(&standalone);
		EXPECT_EQ(unrelatedMirror->getDirectDamageSpellPowerCoefficientBasisPoints(), spell == SpellID::IMPLOSION ? 16900 : 13000);
		if(unrelatedMirror->isNewHorizonsStormOfDaggers())
		{
			ASSERT_TRUE(unrelatedMirror->setStormOfDaggersTargetCount(1));
		}
		ASSERT_GT(expected, unrelatedMirror->adjustEffectValue(ally));
		const auto before = ally->getAvailableHealth(), enemyBefore = enemy->getAvailableHealth();
		const auto mana = attackerSideHero->getManaAvailable();
		ASSERT_GT(expected, 0);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action(spell, enemy)));
		EXPECT_EQ(before - ally->getAvailableHealth(), expected);
		EXPECT_EQ(enemy->getAvailableHealth(), enemyBefore);
		EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - attackerSideHero->getSpellCost(spell.toSpell()));
		EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 1);
	}
};
}

TEST_F(NewHorizonsSchoolSpellcraftConsumerTest, MalekithReflectedCachedMagicArrowKeepsOriginalCoefficient)
{
	ASSERT_NO_FATAL_FAILURE(reflectedOriginal("core:magicArrow"));
}
TEST_F(NewHorizonsSchoolSpellcraftConsumerTest, MalekithReflectedDynamicStormKeepsOriginalCoefficient)
{
	ASSERT_NO_FATAL_FAILURE(reflectedOriginal("new-horizons:stormOfDaggers"));
}
TEST_F(NewHorizonsSchoolSpellcraftConsumerTest, MalekithReflectedDynamicImplosionKeepsOriginalCoefficient)
{
	ASSERT_NO_FATAL_FAILURE(reflectedOriginal("core:implosion"));
}

TEST_F(NewHorizonsSchoolSpellcraftConsumerTest, CopiedOriginalCastKeepsFactorsAndExplicitEmptyContextNeverRecaptures)
{
	const auto spell = identified("core:haste");
	ASSERT_NO_FATAL_FAILURE(prepare("core:sandro", spell));
	spells::BattleCast original(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	const auto captured = original.getSpellcraftContext();
	ASSERT_TRUE(captured && captured->factors);
	EXPECT_EQ(captured->factors->enhancedEfficiencyBasisPoints, 16000);
	attackerSideHero->level = 1;
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:spellcraft")), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	const auto copied = original;
	EXPECT_EQ(copied.getSpellcraftContext(), captured);
	const auto derived = copied.mechanicsForTarget({spells::Destination(ally)});
	EXPECT_EQ(derived->getSpellPowerCoefficientBasisPoints(), 16000);
	EXPECT_EQ(derived->getEffectDuration(), 32);
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:christian")));
	spells::BattleCast empty(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	ASSERT_TRUE(empty.getSpellcraftContext());
	EXPECT_TRUE(empty.getSpellcraftContext()->originalHero);
	EXPECT_FALSE(empty.getSpellcraftContext()->factors);
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:sandro")));
	EXPECT_EQ(empty.mechanicsForTarget({spells::Destination(ally)})->getSpellPowerCoefficientBasisPoints(), 11000);
	spells::BattleCast passive(battle(), attackerSideHero, spells::Mode::PASSIVE, spell.toSpell());
	ASSERT_TRUE(passive.getSpellcraftContext());
	EXPECT_FALSE(passive.getSpellcraftContext()->originalHero);
	EXPECT_FALSE(passive.getSpellcraftContext()->factors);
	EXPECT_EQ(passive.mechanicsForTarget({spells::Destination(ally)})->getSpellPowerCoefficientBasisPoints(), 11000);
}

TEST_F(NewHorizonsSchoolSpellcraftConsumerTest, SandroPaidHasteUsesFullSpellcraftDurationWithoutChangingFixedSpeed)
{
	const SpellID spell(SpellID::HASTE);
	ASSERT_NO_FATAL_FAILURE(prepare("core:sandro", spell));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action(spell, ally)));
	const auto bonuses = ally->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell))
		.And(Selector::type()(BonusType::STACKS_SPEED)));
	ASSERT_EQ(bonuses->size(), 1u);
	EXPECT_EQ(bonuses->front()->turnsRemain, 32);
	EXPECT_EQ(bonuses->front()->val, 3);
}

TEST_F(NewHorizonsSchoolSpellcraftConsumerTest, GirdPaidVinesChangesOnlyExistingPowerComponentsNotFixedTwoRoundSlow)
{
	const auto spell = identified("new-horizons:vengefulVines");
	ASSERT_NO_FATAL_FAILURE(prepare("core:gird", spell));
	BattleAction cast = action(spell, enemy);
	cast.target.clear();
	for(const auto hex : {BattleHex(8, 5), BattleHex(9, 5), BattleHex(10, 5)})
		cast.aimToHex(hex);
	const auto health = enemy->getAvailableHealth();
	const auto speed = enemy->getMovementRange();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), cast));
	EXPECT_EQ(health - enemy->getAvailableHealth(), 20 + 11LL * 200 * 160 / (10 * 100));
	const auto slow = enemy->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell))
		.And(Selector::type()(BonusType::STACKS_MOVEMENT_RANGE)));
	ASSERT_EQ(slow->size(), 1u);
	EXPECT_EQ(slow->front()->val, -2);
	EXPECT_EQ(slow->front()->turnsRemain, 2);
	EXPECT_EQ(enemy->getMovementRange(), speed - 2);
}

TEST_F(NewHorizonsSchoolSpellcraftConsumerTest, ZydarDelayedWallSnapshotsDamageOnceAndKeepsFixedDuration)
{
	const SpellID spell(SpellID::FIRE_WALL);
	ASSERT_NO_FATAL_FAILURE(prepare("core:zydar", spell));
	spells::BattleCast preview(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	const auto mechanics = spell.toSpell()->battleMechanics(&preview);
	EXPECT_EQ(mechanics->getSpellPowerCoefficientBasisPoints(), 14950);
	EXPECT_EQ(mechanics->getDamageSpellPowerCoefficientBasisPoints(), 18400);
	const int64_t expected = 40 + 10LL * 200 * 115 * 160 / (10 * 100 * 100);
	BattleAction cast;
	cast.actionType = EActionType::HERO_SPELL;
	cast.side = BattleSide::ATTACKER;
	cast.spell = spell;
	cast.aimToHex(BattleHex(70));
	cast.spellFireWallDirection = BattleHex::RIGHT;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), cast));
	ASSERT_EQ(battle()->obstacles.size(), 1u);
	const auto * wall = dynamic_cast<const SpellCreatedObstacle *>(battle()->obstacles.front().get());
	ASSERT_NE(wall, nullptr);
	EXPECT_TRUE(wall->damageSnapshot);
	EXPECT_EQ(wall->minimalDamage, expected);
	EXPECT_EQ(wall->turnsRemaining, 3);
	attackerSideHero->level = 0;
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:spellcraft")), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	auto * victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(70), 1000);
	ASSERT_NE(victim, nullptr);
	const auto health = victim->getAvailableHealth();
	ASSERT_TRUE(battle()->handleObstacleTriggersForUnit(*gameHandler->spellEnv, *victim));
	EXPECT_EQ(health - victim->getAvailableHealth(), expected);
}

TEST_F(NewHorizonsSchoolSpellcraftConsumerTest, StygPlagueStoresEnhancedTickAndWorldSavePreservesPolicy)
{
	const auto spell = identified("new-horizons:plague");
	ASSERT_NO_FATAL_FAILURE(prepare("core:styg", spell));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action(spell, enemy)));
	const auto markers = enemy->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spell))
		.And(Selector::type()(BonusType::COMBAT_EVENT_TRIGGER)));
	ASSERT_EQ(markers->size(), 1u);
	EXPECT_EQ(markers->front()->val, 25 + 8LL * 200 * 115 * 160 / (10 * 100 * 100));
	const auto heroID = attackerSideHero->id;
	const auto capturedRules = attackerSideHero->getPrimaryGrowthRules();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	ASSERT_NE(restored.getHero(heroID), nullptr);
	EXPECT_EQ(restored.getHero(heroID)->getPrimaryGrowthRules(), capturedRules);
	EXPECT_TRUE(newHorizonsHeroes::schoolSpellcraftRules(restored.getHero(heroID)->getPrimaryGrowthRules(),
		newHorizonsHeroes::SchoolSpellcraftKind::MIRE_HEXER));
}

TEST_F(NewHorizonsSchoolSpellcraftConsumerTest, HistoricalAbsenceSurvivesWorldRoundTripAndDoesNotAcquireInstalledPolicy)
{
	capturePolicy = false;
	ASSERT_NO_FATAL_FAILURE(prepare("core:sandro", SpellID::HASTE));
	const auto heroID = attackerSideHero->id;
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * hero = restored.getHero(heroID);
	ASSERT_NE(hero, nullptr);
	EXPECT_FALSE(newHorizonsHeroes::schoolSpellcraftRules(hero->getPrimaryGrowthRules(), newHorizonsHeroes::SchoolSpellcraftKind::DARK_SAVANT));
	EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientBasisPoints(hero->getMagicRules(), hero, SpellID::HASTE), 13000);
}

TEST_F(NewHorizonsSchoolSpellcraftConsumerTest, SavedSchoolMembershipMatchesEitherOnceAndZeroSpellcraftIsInert)
{
	const SpellID spell(SpellID::FIRE_WALL);
	ASSERT_NO_FATAL_FAILURE(prepare("core:zydar", spell));
	auto saved = attackerSideHero->getMagicRules();
	auto & schools = saved["spells"]["core:fireWall"]["schools"].Vector();
	schools = {JsonNode("new-horizons:chaos"), JsonNode("new-horizons:havoc")};
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(saved));
	EXPECT_EQ(newHorizonsMagic::damageSpellPowerCoefficientBasisPoints(saved, attackerSideHero, spell), 18400);
	EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientBasisPoints(saved, attackerSideHero, spell), 14950);
	schools = {JsonNode("new-horizons:nature")};
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(saved));
	EXPECT_EQ(newHorizonsMagic::damageSpellPowerCoefficientBasisPoints(saved, attackerSideHero, spell), 13000);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:spellcraft")), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	schools = {JsonNode("new-horizons:havoc")};
	EXPECT_EQ(newHorizonsMagic::damageSpellPowerCoefficientBasisPoints(saved, attackerSideHero, spell), 11500);
}

TEST_F(NewHorizonsSchoolSpellcraftConsumerTest, SandroPaidReanimateUsesCapturedHeroCoefficientButPassiveStaysOrdinary)
{
	const auto spell = identified("new-horizons:reanimate");
	ASSERT_NO_FATAL_FAILURE(prepare("core:sandro", spell));
	auto * passiveTarget = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(4, 9), 1000);
	ASSERT_NE(passiveTarget, nullptr);
	for(const auto * unit : {ally, passiveTarget})
	{
		auto state = unit->acquireState();
		int64_t damage = 6000;
		state->damage(damage, false);
		UnitChanges change(unit->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		change.healthDelta = -6000;
		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		update.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(update);
		ASSERT_EQ(unit->getAvailableHealth(), 4000);
	}
	// Advanced Shadow130%, Expert Spellcraft160% for Sandro versus ordinary130%.
	const int64_t heroPool = 220 + 5LL * 200 * 130 * 160 / (100 * 100);
	const int64_t passivePool = 220 + 5LL * 200 * 130 * 130 / (100 * 100);
	ASSERT_EQ(heroPool, 2300);
	ASSERT_EQ(passivePool, 1910);
	spells::BattleCast heroCast(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
	EXPECT_EQ(heroCast.mechanicsForTarget({spells::Destination(ally)})->getEffectValue(), heroPool);
	spells::BattleCast passive(battle(), attackerSideHero, spells::Mode::PASSIVE, spell.toSpell());
	EXPECT_EQ(passive.mechanicsForTarget({spells::Destination(passiveTarget)})->getEffectValue(), passivePool);
	const auto mana = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action(spell, ally)));
	EXPECT_EQ(ally->getAvailableHealth(), 4000 + heroPool);
	EXPECT_EQ(passiveTarget->getAvailableHealth(), 4000);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - 16);
	passive.cast(gameHandler->spellcastEnvironment(), {spells::Destination(passiveTarget)});
	EXPECT_EQ(passiveTarget->getAvailableHealth(), 4000 + passivePool);
	EXPECT_EQ(ally->getAvailableHealth(), 4000 + heroPool);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - 16);
}

TEST_F(NewHorizonsSchoolSpellcraftConsumerTest, SchoolIconsRequireTheirCapturedPolicy)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	const auto & development = attackerSideHero->getPrimaryGrowthRules();
	auto historical = development;
	historical.Struct().erase("schoolSpellcraftSpecialties");
	constexpr std::array<std::pair<const char *, const char *>, 4> rows{{
		{"core:zydar", "ZydarOvercaster"}, {"core:sandro", "SandroDarkSavant"},
		{"core:gird", "GirdPrimalFormula"}, {"core:styg", "StygMireHexer"}}};
	for(const auto & [hero, artwork] : rows)
	{
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode(hero)));
		newHorizonsHeroes::SpecialtyPresentation current;
		ASSERT_TRUE(newHorizonsHeroes::resolveSchoolSpellcraftPresentation(*attackerSideHero->getHeroType(),
			development, attackerSideHero->getMagicRules(), attackerSideHero, current));
		EXPECT_EQ(current.artwork, std::optional<std::string>("newHorizons/heroSpecialties/" + std::string(artwork)));
		newHorizonsHeroes::SpecialtyPresentation old;
		EXPECT_FALSE(newHorizonsHeroes::resolveSchoolSpellcraftPresentation(*attackerSideHero->getHeroType(),
			historical, attackerSideHero->getMagicRules(), attackerSideHero, old));
		EXPECT_FALSE(old.artwork);
	}
}

TEST_F(NewHorizonsSchoolSpellcraftConsumerTest, OmittedHistoricalEfficiencyDoesNotAdvertiseInactiveCurrentBonus)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:sandro", SpellID::HASTE));
	auto magic = attackerSideHero->getMagicRules();
	newHorizonsHeroes::SpecialtyPresentation ordinary;
	ASSERT_TRUE(newHorizonsHeroes::resolveSchoolSpellcraftPresentation(*attackerSideHero->getHeroType(),
		attackerSideHero->getPrimaryGrowthRules(), magic, attackerSideHero, ordinary));
	EXPECT_NE(ordinary.description.find("Current increase to the Spellcraft bonus: +100%."), std::string::npos);
	magic.Struct().erase("spellcraftEfficiencyPercent");
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(magic));
	ASSERT_EQ(newHorizonsMagic::spellcraftEfficiencyPercent(magic, MasteryLevel::EXPERT), 100);
	newHorizonsHeroes::SpecialtyPresentation historical;
	ASSERT_TRUE(newHorizonsHeroes::resolveSchoolSpellcraftPresentation(*attackerSideHero->getHeroType(),
		attackerSideHero->getPrimaryGrowthRules(), magic, attackerSideHero, historical));
	EXPECT_EQ(historical.name, "Dark Savant");
	EXPECT_NE(historical.description.find("Spellcraft provides no numerical bonus under this game's rules."), std::string::npos);
	EXPECT_EQ(historical.description.find("Current increase"), std::string::npos);
}
