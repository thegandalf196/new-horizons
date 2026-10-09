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
#include "../../hero/NewHorizonsHeroRulesFixture.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/spells/BattleSpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/NewHorizonsSpellAvailability.h"
#include "../../../lib/spells/NewHorizonsSorcery.h"
#include "../../../lib/battle/NewHorizonsBulwark.h"
#include "../../../lib/battle/NewHorizonsFrozen.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/spells/Problem.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/CRandomGenerator.h"

namespace
{
constexpr auto cureKey = "core:cure";

SpellID naturePoisonSpell()
{
	static const SpellID poison(SpellID::decode(std::string(newHorizonsMagic::NATURE_POISON_SPELL)));
	return poison;
}

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
	bool enableHealerPerkRules = false;
	int magicVersion = newHorizonsMagic::CURRENT_RULESET_VERSION;
	bool disableFrozenRules = false;
	int freezingTouchChance = -1;
	CStack * target = nullptr;
	CStack * poisonEnemy = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires separate native curated preset";
	}

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		if(enableHealerPerkRules)
		{
			map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
			map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
				JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		}
		JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
		if(disableFrozenRules)
			rules.Struct().erase("creatureAbilities");
		else if(freezingTouchChance >= 0)
			rules["creatureAbilities"]["freezingTouchChancePercent"].Integer() = freezingTouchChance;
		if(magicVersion != newHorizonsMagic::CURRENT_RULESET_VERSION)
		{
			rules["rulesetVersion"].Integer() = magicVersion;
			rules.Struct().erase("schoolRankPowerCoefficientPercent");
			rules.Struct().erase("spellcraftEfficiencyPercent");
		}
		if(!optIntoNewCure)
			rules["spells"][cureKey].Struct().erase("cureAfflictions");
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, rules);
	}

	void prepare(int spellPower = 20, const std::string & creature = "core:pikeman", int count = 100,
		int poisonEnemyCount = 1)
	{
		startGame();
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::CURE);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 100);
		startBattle();
		removeDeployedUnits();
		target = addStack(BattleSide::ATTACKER, creatureByName(creature), BattleHex(3, 5), count);
		poisonEnemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), poisonEnemyCount);
		beginCombat();
		ASSERT_NE(target, nullptr);
		ASSERT_NE(poisonEnemy, nullptr);
	}

	void removeDeployedUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
	}

	void injure(int64_t damage, CStack * stack = nullptr)
	{
		CStack * injuredStack = stack ? stack : target;
		auto state = injuredStack->acquireState();
		state->damage(damage);
		BattleUnitsChanged change;
		change.battleID = BattleID(0);
		change.changedStacks.emplace_back(injuredStack->unitId(), UnitChanges::EOperation::UPDATE);
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

	void addFrozen(CStack * recipient = nullptr)
	{
		auto * frozenTarget = recipient ? recipient : target;
		SetStackEffect effects;
		effects.battleID = BattleID(0);
		effects.toAdd.emplace_back(frozenTarget->unitId(), std::vector<Bonus>{
			newHorizonsFrozen::marker(BonusSourceID(creatureByName("core:iceElemental")),
				battle()->getBattle()->getRound())});
		gameHandler->sendAndApply(effects);
		ASSERT_TRUE(newHorizonsFrozen::isFrozen(*frozenTarget));
	}

	void addDisease()
	{
		const auto disease = SpellID::DISEASE;
		target->addNewBonus(spellEffect(disease, BonusType::PRIMARY_SKILL, -2,
			BonusValueType::ADDITIVE_VALUE, BonusSubtypeID(PrimarySkill::ATTACK)));
		target->addNewBonus(spellEffect(disease, BonusType::PRIMARY_SKILL, -2,
			BonusValueType::ADDITIVE_VALUE, BonusSubtypeID(PrimarySkill::DEFENSE)));
	}

	void addPhysicalPoison(int64_t baseDamage = 5, int32_t ticks = 3,
		CStack * stack = nullptr, int32_t sourceStackId = 17)
	{
		CStack * poisonedStack = stack ? stack : target;
		auto state = poisonedStack->acquireState();
		state->physicalPoisonBaseDamage = baseDamage;
		state->physicalPoisonActivationsRemaining = ticks;
		state->physicalPoisonSourceStackId = sourceStackId;
		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		UnitChanges change(poisonedStack->unitId(), UnitChanges::EOperation::UPDATE);
		change.data = state->save();
		update.changedStacks.push_back(std::move(change));
		gameHandler->sendAndApply(update);
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

	BattleAction poisonAction(const CStack * unit) const
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = naturePoisonSpell();
		action.aimToUnit(unit);
		return action;
	}

	bool advanceUntilPhysicalPoisonActivation(CStack * stack, int32_t remainingBefore)
	{
		const size_t maximumActions = battle()->stacks.size() * 8 + 8;
		for(size_t actionIndex = 0; actionIndex < maximumActions; ++actionIndex)
		{
			if(stack->physicalPoisonActivationsRemaining < remainingBefore)
				return true;

			const auto * active = battle()->battleActiveUnit();
			if(!active)
				return false;
			const BattleAction defend = BattleAction::makeDefend(active);
			if(!gameHandler->battles->makePlayerBattleAction(BattleID(0),
				battle()->sideToPlayer(active->unitSide()), defend))
				return false;
		}
		return stack->physicalPoisonActivationsRemaining < remainingBefore;
	}
};
}

