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
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../server/queries/MapQueries.h"
#include "../../../server/queries/QueriesProcessor.h"

namespace
{
class NewHorizonsOffenseRankTest : public HeroCommandFixture
{
protected:
	bool guaranteedGrowth = false;
	bool acquisitionOnly = false;

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		auto rules = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
		if(guaranteedGrowth)
			for(int rank = 1; rank <= 3; ++rank)
				rules["extraGrowth"].Vector().front()["chances"].Vector()[rank].Integer() = 100;
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		if(acquisitionOnly)
			map->allowedAbilities = {offense()};
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

TEST_F(NewHorizonsOffenseRankTest, CanonicalGrowthChanceStartsOnlyAfterOffenseIsLearned)
{
	startGame();
	const auto rules = attackerSideHero->getPrimaryGrowthRules();
	const auto chance = [&](int rank)
	{
		const auto canonicalOffense = offense();
		const auto growth = newHorizonsHeroes::skillGrowthChances(rules, [rank, canonicalOffense](SecondarySkill skill)
		{
			return skill == canonicalOffense ? rank : 0;
		});
		if(rank == 0)
		{
			EXPECT_TRUE(growth.empty());
			return 0;
		}
		EXPECT_EQ(growth.size(), 1u);
		EXPECT_EQ(growth.front().skill, canonicalOffense);
		EXPECT_EQ(growth.front().attribute, PrimarySkill::ATTACK);
		return growth.front().chancePercent;
	};

	EXPECT_EQ(chance(0), 0);
	EXPECT_EQ(chance(1), 10);
	EXPECT_EQ(chance(2), 20);
	EXPECT_EQ(chance(3), 30);
}

TEST_F(NewHorizonsOffenseRankTest, SkillChosenAtCurrentLevelAffectsOnlyFollowingLevelGrowth)
{
	guaranteedGrowth = true;
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
		profile.growth[PrimarySkill(PrimarySkill::ATTACK).getNum()] + 1);
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
