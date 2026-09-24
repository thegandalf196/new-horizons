/*
 * NewHorizonsCureTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/modding/CModHandler.h"

namespace
{
constexpr auto cureKey = "core:cure";

std::shared_ptr<Bonus> spellEffect(SpellID source, BonusType type, int value,
	BonusValueType valueType = BonusValueType::ADDITIVE_VALUE,
	BonusSubtypeID subtype = BonusSubtypeID())
{
	auto result = std::make_shared<Bonus>(BonusDuration::N_TURNS, type, BonusSource::SPELL_EFFECT,
		value, BonusSourceID(source), subtype, valueType);
	result->turnsRemain = 3;
	return result;
}

class NewHorizonsCureTest : public HeroCommandFixture
{
protected:
	bool optIntoNewCure = true;
	CStack * target = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires separate native curated preset";
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
		if(!optIntoNewCure)
			rules["spells"][cureKey].Struct().erase("cureAfflictions");
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
	}

	void prepare(int spellPower = 20, const std::string & creature = "core:pikeman", int count = 100)
	{
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::CURE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 100);
		startBattle();
		removeDeployedUnits();
		target = addStack(BattleSide::ATTACKER, creatureByName(creature), BattleHex(3, 5), count);
		addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 1);
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

	void injure(int64_t damage)
	{
		auto state = target->acquireState();
		state->damage(damage);
		BattleUnitsChanged change;
		change.battleID = BattleID(0);
		change.changedStacks.emplace_back(target->unitId(), UnitChanges::EOperation::UPDATE);
		change.changedStacks.back().data = state->save();
		change.changedStacks.back().healthDelta = -damage;
		gameHandler->sendAndApply(change);
	}

	void addPoison(bool healthPenalty = false)
	{
		const auto poison = SpellID::POISON;
		target->addNewBonus(spellEffect(poison, BonusType::POISON, 30));
		if(healthPenalty)
			target->addNewBonus(spellEffect(poison, BonusType::STACK_HEALTH, -10,
				BonusValueType::PERCENT_TO_ALL));
	}

	void addDisease()
	{
		const auto disease = SpellID::DISEASE;
		target->addNewBonus(spellEffect(disease, BonusType::PRIMARY_SKILL, -2,
			BonusValueType::ADDITIVE_VALUE, BonusSubtypeID(PrimarySkill::ATTACK)));
		target->addNewBonus(spellEffect(disease, BonusType::PRIMARY_SKILL, -2,
			BonusValueType::ADDITIVE_VALUE, BonusSubtypeID(PrimarySkill::DEFENSE)));
	}

	void addMagicalConditions()
	{
		target->addNewBonus(spellEffect(SpellID::CURSE, BonusType::PRIMARY_SKILL, -1,
			BonusValueType::ADDITIVE_VALUE, BonusSubtypeID(PrimarySkill::ATTACK)));
		target->addNewBonus(spellEffect(SpellID::SLOW, BonusType::STACKS_SPEED, -1));
		target->addNewBonus(spellEffect(SpellID::BERSERK, BonusType::PRIMARY_SKILL, -1,
			BonusValueType::ADDITIVE_VALUE, BonusSubtypeID(PrimarySkill::DEFENSE)));
	}

	bool hasSource(SpellID source) const
	{
		return !target->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(source)))->empty();
	}

	BattleAction cureAction(const CStack * unit, SpellID affliction = SpellID::NONE) const
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::CURE;
		action.spellCureAffliction = affliction;
		action.aimToUnit(unit);
		return action;
	}
};
}

TEST_F(NewHorizonsCureTest, CanonicalProfileOptsIntoOnlyPoisonAndDiseaseAtFourManaEveryRank)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(rules));
	const auto & cure = rules["spells"][cureKey];
	ASSERT_EQ(cure["costs"].Vector().size(), 4u);
	for(int rank = 0; rank < 4; ++rank)
		EXPECT_EQ(newHorizonsMagic::spellCost(rules, SpellID::CURE, rank), 4);
	ASSERT_EQ(cure["cureAfflictions"].Vector().size(), 2u);
	EXPECT_EQ(cure["cureAfflictions"].Vector()[0].String(), "core:poison");
	EXPECT_EQ(cure["cureAfflictions"].Vector()[1].String(), "core:disease");
	EXPECT_TRUE(newHorizonsMagic::cureEnabled(rules, SpellID::CURE));
	EXPECT_FALSE(newHorizonsMagic::cureEnabled(rules, SpellID::SLOW));
}

TEST_F(NewHorizonsCureTest, OptedInDescriptionExplainsReworkedCure)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	const auto * spell = SpellID(SpellID::CURE).toSpell();
	ASSERT_NE(spell, nullptr);
	ASSERT_TRUE(newHorizonsMagic::cureEnabled(attackerSideHero->getMagicRules(), spell->getId()));
	const auto description = newHorizonsMagic::spellDescriptionForHero(attackerSideHero, spell, 0);

	EXPECT_NE(description.find("one friendly living stack"), std::string::npos);
	EXPECT_NE(description.find("25 + 1.5"), std::string::npos);
	EXPECT_NE(description.find("cannot resurrect casualties"), std::string::npos);
	EXPECT_NE(description.find("Poison or Disease"), std::string::npos);
	EXPECT_NE(description.find("one physical affliction"), std::string::npos);
	EXPECT_NE(description.find("does not remove magical effects"), std::string::npos);
	EXPECT_NE(description, spell->getDescriptionTranslated(0));
}

TEST_F(NewHorizonsCureTest, MissingSavedCureOptInKeepsTranslatedCoreDescription)
{
	optIntoNewCure = false;
	ASSERT_NO_FATAL_FAILURE(startGame());
	const auto * spell = SpellID(SpellID::CURE).toSpell();
	ASSERT_NE(spell, nullptr);
	ASSERT_FALSE(newHorizonsMagic::cureEnabled(attackerSideHero->getMagicRules(), spell->getId()));

	EXPECT_EQ(newHorizonsMagic::spellDescriptionForHero(attackerSideHero, spell, 0),
		spell->getDescriptionTranslated(0));
}

TEST_F(NewHorizonsCureTest, OptionalSavedRowFieldPreservesLegacyMechanicsAndRejectsInvalidGroups)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["spells"][cureKey].Struct().erase("cureAfflictions");
	EXPECT_NO_THROW(newHorizonsMagic::validateRules(rules));
	EXPECT_FALSE(newHorizonsMagic::cureEnabled(rules, SpellID::CURE));

	rules["spells"][cureKey]["cureAfflictions"].Vector().emplace_back(JsonNode("core:slow"));
	EXPECT_THROW(newHorizonsMagic::validateRules(rules), std::runtime_error);
	JsonNode nonCure(JsonPath::builtin("config/newHorizonsMagic"));
	nonCure["spells"]["core:haste"]["cureAfflictions"].Vector().emplace_back(JsonNode("core:poison"));
	EXPECT_THROW(newHorizonsMagic::validateRules(nonCure), std::runtime_error);
}

TEST_F(NewHorizonsCureTest, BattleActionSelectionRoundTripsAndCannotBeDiscardedByOldProtocol)
{
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::CURE;
	action.spellCureAffliction = SpellID::DISEASE;

	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & action);
	BattleAction restored;
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_EQ(restored.spellCureAffliction, SpellID::DISEASE);

	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_CHAIN_GATE;
	EXPECT_THROW(old.oser & action, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
}

TEST_F(NewHorizonsCureTest, HealsByTwentyFivePlusFloorOnePointFiveSpellPower)
{
	ASSERT_NO_FATAL_FAILURE(prepare(21, "core:archangel", 10));
	ASSERT_NO_FATAL_FAILURE(injure(100));
	const auto before = target->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto * spell = SpellID(SpellID::CURE).toSpell();
	spells::BattleCast parameters(battle(), attackerSideHero, spells::Mode::HERO, spell);
	EXPECT_EQ(spell->battleMechanics(&parameters)->getEffectValue(), 56);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), cureAction(target)));
	EXPECT_EQ(target->getAvailableHealth() - before, 56);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - 4);
	EXPECT_EQ(target->getCount(), 10);
}

TEST_F(NewHorizonsCureTest, FullHealthTargetCanBeCleansedBySelectingPoison)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, "core:pikeman", 1));
	addPoison();
	ASSERT_TRUE(target->getAvailableHealth() >= target->getTotalHealth());
	const auto health = target->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(target, SpellID::POISON)));
	EXPECT_FALSE(hasSource(SpellID::POISON));
	EXPECT_EQ(target->getAvailableHealth(), health);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - 4);
}

TEST_F(NewHorizonsCureTest, PoisonHealthReductionIsRemovedBeforeHealing)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, "core:pikeman", 10));
	const auto fullTotalHealth = target->getTotalHealth();
	addPoison(true);
	const auto poisonedTotalHealth = target->getTotalHealth();
	ASSERT_LT(poisonedTotalHealth, fullTotalHealth);
	// Keep all ten pikemen alive. Cure's ordinary HEAL effect cannot resurrect
	// casualties and can only fill the wound on the first surviving creature.
	ASSERT_NO_FATAL_FAILURE(injure(8));
	const auto healthBeforeCure = target->getAvailableHealth();
	ASSERT_LT(target->getAvailableHealth(), target->getTotalHealth());
	ASSERT_EQ(target->getCount(), 10);
	ASSERT_EQ(target->getMaxHealth(), 9);
	ASSERT_EQ(target->getFirstHPleft(), 2);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(target, SpellID::POISON)));
	EXPECT_FALSE(hasSource(SpellID::POISON));
	EXPECT_EQ(target->getTotalHealth(), fullTotalHealth);
	EXPECT_EQ(target->getCount(), 10);
	// Under Poison, the stack has nine full units at 9 HP and a first unit at
	// 2 HP. Removing the health penalty first restores those nine units (+9),
	// then HEAL fills the first unit by 8, to 100 HP. Reversing the order would
	// cap HEAL at 7 (9 - 2), then restore the penalty, ending at 99 instead.
	EXPECT_EQ(target->getAvailableHealth() - healthBeforeCure, 17);
}

TEST_F(NewHorizonsCureTest, SelectedDiseaseRemovesItsWholeGroupAndLeavesPoisonAlone)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, "core:pikeman", 1));
	addPoison();
	addDisease();
	const auto afflictions = newHorizonsMagic::cureAfflictions(
		battle()->getBattle()->getMagicRules(), target);
	ASSERT_EQ(afflictions.size(), 2u);
	EXPECT_EQ(afflictions[0], SpellID::POISON);
	EXPECT_EQ(afflictions[1], SpellID::DISEASE);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(target, SpellID::DISEASE)));
	EXPECT_FALSE(hasSource(SpellID::DISEASE));
	EXPECT_TRUE(hasSource(SpellID::POISON));
}

TEST_F(NewHorizonsCureTest, MagicalConditionsAreNotCureAfflictions)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addMagicalConditions();
	ASSERT_NO_FATAL_FAILURE(injure(2));
	const auto mana = attackerSideHero->getManaAvailable();

	for(const auto spell : {SpellID::CURSE, SpellID::SLOW, SpellID::BERSERK})
	{
		EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			cureAction(target, spell)));
		EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
		EXPECT_TRUE(hasSource(spell));
	}
}

TEST_F(NewHorizonsCureTest, SuccessfulCurePreservesStoneGazeAndOtherMagicalConditions)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addPoison();
	addMagicalConditions();
	target->addNewBonus(spellEffect(SpellID::STONE_GAZE, BonusType::NOT_ACTIVE, 0));
	ASSERT_NO_FATAL_FAILURE(injure(2));
	const auto mana = attackerSideHero->getManaAvailable();

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(target, SpellID::POISON)));
	EXPECT_FALSE(hasSource(SpellID::POISON));
	for(const auto spell : {SpellID::STONE_GAZE, SpellID::CURSE, SpellID::SLOW, SpellID::BERSERK})
		EXPECT_TRUE(hasSource(spell));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - 4);
}

TEST_F(NewHorizonsCureTest, StaleSelectorIsRejectedBeforeManaAndHeroActionAreSpent)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(injure(2));
	const auto mana = attackerSideHero->getManaAvailable();

	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(target, SpellID::POISON)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);

	const SpellID forged(10000);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(target, forged)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
}

TEST_F(NewHorizonsCureTest, HealOnlySelectorCannotBypassARequiredAfflictionChoice)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addPoison();
	ASSERT_NO_FATAL_FAILURE(injure(2));
	const auto mana = attackerSideHero->getManaAvailable();

	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(target)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_TRUE(hasSource(SpellID::POISON));
}

TEST_F(NewHorizonsCureTest, NonCureSpellRejectsCureSelectorBeforeSpendingMana)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(injure(2));
	attackerSideHero->addSpellToSpellbook(SpellID::HASTE);
	const auto mana = attackerSideHero->getManaAvailable();
	auto hasteAction = cureAction(target, SpellID::POISON);
	hasteAction.spell = SpellID::HASTE;
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), hasteAction));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
}


TEST_F(NewHorizonsCureTest, LegacyCureRejectsAfflictionSelectorBeforeSpendingMana)
{
	optIntoNewCure = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(injure(2));
	const auto legacyMana = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(target, SpellID::POISON)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), legacyMana);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
}

TEST_F(NewHorizonsCureTest, EnemyAndUndeadTargetsAreRejectedWithoutSpendingMana)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(injure(2)); // keeps the generic spell available before target selection
	const auto mana = attackerSideHero->getManaAvailable();
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(13, 5), 1);
	enemy->addNewBonus(spellEffect(SpellID::POISON, BonusType::POISON, 30));
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(enemy, SpellID::POISON)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_FALSE(enemy->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::POISON))))->empty());

	auto * undead = addStack(BattleSide::ATTACKER, creatureByName("core:skeleton"), BattleHex(4, 5), 1);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(undead)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
}

TEST_F(NewHorizonsCureTest, ExpertCureStaysSingleTargetAndNeverResurrects)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	const auto lightMagic = SecondarySkill::decode("new-horizons:lightMagic");
	ASSERT_GE(lightMagic, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(lightMagic), MasteryLevel::EXPERT,
		ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 20, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 100);
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(SpellID::CURE);
	startBattle();
	removeDeployedUnits();
	target = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 2);
	auto * other = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(4, 5), 1);
	addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 1);
	beginCombat();
	addPoison();
	other->addNewBonus(spellEffect(SpellID::POISON, BonusType::POISON, 30));
	const auto otherHealth = other->getAvailableHealth();
	ASSERT_NO_FATAL_FAILURE(injure(16)); // one casualty; the remaining living creature can be healed, not resurrected
	ASSERT_EQ(target->getCount(), 1);

	const auto * spell = SpellID(SpellID::CURE).toSpell();
	spells::BattleCast parameters(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&parameters);
	EXPECT_EQ(mechanics->getRangeLevel(), 0);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(target, SpellID::POISON)));
	EXPECT_EQ(target->getCount(), 1);
	EXPECT_FALSE(hasSource(SpellID::POISON));
	EXPECT_TRUE(!other->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(SpellID(SpellID::POISON))))->empty());
	EXPECT_EQ(other->getAvailableHealth(), otherHealth);
}

TEST_F(NewHorizonsCureTest, UlandSpellSpecialtyStillScalesTheHealingComponent)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	const auto ulandId = HeroTypeID::decode("core:uland");
	ASSERT_GE(ulandId, 0);
	attackerSideHero->setHeroType(HeroTypeID(ulandId));
	for(const auto & bonus : attackerSideHero->getHeroType()->specialty)
		attackerSideHero->addNewBonus(std::make_shared<Bonus>(*bonus));
	attackerSideHero->level = 7;
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 20, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 100);
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(SpellID::CURE);
	startBattle();
	removeDeployedUnits();
	target = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(3, 5), 10);
	addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 1);
	beginCombat();
	ASSERT_NO_FATAL_FAILURE(injure(100));

	const auto baseHealing = int64_t{25} + (int64_t{3} * 20) / 2;
	const auto expectedHealing = attackerSideHero->getSpellBonus(SpellID(SpellID::CURE).toSpell(),
		baseHealing, target);
	ASSERT_GT(expectedHealing, baseHealing);
	const auto before = target->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), cureAction(target)));
	EXPECT_EQ(target->getAvailableHealth() - before, expectedHealing);
}

TEST_F(NewHorizonsCureTest, SavedProfileWithoutTheOptInKeepsOriginalCureMechanics)
{
	optIntoNewCure = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * spell = SpellID(SpellID::CURE).toSpell();
	spells::BattleCast parameters(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&parameters);
	EXPECT_FALSE(mechanics->isNewHorizonsCure());
	EXPECT_FALSE(newHorizonsMagic::cureEnabled(
		battle()->getBattle()->getMagicRules(), SpellID::CURE));
}