TEST_F(NewHorizonsCureTest, FrozenPhysicalChoiceCleansesFullHealthAndPreservesRecipientStampAndInnateBonus)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(newHorizonsFrozen::enabled(battle()->getMagicRules()));
	ASSERT_NO_FATAL_FAILURE(addFrozen());
	const auto healthBefore = target->getAvailableHealth();
	const auto roundBefore = target->frozenLastAppliedRound();
	const auto ice = creatureByName("core:iceElemental");
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED,
		BonusSource::CREATURE_ABILITY, 1, BonusSourceID(ice)));
	const int manaBefore = attackerSideHero->getManaAvailable();
	auto action = cureAction(target);
	action.spellCurePhysicalAffliction = "frozen";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->tempOwner, action));
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*target));
	EXPECT_EQ(target->getAvailableHealth(), healthBefore);
	EXPECT_EQ(target->frozenLastAppliedRound(), roundBefore);
	EXPECT_FALSE(newHorizonsFrozen::canApply(*target, roundBefore));
	EXPECT_TRUE(target->hasBonus(Selector::type()(BonusType::STACKS_SPEED)
		.And(Selector::source(BonusSource::CREATURE_ABILITY, BonusSourceID(ice)))));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore - 4);
	// Exercise the receiver, not only the read-only eligibility predicate: Cure
	// must not permit another same-round receipt to re-freeze the recipient.
	EXPECT_THROW(battle()->addUnitBonus(target->unitId(), {
		newHorizonsFrozen::marker(BonusSourceID(ice), roundBefore)}), std::invalid_argument);
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*target));
	EXPECT_EQ(target->frozenLastAppliedRound(), roundBefore);
}

TEST_F(NewHorizonsCureTest, FrozenPhysicalChoiceLeavesPoisonAndDiseaseUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addPoison();
	addDisease();
	ASSERT_NO_FATAL_FAILURE(addFrozen());
	auto action = cureAction(target);
	action.spellCurePhysicalAffliction = "frozen";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->tempOwner, action));
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*target));
	EXPECT_TRUE(hasSource(SpellID::POISON));
	EXPECT_TRUE(hasSource(SpellID::DISEASE));
}

TEST_F(NewHorizonsCureTest, FrozenPhysicalChoiceRejectsMissingUnknownAndConflictingSelectionWithoutSpending)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto manaBefore = attackerSideHero->getManaAvailable();
	auto action = cureAction(target);
	action.spellCurePhysicalAffliction = "frozen";
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->tempOwner, action));
	ASSERT_NO_FATAL_FAILURE(addFrozen());
	action.spellCurePhysicalAffliction = "stoneGaze";
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->tempOwner, action));
	action.spellCurePhysicalAffliction = "frozen";
	action.spellCureAffliction = SpellID::POISON;
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->tempOwner, action));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*target));
}

TEST_F(NewHorizonsCureTest, FrozenPhysicalChoiceRequiresCapturedCreatureRulesButNotPositiveProcChance)
{
	freezingTouchChance = 0;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(newHorizonsFrozen::chancePercent(battle()->getMagicRules()), 0);
	ASSERT_NO_FATAL_FAILURE(addFrozen());
	auto action = cureAction(target);
	action.spellCurePhysicalAffliction = "frozen";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->tempOwner, action));
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*target));
}

TEST_F(NewHorizonsCureTest, FrozenPhysicalChoiceRejectsAbsentSavedRulesWithoutSpending)
{
	disableFrozenRules = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(addFrozen());
	const auto manaBefore = attackerSideHero->getManaAvailable();
	auto action = cureAction(target);
	action.spellCurePhysicalAffliction = "frozen";
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->tempOwner, action));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*target));
}

