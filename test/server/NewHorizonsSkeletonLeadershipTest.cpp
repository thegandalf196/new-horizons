/*
 * Part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/entities/hero/NewHorizonsCapabilityRules.h"
#include "../../lib/entities/hero/NewHorizonsNecromancy.h"
#include "../../lib/entities/hero/NewHorizonsNecromancyPolicy.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapping/CMap.h"

namespace
{
class NewHorizonsSkeletonLeadershipTest : public TinyMapGameTest,
	public ::testing::WithParamInterface<bool>
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		if(!GetParam())
			perks.Struct().erase(newHorizonsNecromancy::POLICY_KEY);
		perks.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
	}
	void startScenario()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.town({12, 12, 0}, FactionID::NECROPOLIS, PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:isra")), PlayerColor(0))
			.town({26, 26, 0}, FactionID::CASTLE, PlayerColor(1));
		startWithMap(std::move(builder));
		hero = findHeroByOwner(PlayerColor(0));
		ASSERT_TRUE(hero);
		ASSERT_EQ(newHorizonsNecromancy::policy(hero->getPerkState().rules).has_value(), GetParam());
	}
	void rank(int value)
	{
		hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(newHorizonsNecromancy::SKILL_ID)),
			value, ChangeValueMode::ABSOLUTE);
	}
	void select(const char * id)
	{
		hero->applyPerkSelection({newHorizonsNecromancy::SKILL_ID, id});
	}
	std::array<CreatureID, 2> skeletons() const
	{
		return {CreatureID(CreatureID::SKELETON), CreatureID(CreatureID::decode("core:skeletonWarrior"))};
	}
	void preparePerks(const char * expert)
	{
		rank(3);
		select(newHorizonsNecromancy::DARK_CONVERSION_ID);
		select(newHorizonsNecromancy::SOUL_HARVESTER_ID);
		select(expert);
	}
	CGHeroInstance * hero = nullptr;
};
}

TEST_P(NewHorizonsSkeletonLeadershipTest, RankChangesOnlySkeletonFamilyRequirements)
{
	ASSERT_NO_FATAL_FAILURE(startScenario());
	const auto leadership = hero->getLeadershipCapacity();
	ASSERT_TRUE(leadership);
	const auto baseLeadership = leadership->capacity;
	const CreatureID zombie(CreatureID::decode("core:zombie"));
	const auto zombieBefore = hero->getLeadershipSlotCapacity(zombie);
	ASSERT_TRUE(zombieBefore);
	for(int value = 0; value <= 3; ++value)
	{
		rank(value);
		const int expected = GetParam() ? std::array{45, 23, 18, 14}[value] : 45;
		for(const auto creature : skeletons())
		{
			ASSERT_TRUE(newHorizonsNecromancy::isSkeletonFamily(creature));
			const auto slot = hero->getLeadershipSlotCapacity(creature);
			ASSERT_TRUE(slot);
			EXPECT_EQ(slot->requirement, expected);
			EXPECT_EQ(slot->leadership, baseLeadership);
			EXPECT_EQ(slot->maximum, baseLeadership / expected);
			EXPECT_TRUE(slot->accepts(slot->maximum));
			EXPECT_FALSE(slot->accepts(slot->maximum + 1));
		}
		EXPECT_EQ(hero->getLeadershipSlotCapacity(zombie)->requirement, zombieBefore->requirement);
		EXPECT_EQ(hero->getLeadershipSlotCapacity(zombie)->maximum, zombieBefore->maximum);
	}
	EXPECT_FALSE(newHorizonsNecromancy::isSkeletonFamily(zombie));
	EXPECT_FALSE(newHorizonsNecromancy::isSkeletonFamily(CreatureID::NONE));
}

TEST_P(NewHorizonsSkeletonLeadershipTest, LordOfTheDeadOverridesRatherThanStacksDiscounts)
{
	ASSERT_NO_FATAL_FAILURE(startScenario());
	ASSERT_NO_FATAL_FAILURE(preparePerks(newHorizonsNecromancy::LORD_OF_THE_DEAD_ID));
	for(const auto creature : skeletons())
	{
		const auto slot = hero->getLeadershipSlotCapacity(creature);
		ASSERT_TRUE(slot);
		EXPECT_EQ(newHorizonsNecromancy::skeletonLeadershipReductionPercent(hero, creature), GetParam() ? 90 : 0);
		EXPECT_EQ(slot->requirement, GetParam() ? 5 : 45);
		EXPECT_EQ(slot->maximum, slot->leadership / slot->requirement);
	}
}

TEST_P(NewHorizonsSkeletonLeadershipTest, FlatLeadershipBonusUsesTheSameDiscountedRequirement)
{
	ASSERT_NO_FATAL_FAILURE(startScenario());
	rank(1);
	const auto before = hero->getLeadershipSlotCapacity(skeletons().front());
	ASSERT_TRUE(before);
	hero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::LEADERSHIP, BonusSource::OTHER, 17, BonusSourceID()));
	for(const auto creature : skeletons())
	{
		const auto after = hero->getLeadershipSlotCapacity(creature);
		ASSERT_TRUE(after);
		EXPECT_EQ(after->leadership, before->leadership + 17);
		EXPECT_EQ(after->requirement, before->requirement);
		EXPECT_EQ(after->maximum, (before->leadership + 17) / before->requirement);
	}
}

TEST_P(NewHorizonsSkeletonLeadershipTest, PassivesFollowCapturedPolicyCurrentRankAndCreatureFamily)
{
	ASSERT_NO_FATAL_FAILURE(startScenario());
	ASSERT_NO_FATAL_FAILURE(preparePerks(newHorizonsNecromancy::CORPSE_PRESERVATION_ID));
	for(const auto creature : skeletons())
	{
		EXPECT_EQ(newHorizonsNecromancy::bonewardMagicalReductionPercent(hero, creature), GetParam() ? 30 : 0);
		EXPECT_EQ(newHorizonsNecromancy::deathMarchSpeed(hero, creature), GetParam() ? 2 : 0);
		EXPECT_EQ(newHorizonsNecromancy::tombGuardAdditionalRetaliations(hero, creature), GetParam() ? 1 : 0);
	}
	const CreatureID zombie(CreatureID::decode("core:zombie"));
	EXPECT_EQ(newHorizonsNecromancy::bonewardMagicalReductionPercent(hero, zombie), 0);
	EXPECT_EQ(newHorizonsNecromancy::deathMarchSpeed(hero, zombie), 0);
	EXPECT_EQ(newHorizonsNecromancy::tombGuardAdditionalRetaliations(hero, zombie), 0);
	EXPECT_EQ(newHorizonsNecromancy::deathMarchSpeed(nullptr, skeletons().front()), 0);
	rank(0);
	for(const auto creature : skeletons())
	{
		EXPECT_EQ(newHorizonsNecromancy::bonewardMagicalReductionPercent(hero, creature), 0);
		EXPECT_EQ(newHorizonsNecromancy::deathMarchSpeed(hero, creature), 0);
		EXPECT_EQ(newHorizonsNecromancy::tombGuardAdditionalRetaliations(hero, creature), 0);
		EXPECT_EQ(hero->getLeadershipSlotCapacity(creature)->requirement, 45);
	}
}

TEST_P(NewHorizonsSkeletonLeadershipTest, SaveLoadRetainsCapturedPolicyAndEffectiveCosts)
{
	ASSERT_NO_FATAL_FAILURE(startScenario());
	ASSERT_NO_FATAL_FAILURE(preparePerks(newHorizonsNecromancy::LORD_OF_THE_DEAD_ID));
	const auto captured = hero->getPerkState().rules;
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	const auto * loaded = restored.getMap().getHero(hero->getHeroTypeID());
	ASSERT_TRUE(loaded);
	EXPECT_EQ(loaded->getPerkState().rules, captured);
	EXPECT_EQ(newHorizonsNecromancy::policy(loaded->getPerkState().rules).has_value(), GetParam());
	for(const auto creature : skeletons())
	{
		const auto slot = loaded->getLeadershipSlotCapacity(creature);
		ASSERT_TRUE(slot);
		EXPECT_EQ(slot->requirement, GetParam() ? 5 : 45);
		EXPECT_EQ(slot->maximum, hero->getLeadershipSlotCapacity(creature)->maximum);
	}
}

TEST_P(NewHorizonsSkeletonLeadershipTest, GraveplateProtectsOnlySkeletonsUnderItsActiveCapturedPerk)
{
	ASSERT_NO_FATAL_FAILURE(startScenario());
	rank(3);
	select(newHorizonsNecromancy::DARK_CONVERSION_ID);
	select(newHorizonsNecromancy::DEATH_LORD_ID);
	select(newHorizonsNecromancy::CORPSE_PRESERVATION_ID);
	for(const auto creature : skeletons())
		EXPECT_EQ(newHorizonsNecromancy::graveplatePhysicalReductionPercent(hero, creature), GetParam() ? 20 : 0);
	EXPECT_EQ(newHorizonsNecromancy::graveplatePhysicalReductionPercent(hero,
		CreatureID(CreatureID::decode("core:zombie"))), 0);
	EXPECT_EQ(newHorizonsNecromancy::graveplatePhysicalReductionPercent(nullptr, skeletons().front()), 0);
	EXPECT_EQ(newHorizonsNecromancy::graveKnowledgeRecoveryPercent(hero), 0);
	rank(1);
	for(const auto creature : skeletons())
		EXPECT_EQ(newHorizonsNecromancy::graveplatePhysicalReductionPercent(hero, creature), 0);
}

TEST_P(NewHorizonsSkeletonLeadershipTest, GraveKnowledgeRecoveryUsesItsOwnCapturedPerkAndCurrentRank)
{
	ASSERT_NO_FATAL_FAILURE(startScenario());
	rank(3);
	select(newHorizonsNecromancy::DARK_CONVERSION_ID);
	select(newHorizonsNecromancy::GRAVE_KNOWLEDGE_ID);
	select(newHorizonsNecromancy::CORPSE_PRESERVATION_ID);
	EXPECT_EQ(newHorizonsNecromancy::graveKnowledgeRecoveryPercent(hero), GetParam() ? 20 : 0);
	EXPECT_EQ(newHorizonsNecromancy::graveKnowledgeRecoveryPercent(nullptr), 0);
	for(const auto creature : skeletons())
		EXPECT_EQ(newHorizonsNecromancy::graveplatePhysicalReductionPercent(hero, creature), 0);
	rank(1);
	EXPECT_EQ(newHorizonsNecromancy::graveKnowledgeRecoveryPercent(hero), 0);
	rank(2);
	EXPECT_EQ(newHorizonsNecromancy::graveKnowledgeRecoveryPercent(hero), GetParam() ? 20 : 0);
}

INSTANTIATE_TEST_SUITE_P(CapturedPolicies, NewHorizonsSkeletonLeadershipTest, ::testing::Bool());
