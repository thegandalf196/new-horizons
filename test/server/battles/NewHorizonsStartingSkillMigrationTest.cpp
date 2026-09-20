/*
 * NewHorizonsStartingSkillMigrationTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../mock/TinyH3MBuilder.h"
#include "../../mock/TinyMapGameTest.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/campaign/CampaignState.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"

namespace
{
SecondarySkill scopedSkill(const char * identifier)
{
	return SecondarySkill(SecondarySkill::decode(identifier));
}

void expectUniqueSkills(const CGHeroInstance * hero)
{
	std::set<si32> skills;
	for(const auto & [skill, rank] : hero->secSkills)
	{
		static_cast<void>(rank);
		EXPECT_TRUE(skills.insert(skill.getNum()).second);
	}
}
}

class NewHorizonsStartingSkillMigrationTest : public TinyMapGameTest
{
protected:
	bool canonicalRules = true;

	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			canonicalRules ? JsonNode(JsonPath::builtin("config/newHorizonsHeroes")) : JsonNode());
	}
};

TEST_F(NewHorizonsStartingSkillMigrationTest, CanonicalInitializationMigratesYogJenovaAndOrrin)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("yog")), PlayerColor(0))
		.hero({7, 5, 0}, HeroTypeID(HeroTypeID::decode("jenova")), PlayerColor(0))
		.hero({9, 5, 0}, HeroTypeID(HeroTypeID::decode("orrin")), PlayerColor(0));
	startWithMap(std::move(builder));

	auto * yog = findHeroAt({5, 5, 0});
	auto * jenova = findHeroAt({7, 5, 0});
	auto * orrin = findHeroAt({9, 5, 0});
	ASSERT_NE(yog, nullptr);
	ASSERT_NE(jenova, nullptr);
	ASSERT_NE(orrin, nullptr);

	const auto offense = scopedSkill("new-horizons:offense");
	const auto warMachines = scopedSkill("new-horizons:warMachines");
	const auto bloodrage = scopedSkill("new-horizons:bloodrage");
	const auto archery = scopedSkill("new-horizons:archery");
	const auto sylvanLuck = scopedSkill("new-horizons:sylvanLuck");
	const auto discipline = scopedSkill("new-horizons:discipline");
	const auto divineMandate = scopedSkill("new-horizons:divineMandate");

	// Yog's offence/ballistics pair proves that the second might skill is
	// replaced by the Stronghold skill after legacy identities are migrated.
	EXPECT_EQ(yog->getSecSkillLevel(offense), MasteryLevel::BASIC);
	EXPECT_EQ(yog->getSecSkillLevel(bloodrage), MasteryLevel::BASIC);
	EXPECT_EQ(yog->getSecSkillLevel(warMachines), 0);
	EXPECT_EQ(yog->getSecSkillLevel(SecondarySkill::OFFENCE), 0);
	EXPECT_EQ(yog->getSecSkillLevel(SecondarySkill::BALLISTICS), 0);

	// Jenova has one authored legacy skill, so the Rampart skill is appended
	// using the existing one-skill fallback and the authored rank is retained.
	EXPECT_EQ(jenova->getSecSkillLevel(archery), MasteryLevel::ADVANCED);
	EXPECT_EQ(jenova->getSecSkillLevel(sylvanLuck), MasteryLevel::BASIC);

	// Orrin's Leadership/Archery pair is migrated before the Castle skill
	// replaces the second might slot.
	EXPECT_EQ(orrin->getSecSkillLevel(discipline), MasteryLevel::BASIC);
	EXPECT_EQ(orrin->getSecSkillLevel(divineMandate), MasteryLevel::BASIC);
	EXPECT_EQ(orrin->getSecSkillLevel(SecondarySkill::LEADERSHIP), 0);
	EXPECT_EQ(orrin->getSecSkillLevel(SecondarySkill::ARCHERY), 0);

	expectUniqueSkills(yog);
	expectUniqueSkills(jenova);
	expectUniqueSkills(orrin);

	const auto beforeReinitialization = yog->secSkills;
	GameRandomizer randomizer(*gameState());
	yog->initHero(randomizer);
	EXPECT_EQ(yog->secSkills, beforeReinitialization);
}

TEST_F(NewHorizonsStartingSkillMigrationTest, LegacySerializedRosterIsUnchangedWhenReinitialized)
{
	canonicalRules = false;
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("orrin")), PlayerColor(0));
	startWithMap(std::move(builder));

	const auto * original = findHeroAt({5, 5, 0});
	ASSERT_NE(original, nullptr);
	const auto before = original->secSkills;
	ASSERT_FALSE(before.empty());
	EXPECT_FALSE(newHorizonsHeroes::usesRules(gameState()->getHeroDevelopmentRules()));

	const auto bytes = gameState()->saveToMemory();
	auto restored = std::make_shared<CGameState>();
	restored->preInit(LIBRARY);
	restored->loadFromMemory(bytes);
	auto * loaded = restored->getHero(original->id);
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(loaded->secSkills, before);
	EXPECT_TRUE(restored->getHeroDevelopmentRules().isNull());

	GameRandomizer randomizer(*restored);
	loaded->initHero(randomizer);
	EXPECT_EQ(loaded->secSkills, before);
	EXPECT_TRUE(loaded->getPrimaryGrowthRules().isNull());
	EXPECT_EQ(loaded->getSecSkillLevel(SecondarySkill::LEADERSHIP), MasteryLevel::BASIC);
	EXPECT_EQ(loaded->getSecSkillLevel(SecondarySkill::ARCHERY), MasteryLevel::BASIC);
}

TEST_F(NewHorizonsStartingSkillMigrationTest, OldResolvedSnapshotWithoutMigrationTableSurvivesReinitialization)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("orrin")), PlayerColor(0));
	startWithMap(std::move(builder));

	auto * original = findHeroAt({5, 5, 0});
	ASSERT_NE(original, nullptr);
	CampaignState campaign;
	auto snapshot = campaign.crossoverSerialize(original);
	ASSERT_TRUE(snapshot["primaryGrowthRules"]["startingSkills"].isStruct());
	snapshot["primaryGrowthRules"]["startingSkills"].Struct().erase("legacySkillMigrations");

	const auto restored = campaign.crossoverDeserialize(snapshot, map());
	ASSERT_NE(restored, nullptr);
	const auto beforeReinitialization = restored->secSkills;
	EXPECT_NO_THROW(newHorizonsHeroes::validateResolvedHeroRules(restored->getPrimaryGrowthRules()));

	GameRandomizer randomizer(*gameState());
	restored->initHero(randomizer);
	EXPECT_EQ(restored->secSkills, beforeReinitialization);
	EXPECT_EQ(restored->getSecSkillLevel(scopedSkill("new-horizons:discipline")), MasteryLevel::BASIC);
	EXPECT_EQ(restored->getSecSkillLevel(scopedSkill("new-horizons:divineMandate")), MasteryLevel::BASIC);
}