TEST_F(NewHorizonsCureTest, FrozenSurvivesOrdinarySorceryDispel)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->addSpellToSpellbook(SpellID::DISPEL);
	addMagicalConditions();
	ASSERT_NO_FATAL_FAILURE(addFrozen());
	auto action = cureAction(target);
	action.spell = SpellID::DISPEL;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->tempOwner, action));
	EXPECT_TRUE(newHorizonsFrozen::isFrozen(*target));
	EXPECT_FALSE(hasSource(SpellID::SLOW));
}

TEST_F(NewHorizonsCureTest, FrozenDirectMagicArrowBreaksWithoutShatterBonusAndPreservesStamp)
{
	ASSERT_NO_FATAL_FAILURE(prepare(20, "core:pikeman", 100, 100));
	attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
	ASSERT_NO_FATAL_FAILURE(addFrozen(poisonEnemy));
	const auto roundBefore = poisonEnemy->frozenLastAppliedRound();
	const auto healthBefore = poisonEnemy->getAvailableHealth();
	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	spells::BattleCast parameters(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&parameters);
	const auto ordinaryDamage = mechanics->adjustEffectValue(poisonEnemy);
	ASSERT_GT(ordinaryDamage, 0);
	auto action = cureAction(poisonEnemy);
	action.spell = SpellID::MAGIC_ARROW;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->tempOwner, action));
	EXPECT_EQ(healthBefore - poisonEnemy->getAvailableHealth(), ordinaryDamage);
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*poisonEnemy));
	EXPECT_EQ(poisonEnemy->frozenLastAppliedRound(), roundBefore);
	EXPECT_FALSE(newHorizonsFrozen::canApply(*poisonEnemy, roundBefore));
}

TEST_F(NewHorizonsCureTest, ZeroDamageMagicArrowStillThawsWithoutShatter)
{
	ASSERT_NO_FATAL_FAILURE(prepare(20, "core:pikeman", 100, 100));
	attackerSideHero->addSpellToSpellbook(SpellID::MAGIC_ARROW);
	// Canonical independent MDR caps at 95%, even for a 100% source. Use
	// the established final per-creature cap to produce a receptive zero hit.
	poisonEnemy->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::DAMAGE_RECEIVED_CAP, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_EQ(poisonEnemy->getMaxHealth() / 100, 0);
	ASSERT_NO_FATAL_FAILURE(addFrozen(poisonEnemy));
	const auto healthBefore = poisonEnemy->getAvailableHealth();
	const auto stamp = poisonEnemy->frozenLastAppliedRound();
	const auto * spell = SpellID(SpellID::MAGIC_ARROW).toSpell();
	spells::BattleCast parameters(battle(), attackerSideHero, spells::Mode::HERO, spell);
	ASSERT_EQ(spell->battleMechanics(&parameters)->adjustEffectValue(poisonEnemy), 0);
	auto action = cureAction(poisonEnemy);
	action.spell = SpellID::MAGIC_ARROW;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->tempOwner, action));
	EXPECT_EQ(poisonEnemy->getAvailableHealth(), healthBefore);
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*poisonEnemy));
	EXPECT_EQ(poisonEnemy->frozenLastAppliedRound(), stamp);
	ASSERT_EQ(server.castsOf(SpellID(SpellID::MAGIC_ARROW)).size(), 1u);
	for(const auto & injury : server.injuries)
		for(const auto & hit : injury.stacks)
			EXPECT_FALSE(hit.shattered());
}

TEST_F(NewHorizonsCureTest, HandOfFatePositiveImmediateSpillThawsWithoutShatterOrReroll)
{
	// Separate games are required: each paid spell consumes its hero action.
	// The companion zero-damage case below exercises the same sole candidate.
	ASSERT_NO_FATAL_FAILURE(prepare(100, "core:pikeman", 1000, 1000));
	const SpellID hand(SpellID::decode("new-horizons:handOfFate"));
	ASSERT_TRUE(hand.hasValue());
	attackerSideHero->addSpellToSpellbook(hand);
	ASSERT_NO_FATAL_FAILURE(addFrozen(target));
	const auto stamp = target->frozenLastAppliedRound();
	const auto friendlyHP = target->getAvailableHealth();
	const auto enemyHP = poisonEnemy->getAvailableHealth();
	CRandomGenerator expected(seed);
	for(size_t recipient = 0; recipient < battle()->battleGetAllUnits(false).size(); ++recipient)
		expected.nextInt(0, 99);
	expected.nextInt(1, 1);
	gameHandler->randomizer->setSeed(seed);
	auto action = cureAction(poisonEnemy);
	action.spell = hand;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->tempOwner, action));
	ASSERT_GT(enemyHP - poisonEnemy->getAvailableHealth(), 0);
	EXPECT_EQ(friendlyHP - target->getAvailableHealth(), (enemyHP - poisonEnemy->getAvailableHealth()) / 2);
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*target));
	EXPECT_EQ(target->frozenLastAppliedRound(), stamp);
	EXPECT_EQ(gameHandler->getRandomGenerator().nextInt(), expected.nextInt());
	ASSERT_EQ(server.castsOf(hand).size(), 1u);
	ASSERT_FALSE(server.injuries.empty());
	for(const auto & injury : server.injuries)
		for(const auto & hit : injury.stacks)
			EXPECT_FALSE(hit.shattered());
}

