/*
 * NewHorizonsDruidConsumerTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/entities/hero/CHeroHandler.h"
#include "../../../lib/rewardable/Reward.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"

namespace
{
class NewHorizonsDruidConsumerTest : public HeroCommandFixture
{
protected:
	bool capturedPolicy = true;
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		// Keep the complete development/class-weight pair required by fresh heroes.
		// Only the specialty block is controlled by this focused fixture.
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		rules.setOverrideFlag(true);
		ASSERT_TRUE(newHorizonsHeroes::usesSkillOfferWeights(rules));
		rules.Struct().erase("druidSpecialties");
		if(capturedPolicy)
		{
			auto & specialties = rules["druidSpecialties"];
			specialties["version"].Integer() = 1;
			for(const auto key : {"highLore", "restorativeSorceress", "naturalPhilosopher"})
			{
				specialties[key]["percentPerLevel"].Integer() = std::string_view(key) == "highLore" ? 10 : 5;
				specialties[key]["maximumPercent"].Integer() = 100;
			}
			specialties["naturalPhilosopher"]["knowledgeObjectExperiencePercent"].Integer() = 25;
		}
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	void prepareHero(const char * name, int level)
	{
		startGame();
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode(name)));
		attackerSideHero->level = level;
	}
};
}

TEST_F(NewHorizonsDruidConsumerTest, MalcomScalesOnlyLearningContributionAndKeepsAdditionalPercentIndependent)
{
	ASSERT_NO_FATAL_FAILURE(prepareHero("core:malcom", 1));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:learning")),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	const auto rate = attackerSideHero->valOfBonuses(BonusType::HERO_EXPERIENCE_GAIN_PERCENT);
	EXPECT_EQ(attackerSideHero->calculateXp(1000), static_cast<TExpType>(1000 * rate / 100.0) + 5);
	EXPECT_EQ(attackerSideHero->calculateXp(1000, 50), static_cast<TExpType>(1000 * (rate + 50) / 100.0) + 5);
	EXPECT_EQ(attackerSideHero->calculateXp(0, 50), 0);
	attackerSideHero->level = 40;
	EXPECT_EQ(attackerSideHero->calculateXp(1000), static_cast<TExpType>(1000 * rate / 100.0) + 100);
}

TEST_F(NewHorizonsDruidConsumerTest, KnowledgeRiderRequiresExplicitObjectProvenanceAndMatchesPreview)
{
	ASSERT_NO_FATAL_FAILURE(prepareHero("core:malcom", 1));
	Rewardable::Reward reward;
	reward.heroExperience = 1000;
	reward.primaryExperienceReward = true;
	EXPECT_EQ(reward.calculateHeroExperience(attackerSideHero), attackerSideHero->calculateXp(1000));
	EXPECT_EQ(reward.calculateHeroExperience(attackerSideHero, true, Obj::TREASURE_CHEST),
		attackerSideHero->calculateXp(1000));
	for(const auto source : {Obj::TREE_OF_KNOWLEDGE, Obj::LEARNING_STONE})
	{
		EXPECT_EQ(reward.calculateHeroExperience(attackerSideHero, true, source),
			attackerSideHero->calculateXp(1250));
		std::vector<Component> actual, expected;
		reward.loadComponents(actual, attackerSideHero, source);
		Rewardable::Reward ordinary;
		ordinary.heroExperience = 1250;
		ordinary.primaryExperienceReward = true;
		ordinary.loadComponents(expected, attackerSideHero);
		ASSERT_EQ(actual.size(), expected.size());
		for(size_t index = 0; index < actual.size(); ++index)
		{
			EXPECT_EQ(actual[index].type, expected[index].type);
			EXPECT_EQ(actual[index].value, expected[index].value);
		}
	}
}

TEST_F(NewHorizonsDruidConsumerTest, CapturedPolicyAbsencePreservesMalcomExperience)
{
	capturedPolicy = false;
	ASSERT_NO_FATAL_FAILURE(prepareHero("core:malcom", 10));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:learning")),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	const auto rate = attackerSideHero->valOfBonuses(BonusType::HERO_EXPERIENCE_GAIN_PERCENT);
	EXPECT_EQ(attackerSideHero->calculateXp(1000), static_cast<TExpType>(1000 * rate / 100.0));
	Rewardable::Reward reward;
	reward.heroExperience = 1000;
	EXPECT_EQ(reward.calculateHeroExperience(attackerSideHero, true, Obj::LEARNING_STONE),
		attackerSideHero->calculateXp(1000));
}

TEST_F(NewHorizonsDruidConsumerTest, GemCureScalesOnlyPowerComponentAndDoesNotGrantResurrection)
{
	ASSERT_NO_FATAL_FAILURE(prepareHero("core:gem", 5));
	giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(SpellID::CURE);
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 20, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(attackerSideHero, 100);
	startBattle();
	auto * target = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(3, 5), 10);
	beginCombat();
	auto state = target->acquireState();
	int64_t damage = 400;
	state->damage(damage);
	BattleUnitsChanged change;
	change.battleID = BattleID(0);
	change.changedStacks.emplace_back(target->unitId(), UnitChanges::EOperation::UPDATE);
	change.changedStacks.back().data = state->save();
	change.changedStacks.back().healthDelta = -400;
	gameHandler->sendAndApply(change);
	const auto count = target->getCount();
	spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, SpellID(SpellID::CURE).toSpell());
	EXPECT_EQ(SpellID(SpellID::CURE).toSpell()->battleMechanics(&cast)->getEffectValue(), 62);
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::CURE;
	action.aimToUnit(target);
	const auto before = target->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_EQ(target->getAvailableHealth() - before, 62);
	EXPECT_EQ(target->getCount(), count);
}

TEST_F(NewHorizonsDruidConsumerTest, GemTentCreatureOutputLeavesMachineAndRestorationBudgetsUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepareHero("core:gem", 5));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:warMachines")),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	startBattle();
	auto * tent = addStack(BattleSide::ATTACKER, CreatureID::FIRST_AID_TENT, BattleHex(2, 5), 1);
	auto * target = addStack(BattleSide::ATTACKER, creatureByName("core:archangel"), BattleHex(3, 5), 10);
	auto * machine = addStack(BattleSide::ATTACKER, CreatureID::BALLISTA, BattleHex(4, 5), 1);
	beginCombat();
	for(auto * unit : {target, machine})
	{
		auto state = unit->acquireState();
		int64_t damage = 50;
		state->damage(damage);
		BattleUnitsChanged change;
		change.battleID = BattleID(0);
		change.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::UPDATE);
		change.changedStacks.back().data = state->save();
		change.changedStacks.back().healthDelta = -50;
		gameHandler->sendAndApply(change);
	}
	const auto ordinary = battle()->battleGetFirstAidHealingOutput(tent);
	ASSERT_GT(ordinary, 0);
	EXPECT_EQ(battle()->battleGetFirstAidCreatureHealingOutput(tent, target), ordinary + ordinary / 4);
	EXPECT_EQ(battle()->battleGetFirstAidCreatureHealingOutput(tent, machine), ordinary);
	EXPECT_EQ(battle()->battleGetFirstAidHealingOutput(tent), ordinary);
	EXPECT_EQ(battle()->battleGetBattlefieldMedicRestorationBudget(tent, target), 0);
}

TEST_F(NewHorizonsDruidConsumerTest, CapturedLearningScalingAvoidsProductOverflowAndSaturatesAtExperienceLimit)
{
	ASSERT_NO_FATAL_FAILURE(prepareHero("core:malcom", 1));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:learning")),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(attackerSideHero->valOfBonuses(BonusType::HERO_EXPERIENCE_GAIN_PERCENT), 110);
	// Tree-180-sized raw award: its 25% uplift makes the old integer product overflow.
	EXPECT_EQ(attackerSideHero->calculateXp(74'601'420'655'464'958LL), 82'434'569'824'288'777LL);
	EXPECT_EQ(attackerSideHero->calculateXp(93'251'775'819'331'197LL), 103'043'212'280'360'971LL);
	EXPECT_EQ(attackerSideHero->calculateXp(std::numeric_limits<TExpType>::max()),
		std::numeric_limits<TExpType>::max());
	EXPECT_EQ(attackerSideHero->calculateXp(1000, std::numeric_limits<int32_t>::max()), 21'474'837'575LL);
	EXPECT_EQ(attackerSideHero->calculateXp(-100), -110);
}

TEST_F(NewHorizonsDruidConsumerTest, RealKnowledgeRewardLargeLevelAwardAndMultipleComponentsUseCheckedComposition)
{
	ASSERT_NO_FATAL_FAILURE(prepareHero("core:malcom", 180));
	ASSERT_GE(LIBRARY->heroh->maxSupportedLevel(), 181u);
	attackerSideHero->exp = LIBRARY->heroh->reqExp(180);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:learning")),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(attackerSideHero->valOfBonuses(BonusType::HERO_EXPERIENCE_GAIN_PERCENT), 110);
	const auto raw = LIBRARY->heroh->reqExp(181) - LIBRARY->heroh->reqExp(180);
	ASSERT_EQ(raw, 74'601'420'655'464'958LL);
	Rewardable::Reward reward;
	reward.primaryExperienceReward = true;
	reward.heroLevel = 1;
	EXPECT_EQ(reward.calculateHeroExperience(attackerSideHero, true, Obj::TREE_OF_KNOWLEDGE),
		111'902'130'983'197'435LL);

	// A configured independent multiplier makes the level component saturate.
	// Fixed and next-level XP remain positive separate components afterward.
	auto modifier = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::HERO_EXPERIENCE_GAIN_PERCENT,
		BonusSource::OTHER, 1'000'000'000, BonusSourceID());
	modifier->valType = BonusValueType::PERCENT_TO_BASE;
	attackerSideHero->addNewBonus(modifier);
	reward.heroExperience = std::numeric_limits<int32_t>::max();
	reward.heroExperienceNextLevelPercent = 100;
	EXPECT_EQ(reward.calculateHeroExperience(attackerSideHero, true, Obj::TREE_OF_KNOWLEDGE),
		std::numeric_limits<TExpType>::max());
	reward.heroExperienceNextLevelPercent = 0;
	EXPECT_EQ(reward.calculateHeroExperience(attackerSideHero, true, Obj::TREE_OF_KNOWLEDGE),
		std::numeric_limits<TExpType>::max());
}
