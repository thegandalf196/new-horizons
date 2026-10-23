/*
 * NewHorizonsAdditionalSpecialtiesTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsHeroRulesFixture.h"
#include "../mock/TinyMapGameTest.h"
#include "../../lib/GameSettings.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <limits>
using namespace newHorizonsHeroes;
namespace
{
JsonNode withPhysician(JsonNode rules)
{
	auto & table = rules["additionalSpecialties"];
	table["version"].Integer() = 1;
	table["fieldPhysician"]["healingPercentPerLevel"].Integer() = 5;
	table["fieldPhysician"]["healingMaximumPercent"].Integer() = 100;
	return rules;
}
class NewHorizonsPhysicianDefaultStartTest : public TinyMapGameTest
{
protected:
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires curated New Horizons content";
	}
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		auto rules = withPhysician(JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		rules.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
};
}
TEST(NewHorizonsAdditionalSpecialties, CapturesOnlyImplementedOptionalRowWithoutFillingAbsence)
{
	auto configured = withPhysician(testHeroRules());
	ASSERT_NO_THROW(validateHeroRules(configured, true));
	const auto captured = resolveHeroRules(configured, HeroClassID(0));
	ASSERT_TRUE(fieldPhysicianRules(captured));
	EXPECT_EQ(fieldPhysicianRules(captured)->healingPercentPerLevel, 5);
	configured["additionalSpecialties"]["fieldPhysician"]["healingPercentPerLevel"].Integer() = 9;
	EXPECT_EQ(fieldPhysicianRules(captured)->healingPercentPerLevel, 5);
	auto old = captured;
	old.Struct().erase("additionalSpecialties");
	const auto before = old;
	EXPECT_FALSE(fieldPhysicianRules(old));
	EXPECT_NO_THROW(validateResolvedHeroRules(old));
	EXPECT_EQ(old, before);
	auto noRow = captured;
	noRow["additionalSpecialties"].Struct().erase("fieldPhysician");
	EXPECT_NO_THROW(validateResolvedHeroRules(noRow));
	EXPECT_FALSE(fieldPhysicianRules(noRow));
}
TEST(NewHorizonsAdditionalSpecialties, RejectsNullMissingUnknownFutureSemanticsAndUnsafeBounds)
{
	const auto good = resolveHeroRules(withPhysician(testHeroRules()), HeroClassID(0));
	std::vector<JsonNode> malformed(7, good);
	malformed[0]["additionalSpecialties"] = JsonNode();
	malformed[1]["additionalSpecialties"]["version"].Integer() = 2;
	malformed[2]["additionalSpecialties"]["fieldPhysician"] = JsonNode();
	malformed[3]["additionalSpecialties"]["fieldPhysician"].Struct().erase("healingPercentPerLevel");
	malformed[4]["additionalSpecialties"]["fieldPhysician"]["healingPercentPerLevel"].Float() = 0.5;
	malformed[5]["additionalSpecialties"]["fieldPhysician"]["healingMaximumPercent"].Integer() = 201;
	malformed[6]["additionalSpecialties"]["ecumenicalScholar"]["version"].Integer() = 1;
	for(const auto & rules : malformed)
	{
		EXPECT_THROW(validateResolvedHeroRules(rules), std::runtime_error);
		EXPECT_THROW(fieldPhysicianRules(rules), std::runtime_error);
	}
	auto boundary = good;
	boundary["additionalSpecialties"]["fieldPhysician"]["healingMaximumPercent"].Integer() = 200;
	EXPECT_NO_THROW(validateResolvedHeroRules(boundary));
}
TEST(NewHorizonsAdditionalSpecialties, RejectsUnsupportedCodecBeforeWriterPrefixAndBeforeReaderInstallation)
{
	const auto rules = withPhysician(testHeroRules());
	GameSettings source;
	source.addOverride(EGameSettings::HEROES_NEW_HORIZONS, rules);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_SCHOOL_SPELLCRAFT_SPECIALTIES;
	EXPECT_THROW(source.serialize(old.oser), std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	CMemorySerializer current;
	ASSERT_NO_THROW(source.serialize(current.oser));
	GameSettings decoded;
	ASSERT_NO_THROW(decoded.serialize(current.iser));
	EXPECT_EQ(decoded.getValue(EGameSettings::HEROES_NEW_HORIZONS), rules);
	JsonNode raw;
	raw["heroes"]["newHorizons"] = rules;
	CMemorySerializer olderReader;
	olderReader.oser & raw;
	olderReader.iser.version = old.oser.version;
	const auto before = decoded.getValue(EGameSettings::HEROES_NEW_HORIZONS);
	EXPECT_THROW(decoded.serialize(olderReader.iser), std::runtime_error);
	EXPECT_EQ(decoded.getValue(EGameSettings::HEROES_NEW_HORIZONS), before);
}
TEST(NewHorizonsAdditionalSpecialties, GenericDescriptorsNeverAcquireRionIdentity)
{
	CGHeroInstance hero(nullptr);
	for(const auto type : {HeroTypeID::NONE, HeroTypeID::RANDOM, HeroTypeID(std::numeric_limits<int32_t>::max())})
	{
		hero.setHeroType(type);
		EXPECT_EQ(fieldPhysicianHealingPercent(&hero), 0);
		EXPECT_EQ(fieldPhysicianHealing(42, &hero), 42);
	}
}
TEST_F(NewHorizonsPhysicianDefaultStartTest, ObserveActualDefaultTentAndKeepGuardianBookAndReinitialization)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:rion")), PlayerColor(0)).heroExperience(0);
	startWithMap(std::move(builder));
	auto * hero = findHeroAt({5, 5, 0});
	ASSERT_NE(hero, nullptr);
	ASSERT_EQ(hero->getHeroType()->initialArmy.size(), 3u);
	EXPECT_EQ(hero->getHeroType()->initialArmy[1].creature, CreatureID::FIRST_AID_TENT);
	const auto chances = gameState()->getSettings().getVector(EGameSettings::HEROES_STARTING_STACKS_CHANCES);
	ASSERT_GE(chances.size(), 2u);
	EXPECT_EQ(chances[1], 88);
	const auto * tent = hero->getArt(ArtifactPosition::MACH3);
	RecordProperty("observedDefaultTentCount", tent ? 1 : 0); // Observation, not a guarantee across seeds.
	EXPECT_TRUE(hero->spellbookContainsSpell(SpellID(SpellID::decode("new-horizons:guardianSpirit"))));
	EXPECT_FALSE(hero->spellbookContainsSpell(SpellID::CURE));
	EXPECT_EQ(hero->getPerkSkillRank("new-horizons:warMachines"), MasteryLevel::BASIC);
	EXPECT_TRUE(hero->hasActivePerk("new-horizons:warMachines", "new-horizons:warMachines.surgeon"));
	const auto rules = hero->getPrimaryGrowthRules();
	GameRandomizer randomizer(*gameState());
	randomizer.setSeed(19);
	ASSERT_NO_THROW(hero->initHero(randomizer));
	EXPECT_EQ(hero->getArt(ArtifactPosition::MACH3), tent);
	EXPECT_EQ(hero->getPrimaryGrowthRules(), rules);
	const auto id = hero->id;
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	ASSERT_NE(restored.getHero(id), nullptr);
	EXPECT_EQ(restored.getHero(id)->getArt(ArtifactPosition::MACH3) != nullptr, tent != nullptr);
	EXPECT_EQ(restored.getHero(id)->getPrimaryGrowthRules(), rules);
}