TEST_F(NewHorizonsCureTest, HandOfFateZeroImmediateSpillThawsWithoutShatterOrReroll)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, "core:pikeman", 1000, 1000));
	const SpellID hand(SpellID::decode("new-horizons:handOfFate"));
	ASSERT_TRUE(hand.hasValue());
	attackerSideHero->addSpellToSpellbook(hand);
	// 100% authored MDR still leaves 5% under the canonical 95% cap.
	// This supported 1% final cap instead floors to zero for a Pikeman.
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::DAMAGE_RECEIVED_CAP, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_EQ(target->getMaxHealth() / 100, 0);
	ASSERT_NO_FATAL_FAILURE(addFrozen(target));
	const auto * spell = hand.toSpell();
	spells::BattleCast parameters(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&parameters);
	const auto primaryDamage = mechanics->adjustEffectValue(poisonEnemy);
	ASSERT_GT(primaryDamage, 0);
	ASSERT_EQ(mechanics->adjustRecipientDamage(target, primaryDamage / 2), 0);
	const auto stamp = target->frozenLastAppliedRound();
	const auto friendlyHP = target->getAvailableHealth();
	const auto enemyHP = poisonEnemy->getAvailableHealth();
	CRandomGenerator expected(seed);
	for(size_t recipient = 0; recipient < battle()->battleGetAllUnits(false).size(); ++recipient)
		expected.nextInt(0, 99);
	expected.nextInt(1, 1);
	gameHandler->randomizer->setSeed(seed);
	auto action = cureAction(poisonEnemy);
	action.spell = hand;
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), attackerSideHero->tempOwner, action));
	EXPECT_GT(enemyHP - poisonEnemy->getAvailableHealth(), 0);
	EXPECT_EQ(target->getAvailableHealth(), friendlyHP);
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*target));
	EXPECT_EQ(target->frozenLastAppliedRound(), stamp);
	EXPECT_EQ(gameHandler->getRandomGenerator().nextInt(), expected.nextInt());
	ASSERT_EQ(server.castsOf(hand).size(), 1u);
	ASSERT_FALSE(server.injuries.empty());
	for(const auto & injury : server.injuries)
		for(const auto & hit : injury.stacks)
			EXPECT_FALSE(hit.shattered());
}

TEST_F(NewHorizonsCureTest, CanonicalPoisonIsAvailableAsLevelTwoNatureAtSevenMana)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(rules));
	const SpellID poison = naturePoisonSpell();
	ASSERT_TRUE(newHorizonsMagic::physicalPoisonEnabled(rules, poison));
	EXPECT_TRUE(newHorizonsMagic::spellAllowedBySavedRoster(rules, poison));
	EXPECT_FALSE(newHorizonsMagic::physicalPoisonEnabled(rules, SpellID(SpellID::POISON)));
	const auto * originalAbility = SpellID(SpellID::POISON).toSpell();
	ASSERT_NE(originalAbility, nullptr);
	EXPECT_TRUE(originalAbility->isCreatureAbility());
	EXPECT_FALSE(originalAbility->isCommonHeroSpell());
	EXPECT_EQ(newHorizonsMagic::spellLevel(rules, poison), 2);
	const auto schools = newHorizonsMagic::spellSchools(rules, poison);
	ASSERT_EQ(schools.size(), 1u);
	EXPECT_EQ(SpellSchool::encode(schools.front().getNum()), "new-horizons:nature");
	EXPECT_GT(newHorizonsMagic::factionSpellWeight(rules, FactionID::RAMPART, poison), 0);
	for(int rank = 0; rank < 4; ++rank)
		EXPECT_EQ(newHorizonsMagic::spellCost(rules, poison, rank), 7);

	constexpr std::array<int, 4> coefficients{100, 115, 130, 145};
	constexpr std::array<int64_t, 4> baseDamage{70, 77, 85, 92};
	for(int rank = 0; rank < 4; ++rank)
	{
		EXPECT_EQ(newHorizonsMagic::schoolRankPowerCoefficientPercent(rules, rank), coefficients[rank]);
		EXPECT_EQ(newHorizonsMagic::poisonBaseDamage(100, coefficients[rank]), baseDamage[rank]);
	}
	EXPECT_EQ(newHorizonsMagic::poisonBaseDamage(101, 115), 78);
}

