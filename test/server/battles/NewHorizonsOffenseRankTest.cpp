/*
 * NewHorizonsOffenseRankTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../../lib/CSkillHandler.h"
#include "../../../lib/battle/CBattleInfoCallback.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/entities/hero/CHeroHandler.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../server/queries/MapQueries.h"
#include "../../../server/queries/QueriesProcessor.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"

namespace
{
class NewHorizonsOffenseRankTest : public HeroCommandFixture
{
protected:
	bool acquisitionOnly = false;
	bool perksEnabled = false;

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		auto rules = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		if(acquisitionOnly)
			map->allowedAbilities = {offense()};
		if(perksEnabled)
			map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
				JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	SecondarySkill offense() const
	{
		const SecondarySkill result(SecondarySkill::decode("new-horizons:offense"));
		EXPECT_NE(result, SecondarySkill::OFFENCE);
		return result;
	}

	void prepare()
	{
		startGame();
		startBattle();
	}

	CStack * preparePerk(const char * perk, int rank, int count = 10)
	{
		perksEnabled = true;
		startGame();
		const auto skill = offense();
		attackerSideHero->setSecSkillLevel(skill, rank, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({"new-horizons:offense", std::string("new-horizons:offense.") + perk});
		EXPECT_TRUE(attackerSideHero->hasActivePerk(
			"new-horizons:offense", std::string("new-horizons:offense.") + perk));
		startBattle();
		auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), count);
		const auto * ownerHero = battle()->battleGetOwnerHero(attacker);
		EXPECT_EQ(ownerHero, attackerSideHero);
		if(ownerHero)
		{
			EXPECT_TRUE(ownerHero->hasActivePerk(
				"new-horizons:offense", std::string("new-horizons:offense.") + perk));
		}
		return attacker;
	}

	CStack * addDefender(int count = 10)
	{
		return addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), count);
	}
};
}

TEST_F(NewHorizonsOffenseRankTest, CanonicalRanksScaleOnlyMeleeDamageByTenTwentyThirtyPercent)
{
	prepare();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	const auto damage = [&](bool shooting)
	{
		return battle()->calculateDmgRange(BattleAttackInfo(attacker, defender, 0, shooting)).damage.min;
	};

	attackerSideHero->setSecSkillLevel(offense(), 0, ChangeValueMode::ABSOLUTE);
	const auto meleeBase = damage(false);
	const auto rangedBase = damage(true);
	ASSERT_GT(meleeBase, 0);
	for(const auto & [rank, percent] : {std::pair{1, 10}, std::pair{2, 20}, std::pair{3, 30}})
	{
		SCOPED_TRACE(rank);
		attackerSideHero->setSecSkillLevel(offense(), rank, ChangeValueMode::ABSOLUTE);
		EXPECT_EQ(damage(false), meleeBase * (100 + percent) / 100);
		EXPECT_EQ(damage(true), rangedBase);
	}
}

TEST_F(NewHorizonsOffenseRankTest, RetaliationEstimateUsesDefendersOffenseRank)
{
	prepare();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	const BattleAttackInfo attack(attacker, defender, 0, false);
	DamageEstimation retaliationBase;
	defenderSideHero->setSecSkillLevel(offense(), 0, ChangeValueMode::ABSOLUTE);
	battle()->battleEstimateDamage(attack, &retaliationBase);
	ASSERT_GT(retaliationBase.damage.min, 0);

	DamageEstimation retaliationExpert;
	defenderSideHero->setSecSkillLevel(offense(), 3, ChangeValueMode::ABSOLUTE);
	battle()->battleEstimateDamage(attack, &retaliationExpert);
	EXPECT_EQ(retaliationExpert.damage.min, retaliationBase.damage.min * 130 / 100);
	EXPECT_EQ(retaliationExpert.damage.max, retaliationBase.damage.max * 130 / 100);
}

TEST_F(NewHorizonsOffenseRankTest, ExpandedPrimaryAttackDoesNotLeakIntoOrdinaryCreatureDamage)
{
	prepare();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("angel"), BattleHex(70), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("angel"), BattleHex(71), 100);
	attackerSideHero->setSecSkillLevel(offense(), 0, ChangeValueMode::ABSOLUTE);
	const auto before = battle()->calculateDmgRange(BattleAttackInfo(attacker, defender, 0, false)).damage;
	attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 500, ChangeValueMode::ABSOLUTE);
	const auto after = battle()->calculateDmgRange(BattleAttackInfo(attacker, defender, 0, false)).damage;
	EXPECT_EQ(after.min, before.min);
	EXPECT_EQ(after.max, before.max);
}

TEST_F(NewHorizonsOffenseRankTest, ExecutionerUsesCurrentHealthAndMatchesHypotheticBattlePreview)
{
	auto * attacker = preparePerk("executioner", 1);
	auto * defender = addDefender();
	const BattleAttackInfo attack(attacker, defender, 0, false);
	EXPECT_NE(battle()->battleGetOwner(attacker), battle()->battleGetOwner(defender));
	const auto fullHealth = battle()->calculateDmgRange(attack).damage;
	ASSERT_EQ(defender->getAvailableHealth(), defender->getTotalHealth());

	const auto savedSelections = attackerSideHero->getPerkState().selected;
	auto & mutablePerks = const_cast<newHorizonsHeroes::PerkState &>(attackerSideHero->getPerkState());
	mutablePerks.selected.clear();
	const auto fullHealthWithoutExecutioner = battle()->calculateDmgRange(attack).damage;
	mutablePerks.selected = savedSelections;
	EXPECT_EQ(fullHealth.min, fullHealthWithoutExecutioner.min);
	EXPECT_EQ(fullHealth.max, fullHealthWithoutExecutioner.max);

	int64_t wound = defender->getTotalHealth() * 61 / 100;
	defender->damage(wound);
	ASSERT_LT(defender->getAvailableHealth() * 100, defender->getTotalHealth() * 40);
	const auto wounded = battle()->calculateDmgRange(attack).damage;
	mutablePerks.selected.clear();
	const auto woundedWithoutExecutioner = battle()->calculateDmgRange(attack).damage;
	attackerSideHero->setSecSkillLevel(offense(), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	const auto woundedWithoutOffense = battle()->calculateDmgRange(attack).damage;
	attackerSideHero->setSecSkillLevel(offense(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	mutablePerks.selected = savedSelections;
	// Offense rank and Executioner are additive percentage points in the
	// authoritative damage-premium pipeline: Basic Offense is already present
	// in the control, so Executioner adds 20% of the underlying base damage.
	EXPECT_EQ(wounded.min, woundedWithoutExecutioner.min
		+ woundedWithoutOffense.min * 20 / 100);
	EXPECT_EQ(wounded.max, woundedWithoutExecutioner.max
		+ woundedWithoutOffense.max * 20 / 100);

	class OffenseEnvironment final : public Environment
	{
		std::shared_ptr<CGameState> state;
	public:
		explicit OffenseEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
		const Services * services() const override { return LIBRARY; }
		const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
		const GameCb * game() const override { return state.get(); }
	} environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle model(&environment, callback);
	auto projectedAttacker = model.getForUpdate(attacker->unitId());
	auto projectedDefender = model.getForUpdate(defender->unitId());
	const auto projected = model.calculateDmgRange(
		BattleAttackInfo(projectedAttacker.get(), projectedDefender.get(), 0, false)).damage;
	EXPECT_EQ(projected.min, wounded.min);
	EXPECT_EQ(projected.max, wounded.max);
}

TEST_F(NewHorizonsOffenseRankTest, ArmorPiercerIgnoresOnlyMeleeCreatureDefense)
{
	auto * attacker = preparePerk("armorPiercer", 2);
	auto * defender = addDefender();
	const auto melee = battle()->calculateDmgRange(BattleAttackInfo(attacker, defender, 0, false)).damage;
	const auto ranged = battle()->calculateDmgRange(BattleAttackInfo(attacker, defender, 0, true)).damage;
	ASSERT_GT(melee.min, 0);
	ASSERT_GT(ranged.min, 0);

	const auto savedSelections = attackerSideHero->getPerkState().selected;
	auto & mutablePerks = const_cast<newHorizonsHeroes::PerkState &>(attackerSideHero->getPerkState());
	mutablePerks.selected.clear();
	const auto inactiveMelee = battle()->calculateDmgRange(BattleAttackInfo(attacker, defender, 0, false)).damage;
	const auto inactiveRanged = battle()->calculateDmgRange(BattleAttackInfo(attacker, defender, 0, true)).damage;
	mutablePerks.selected = savedSelections;
	EXPECT_GT(melee.min, inactiveMelee.min);
	EXPECT_GT(melee.max, inactiveMelee.max);
	EXPECT_EQ(ranged.min, inactiveRanged.min);
	EXPECT_EQ(ranged.max, inactiveRanged.max);
}

TEST_F(NewHorizonsOffenseRankTest, BreakthroughPiercesHalfOfDefendReductionButNotOrdinaryDefense)
{
	auto * attacker = preparePerk("breakthrough", 2, 100);
	auto * defender = addDefender(100);
	const BattleAttackInfo attack(attacker, defender, 0, false);
	const auto beforeDefend = battle()->calculateDmgRange(attack).damage;

	battle()->activeStack = defender->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), defender->unitOwner(),
		BattleAction::makeDefend(defender)));
	ASSERT_TRUE(defender->defended());
	ASSERT_GT(defender->getDefense(false) - defender->getDefenseIgnoringDefensiveStance(false), 0);
	const auto withBreakthrough = battle()->calculateDmgRange(attack).damage;
	ASSERT_LT(withBreakthrough.min, beforeDefend.min);

	const auto savedSelections = attackerSideHero->getPerkState().selected;
	auto & mutablePerks = const_cast<newHorizonsHeroes::PerkState &>(attackerSideHero->getPerkState());
	mutablePerks.selected.clear();
	const auto withoutBreakthrough = battle()->calculateDmgRange(attack).damage;
	mutablePerks.selected = savedSelections;

	EXPECT_GT(withBreakthrough.min, withoutBreakthrough.min);
	EXPECT_GT(withBreakthrough.max, withoutBreakthrough.max);
	EXPECT_LT(withBreakthrough.min, beforeDefend.min);
	EXPECT_LT(withBreakthrough.max, beforeDefend.max);
}

TEST_F(NewHorizonsOffenseRankTest, BreakthroughDoesNotPierceUnrelatedTemporaryOrDamageReductions)
{
	auto * attacker = preparePerk("breakthrough", 2, 100);
	auto * defender = addDefender(100);

	// STACK_GETS_TURN is deliberately not enough to identify Defend.  This
	// synthetic primary-defense effect has the same lifetime but no Defend
	// provenance and must remain fully effective.
	defender->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::STACK_GETS_TURN, BonusType::PRIMARY_SKILL, BonusSource::OTHER, 8,
		BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE)));
	// A passive creature/Armorer-style reduction and a magical reduction are
	// both outside Breakthrough's explicit Defend scope.
	defender->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::GENERAL_DAMAGE_REDUCTION, BonusSource::OTHER, 20,
		BonusSourceID(), BonusSubtypeID(BonusCustomSubtype::damageTypeMelee)));
	defender->addNewBonus(std::make_shared<Bonus>(
		BonusDuration::PERMANENT, BonusType::GENERAL_DAMAGE_REDUCTION, BonusSource::SPELL_EFFECT, 20,
		BonusSourceID(), BonusSubtypeID(BonusCustomSubtype::damageTypeMelee)));

	const BattleAttackInfo attack(attacker, defender, 0, false);
	const auto withBreakthrough = battle()->calculateDmgRange(attack).damage;

	const auto savedSelections = attackerSideHero->getPerkState().selected;
	auto & mutablePerks = const_cast<newHorizonsHeroes::PerkState &>(attackerSideHero->getPerkState());
	mutablePerks.selected.clear();
	const auto withoutBreakthrough = battle()->calculateDmgRange(attack).damage;
	mutablePerks.selected = savedSelections;

	EXPECT_EQ(withBreakthrough.min, withoutBreakthrough.min);
	EXPECT_EQ(withBreakthrough.max, withoutBreakthrough.max);
}

TEST_F(NewHorizonsOffenseRankTest, CanonicalGrowthHasNoSkillBasedChanceRows)
{
	startGame();
	const auto rules = attackerSideHero->getPrimaryGrowthRules();
	for(const int rank : {0, 1, 2, 3})
	{
		SCOPED_TRACE(rank);
		EXPECT_TRUE(newHorizonsHeroes::skillGrowthChances(rules, [rank](SecondarySkill)
		{
			return rank;
		}).empty());
	}
}

TEST_F(NewHorizonsOffenseRankTest, SkillChosenAtCurrentLevelKeepsFixedClassGrowth)
{
	acquisitionOnly = true;
	startGame();
	const auto skill = offense();
	attackerSideHero->setSecSkillLevel(skill, 0, ChangeValueMode::ABSOLUTE);
	gameHandler->onAdvInterfaceReady(attackerSideHero->getOwner());
	const auto profile = attackerSideHero->getPrimaryGrowthView()->profile;
	const auto attackBefore = attackerSideHero->getBasePrimarySkillValue(PrimarySkill::ATTACK);
	attackerSideHero->setExperience(LIBRARY->heroh->reqExp(attackerSideHero->level + 1), ChangeValueMode::ABSOLUTE);
	gameHandler->levelUpHero(attackerSideHero);
	const auto * firstQuery = dynamic_cast<const CHeroLevelUpDialogQuery *>(
		gameHandler->queries->topQuery(attackerSideHero->getOwner()).get());
	ASSERT_NE(firstQuery, nullptr);
	EXPECT_EQ(attackerSideHero->getBasePrimarySkillValue(PrimarySkill::ATTACK) - attackBefore,
		profile.growth[PrimarySkill(PrimarySkill::ATTACK).getNum()]);
	const auto choice = std::find(firstQuery->hlu.skills.begin(), firstQuery->hlu.skills.end(), skill);
	ASSERT_NE(choice, firstQuery->hlu.skills.end());
	ASSERT_TRUE(gameHandler->queryReply(firstQuery->queryID,
		static_cast<int>(std::distance(firstQuery->hlu.skills.begin(), choice)), attackerSideHero->getOwner()));
	ASSERT_EQ(attackerSideHero->getSecSkillLevel(skill), 1);

	const auto attackAfterLearning = attackerSideHero->getBasePrimarySkillValue(PrimarySkill::ATTACK);
	attackerSideHero->setExperience(LIBRARY->heroh->reqExp(attackerSideHero->level + 1), ChangeValueMode::ABSOLUTE);
	gameHandler->levelUpHero(attackerSideHero);
	EXPECT_EQ(attackerSideHero->getBasePrimarySkillValue(PrimarySkill::ATTACK) - attackAfterLearning,
		profile.growth[PrimarySkill(PrimarySkill::ATTACK).getNum()]);
}

TEST_F(NewHorizonsOffenseRankTest, CanonicalOffenseIsDefaultAllowedAndLegallyOffered)
{
	const auto skill = offense();
	EXPECT_TRUE(LIBRARY->skillh->getDefaultAllowed().contains(skill));
	acquisitionOnly = true;
	startGame();
	attackerSideHero->setSecSkillLevel(skill, 0, ChangeValueMode::ABSOLUTE);
	EXPECT_TRUE(attackerSideHero->canLearnSkill(skill));

	gameHandler->onAdvInterfaceReady(attackerSideHero->getOwner());
	attackerSideHero->setExperience(LIBRARY->heroh->reqExp(attackerSideHero->level + 1), ChangeValueMode::ABSOLUTE);
	gameHandler->levelUpHero(attackerSideHero);
	const auto * query = dynamic_cast<const CHeroLevelUpDialogQuery *>(
		gameHandler->queries->topQuery(attackerSideHero->getOwner()).get());
	ASSERT_NE(query, nullptr);
	const auto choice = std::find(query->hlu.skills.begin(), query->hlu.skills.end(), skill);
	ASSERT_NE(choice, query->hlu.skills.end());
	ASSERT_TRUE(gameHandler->queryReply(query->queryID,
		static_cast<int>(std::distance(query->hlu.skills.begin(), choice)), attackerSideHero->getOwner()));
	EXPECT_EQ(attackerSideHero->getSecSkillLevel(skill), 1);
}