TEST_F(NewHorizonsCureTest, OldV2RosterWithoutTheNewPoisonSpellDoesNotAcquireIt)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsMagic"));
	rules["rulesetVersion"].Integer() = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	rules.Struct().erase("schoolRankPowerCoefficientPercent");
	rules.Struct().erase("spellcraftEfficiencyPercent");
	rules["spells"].Struct().erase(std::string(newHorizonsMagic::NATURE_POISON_SPELL));
	ASSERT_NO_THROW(newHorizonsMagic::validateRules(rules));
	EXPECT_FALSE(newHorizonsMagic::spellAllowedBySavedRoster(rules, naturePoisonSpell()));
	EXPECT_FALSE(newHorizonsMagic::physicalPoisonEnabled(rules, naturePoisonSpell()));
}

TEST_F(NewHorizonsCureTest, V3PoisonUsesTheComposedSchoolAndSpellcraftCoefficient)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, "core:pikeman", 1));
	const auto poison = naturePoisonSpell();
	attackerSideHero->addSpellToSpellbook(poison);
	const int natureMagicId = SecondarySkill::decode(std::string(newHorizonsMagic::NATURE_MAGIC_SKILL));
	ASSERT_GE(natureMagicId, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(natureMagicId), MasteryLevel::BASIC,
		ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:spellcraft")),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientBasisPoints(
		battle()->getMagicRules(), attackerSideHero, poison), 12650);
	EXPECT_EQ(newHorizonsMagic::poisonBaseDamageBasisPoints(0, 12650), 20)
		<< "Spellcraft leaves Poison's fixed 20 damage unchanged";
	const int manaBefore = attackerSideHero->getManaAvailable();
	const auto healthBefore = poisonEnemy->getAvailableHealth();

	const auto * spell = poison.toSpell();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast parameters(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto mechanics = spell->battleMechanics(&parameters);
	EXPECT_EQ(mechanics->getRangeLevel(), 0);
	EXPECT_FALSE(mechanics->isMassive());

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		poisonAction(poisonEnemy)));
	EXPECT_EQ(poisonEnemy->physicalPoisonBaseDamage, 83);
	EXPECT_EQ(poisonEnemy->physicalPoisonActivationsRemaining, 3);
	EXPECT_EQ(poisonEnemy->physicalPoisonSourceStackId, -1);
	EXPECT_EQ(newHorizonsBulwark::physicalPoisonTickDamage(poisonEnemy->acquireState().get()), 83);
	EXPECT_TRUE(poisonEnemy->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(poison)))->empty());
	EXPECT_EQ(poisonEnemy->getAvailableHealth(), healthBefore);
	EXPECT_EQ(manaBefore - attackerSideHero->getManaAvailable(), 7);
}

TEST_F(NewHorizonsCureTest, V3PoisonCastTicksOnThreeRealActivationsAndExpires)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, "core:pikeman", 1000, 1000));
	const auto poison = naturePoisonSpell();
	attackerSideHero->addSpellToSpellbook(poison);
	const int natureMagicId = SecondarySkill::decode(std::string(newHorizonsMagic::NATURE_MAGIC_SKILL));
	ASSERT_GE(natureMagicId, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(natureMagicId), MasteryLevel::BASIC,
		ChangeValueMode::ABSOLUTE);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		poisonAction(poisonEnemy)));
	ASSERT_EQ(poisonEnemy->physicalPoisonBaseDamage, 77);
	ASSERT_EQ(poisonEnemy->physicalPoisonActivationsRemaining, 3);
	const auto initialHealth = poisonEnemy->getAvailableHealth();
	const auto firstInjuryIndex = server.injuries.size();
	constexpr std::array<int64_t, 3> expectedDamage{77, 115, 154};
	int64_t totalDamage = 0;

	for(size_t activation = 0; activation < expectedDamage.size(); ++activation)
	{
		ASSERT_TRUE(advanceUntilPhysicalPoisonActivation(poisonEnemy,
			static_cast<int32_t>(expectedDamage.size() - activation)));
		ASSERT_GE(server.injuries.size(), firstInjuryIndex);
		std::vector<int64_t> poisonHits;
		for(size_t injuryIndex = firstInjuryIndex; injuryIndex < server.injuries.size(); ++injuryIndex)
			for(const auto & hit : server.injuries[injuryIndex].stacks)
				if(hit.stackAttacked == poisonEnemy->unitId())
					poisonHits.push_back(hit.damageAmount);

		ASSERT_EQ(poisonHits.size(), activation + 1);
		EXPECT_EQ(poisonHits.back(), expectedDamage[activation]);
		totalDamage += expectedDamage[activation];
		EXPECT_EQ(poisonEnemy->getAvailableHealth(), initialHealth - totalDamage);
		EXPECT_EQ(poisonEnemy->physicalPoisonActivationsRemaining,
			static_cast<int32_t>(expectedDamage.size() - activation - 1));
	}

	EXPECT_EQ(poisonEnemy->physicalPoisonBaseDamage, 0);
	EXPECT_EQ(poisonEnemy->physicalPoisonActivationsRemaining, 0);
	EXPECT_EQ(poisonEnemy->physicalPoisonSourceStackId, -1);
}

TEST_F(NewHorizonsCureTest, V3EqualPotencyPoisonCastRefreshesTheActivationCount)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, "core:pikeman", 1));
	attackerSideHero->addSpellToSpellbook(naturePoisonSpell());
	const int natureMagicId = SecondarySkill::decode(std::string(newHorizonsMagic::NATURE_MAGIC_SKILL));
	ASSERT_GE(natureMagicId, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(natureMagicId), MasteryLevel::BASIC,
		ChangeValueMode::ABSOLUTE);
	addPhysicalPoison(77, 2, poisonEnemy, 15);
	ASSERT_EQ(poisonEnemy->physicalPoisonActivationsRemaining, 2);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		poisonAction(poisonEnemy)));
	EXPECT_EQ(poisonEnemy->physicalPoisonBaseDamage, 77);
	EXPECT_EQ(poisonEnemy->physicalPoisonActivationsRemaining, 3);
	EXPECT_EQ(poisonEnemy->physicalPoisonSourceStackId, -1);
}

TEST_F(NewHorizonsCureTest, V3StrongerPoisonCastRefreshesTheCurrentAffliction)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, "core:pikeman", 1));
	attackerSideHero->addSpellToSpellbook(naturePoisonSpell());
	const int natureMagicId = SecondarySkill::decode(std::string(newHorizonsMagic::NATURE_MAGIC_SKILL));
	ASSERT_GE(natureMagicId, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(natureMagicId), MasteryLevel::BASIC,
		ChangeValueMode::ABSOLUTE);
	addPhysicalPoison(70, 2, poisonEnemy, 15);
	ASSERT_EQ(poisonEnemy->physicalPoisonBaseDamage, 70);
	ASSERT_EQ(poisonEnemy->physicalPoisonActivationsRemaining, 2);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		poisonAction(poisonEnemy)));
	EXPECT_EQ(poisonEnemy->physicalPoisonBaseDamage, 77);
	EXPECT_EQ(poisonEnemy->physicalPoisonActivationsRemaining, 3);
	EXPECT_EQ(poisonEnemy->physicalPoisonSourceStackId, -1);
}

TEST_F(NewHorizonsCureTest, V3WeakerPoisonCastLeavesTheStrongerCurrentAfflictionUntouched)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, "core:pikeman", 1));
	attackerSideHero->addSpellToSpellbook(naturePoisonSpell());
	addPhysicalPoison(70, 2, poisonEnemy, 15);
	ASSERT_EQ(poisonEnemy->physicalPoisonActivationsRemaining, 2);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		poisonAction(poisonEnemy)));
	EXPECT_EQ(poisonEnemy->physicalPoisonBaseDamage, 70);
	EXPECT_EQ(poisonEnemy->physicalPoisonActivationsRemaining, 2);
	EXPECT_EQ(poisonEnemy->physicalPoisonSourceStackId, 15);
}

TEST_F(NewHorizonsCureTest, V3PoisonRejectsFriendlyAndNonLivingTargetsWithoutSpendingTheCast)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, "core:pikeman", 1));
	attackerSideHero->addSpellToSpellbook(naturePoisonSpell());
	const int manaBefore = attackerSideHero->getManaAvailable();

	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		poisonAction(target)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);

	poisonEnemy->addNewBonus(spellEffect(SpellID::POISON, BonusType::UNDEAD, 0));
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		poisonAction(poisonEnemy)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
	EXPECT_EQ(poisonEnemy->physicalPoisonActivationsRemaining, 0);
}

TEST_F(NewHorizonsCureTest, V3PoisonRejectsDeadTargetsWithoutSpendingTheCast)
{
	ASSERT_NO_FATAL_FAILURE(prepare(100, "core:pikeman", 1));
	attackerSideHero->addSpellToSpellbook(naturePoisonSpell());
	const int manaBefore = attackerSideHero->getManaAvailable();
	ASSERT_NO_FATAL_FAILURE(injure(poisonEnemy->getAvailableHealth(), poisonEnemy));
	ASSERT_FALSE(poisonEnemy->alive());

	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		poisonAction(poisonEnemy)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
	EXPECT_EQ(poisonEnemy->physicalPoisonActivationsRemaining, 0);
}

TEST_F(NewHorizonsCureTest, V2RosterCustomPoisonUsesTheFallbackTimedEffect)
{
	magicVersion = newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION;
	ASSERT_NO_FATAL_FAILURE(prepare(100, "core:pikeman", 1));
	attackerSideHero->addSpellToSpellbook(naturePoisonSpell());
	EXPECT_FALSE(newHorizonsMagic::physicalPoisonEnabled(battle()->getBattle()->getMagicRules(),
		naturePoisonSpell()));

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		poisonAction(poisonEnemy)));
	EXPECT_FALSE(poisonEnemy->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(naturePoisonSpell())))->empty());
	EXPECT_EQ(poisonEnemy->physicalPoisonBaseDamage, 0);
	EXPECT_EQ(poisonEnemy->physicalPoisonActivationsRemaining, 0);
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

TEST_F(NewHorizonsCureTest, BasicSchoolAndSpellcraftScaleOnlyCuresSpellPowerTerm)
{
	ASSERT_NO_FATAL_FAILURE(prepare(23, "core:archangel", 10));
	const SecondarySkill light(SecondarySkill::decode("new-horizons:lightMagic"));
	const SecondarySkill spellcraft(SecondarySkill::decode("new-horizons:spellcraft"));
	attackerSideHero->setSecSkillLevel(light, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setSecSkillLevel(spellcraft, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(injure(100));

	EXPECT_EQ(newHorizonsMagic::spellPowerCoefficientBasisPoints(
		battle()->getMagicRules(), attackerSideHero, SpellID::CURE), 12650);
	const auto * spell = SpellID(SpellID::CURE).toSpell();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell);
	EXPECT_EQ(spell->battleMechanics(&cast)->getEffectValue(), 68)
		<< "The fixed 25 HP remains unchanged while the 1.5 × Spell Power term uses 126.5%";

	const auto before = target->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), cureAction(target)));
	EXPECT_EQ(target->getAvailableHealth() - before, 68);
}

TEST_F(NewHorizonsCureTest, HealerPerkRaisesOnlyCuresSpellPowerHealing)
{
	enableHealerPerkRules = true;
	ASSERT_NO_FATAL_FAILURE(prepare(20, "core:archangel", 10));
	const auto lightSkill = SecondarySkill(SecondarySkill::decode("new-horizons:lightMagic"));
	attackerSideHero->setSecSkillLevel(lightSkill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	const auto * spell = SpellID(SpellID::CURE).toSpell();
	ASSERT_NE(spell, nullptr);
	spells::BattleCast before(battle(), attackerSideHero, spells::Mode::HERO, spell);
	const auto ordinaryHealing = spell->battleMechanics(&before)->getEffectValue();
	ASSERT_EQ(ordinaryHealing, 59);

	attackerSideHero->applyPerkSelection(
		{"new-horizons:lightMagic", "new-horizons:lightMagic.healer"});
	ASSERT_TRUE(attackerSideHero->hasActivePerk(
		"new-horizons:lightMagic", "new-horizons:lightMagic.healer"));
	spells::BattleCast withPerk(battle(), attackerSideHero, spells::Mode::HERO, spell);
	EXPECT_EQ(spell->battleMechanics(&withPerk)->getEffectValue(), 65)
		<< "Only the 34 HP Spell Power-derived component gains 20%; the fixed 25 stays 25";
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

TEST_F(NewHorizonsCureTest, CureSelectsAndRemovesPhysicalOnlyPoison)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, "core:pikeman", 1));
	addPhysicalPoison(5, 2);
	ASSERT_GE(target->getAvailableHealth(), target->getTotalHealth());
	const auto afflictions = newHorizonsMagic::cureAfflictions(
		attackerSideHero->getMagicRules(), target);
	ASSERT_TRUE(vstd::contains(afflictions, SpellID::POISON));
	const auto health = target->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();

	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(target, SpellID::DISEASE)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(target, SpellID::POISON)));
	EXPECT_EQ(target->physicalPoisonBaseDamage, 0);
	EXPECT_EQ(target->physicalPoisonActivationsRemaining, 0);
	EXPECT_EQ(target->physicalPoisonSourceStackId, -1);
	EXPECT_EQ(target->getAvailableHealth(), health);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - 4);
}

TEST_F(NewHorizonsCureTest, CureRemovesBothMagicalAndPhysicalPoisonWhenSelected)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, "core:pikeman", 1));
	addPoison();
	addPhysicalPoison(5, 2);
	const auto mana = attackerSideHero->getManaAvailable();

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(target, SpellID::POISON)));
	EXPECT_FALSE(hasSource(SpellID::POISON));
	EXPECT_EQ(target->physicalPoisonBaseDamage, 0);
	EXPECT_EQ(target->physicalPoisonActivationsRemaining, 0);
	EXPECT_EQ(target->physicalPoisonSourceStackId, -1);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - 4);
}

TEST_F(NewHorizonsCureTest, FullHealthSurvivorIsNotHealableForItsCasualties)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, "core:pikeman", 2));
	CStack * casualtyStack = target;
	ASSERT_NO_FATAL_FAILURE(injure(casualtyStack->getMaxHealth()));
	ASSERT_EQ(casualtyStack->getCount(), 1);
	ASSERT_EQ(casualtyStack->getFirstHPleft(), casualtyStack->getMaxHealth());
	ASSERT_LT(casualtyStack->getAvailableHealth(), casualtyStack->getTotalHealth());

	auto * injuredAlly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(4, 5), 1);
	ASSERT_NE(injuredAlly, nullptr);
	ASSERT_NO_FATAL_FAILURE(injure(2, injuredAlly));

	const auto * cure = SpellID(SpellID::CURE).toSpell();
	ASSERT_NE(cure, nullptr);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, cure);
	const auto mechanics = cure->battleMechanics(&cast);
	spells::detail::ProblemImpl problem;
	ASSERT_TRUE(mechanics->canBeCast(problem));
	const auto mana = attackerSideHero->getManaAvailable();

	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(casualtyStack)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
	EXPECT_EQ(casualtyStack->getCount(), 1);
	EXPECT_EQ(casualtyStack->getFirstHPleft(), casualtyStack->getMaxHealth());
}

TEST_F(NewHorizonsCureTest, SpellLockedPhysicalPoisonTargetIsRejectedWithoutSpendingMana)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, "core:pikeman", 1));
	addPhysicalPoison();
	auto * injuredAlly = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(4, 5), 1);
	ASSERT_NE(injuredAlly, nullptr);
	ASSERT_NO_FATAL_FAILURE(injure(2, injuredAlly));

	const SpellID spellLock(SpellID::decode(newHorizonsSorcery::SPELL_LOCK_SPELL));
	ASSERT_NE(spellLock, SpellID::NONE);
	target->addNewBonus(spellEffect(spellLock, BonusType::MAGIC_RESISTANCE, 100));
	const auto afflictions = newHorizonsMagic::cureAfflictions(
		attackerSideHero->getMagicRules(), target);
	ASSERT_TRUE(vstd::contains(afflictions, SpellID::POISON));

	const auto * cure = SpellID(SpellID::CURE).toSpell();
	ASSERT_NE(cure, nullptr);
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, cure);
	const auto mechanics = cure->battleMechanics(&cast);
	spells::detail::ProblemImpl problem;
	ASSERT_TRUE(mechanics->canBeCast(problem));
	const auto mana = attackerSideHero->getManaAvailable();

	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		cureAction(target, SpellID::POISON)));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
	EXPECT_EQ(target->physicalPoisonActivationsRemaining, 3);
}

TEST_F(NewHorizonsCureTest, OrdinaryDispelDoesNotRemovePhysicalPoison)
{
	ASSERT_NO_FATAL_FAILURE(prepare(0, "core:pikeman", 1));
	addPoison();
	addPhysicalPoison();
	attackerSideHero->addSpellToSpellbook(SpellID::DISPEL);
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::DISPEL;
	action.aimToUnit(target);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_FALSE(hasSource(SpellID::POISON));
	EXPECT_EQ(target->physicalPoisonBaseDamage, 5);
	EXPECT_EQ(target->physicalPoisonActivationsRemaining, 3);
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
