/*
 * NewHorizonsStartingDevelopmentTest.cpp, part of VCMI engine
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/gameState/QuestInfo.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForLobby.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
struct ExpectedStart
{
	const char * hero;
	const char * parent;
	int rank;
	const char * perk;
	const char * faction;
};
const std::array<ExpectedStart, 8> starts{{
	{"core:clancy", "new-horizons:logistics", 1, "new-horizons:logistics.pathfinding", "new-horizons:sylvanLuck"},
	{"core:piquedram", "new-horizons:logistics", 1, "new-horizons:logistics.scouting", "new-horizons:metamagic"},
	{"core:thane", "new-horizons:learning", 2, "new-horizons:learning.scholar", "new-horizons:metamagic"},
	{"core:torosar", "new-horizons:warMachines", 1, "new-horizons:warMachines.masterGunner", "new-horizons:metamagic"},
	{"core:iona", "new-horizons:learning", 1, "new-horizons:learning.scholar", "new-horizons:metamagic"},
	{"core:fiona", "new-horizons:logistics", 2, "new-horizons:logistics.scouting", "new-horizons:demonicGating"},
	{"core:ignatius", "new-horizons:battlecraft", 1, "new-horizons:battlecraft.tactics", "new-horizons:demonicGating"},
	{"core:lacus", "new-horizons:battlecraft", 2, "new-horizons:battlecraft.tactics", "new-horizons:elementalRebirth"}
}};

class NewHorizonsStartingDevelopmentTest : public TinyMapGameTest
{
protected:
	bool absent = false;
	bool legacy = false;
	bool explicitSkills = false;
	Services * gameServices() override { return LIBRARY; }
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
	}
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		if(absent)
			rules["startingSkills"].Struct().erase("startingDevelopmentProfiles");
		if(legacy)
			rules = JsonNode();
		rules.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			legacy ? JsonNode() : JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		if(legacy)
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode());
	}
	void prepare(const ExpectedStart & value = starts[0])
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(value.hero)), PlayerColor(0)).heroExperience(0);
		if(explicitSkills)
			builder.heroSecondarySkills({{SecondarySkill::OFFENCE, MasteryLevel::ADVANCED}})
				.heroSpells({SpellID::MAGIC_ARROW});
		startWithMap(std::move(builder));
		ASSERT_NE(actor(), nullptr);
	}
	CGHeroInstance * actor() const { return findHeroAt({5, 5, 0}); }
	void expectStart(const ExpectedStart & value, const CGHeroInstance & hero)
	{
		EXPECT_EQ(hero.secSkills.size(), 2);
		EXPECT_EQ(hero.getPerkSkillRank(value.parent), value.rank);
		EXPECT_EQ(hero.getPerkSkillRank(value.faction), MasteryLevel::BASIC);
		EXPECT_EQ(hero.getPerkState().selected,
			(std::vector<newHorizonsHeroes::PerkSelection>{{value.parent, value.perk}}));
		const auto projected = hero.getPerkState().project([&hero](const std::string & skill)
		{
			return hero.getPerkSkillRank(skill);
		});
		ASSERT_EQ(projected.size(), 1);
		EXPECT_TRUE(projected.front().enabled);
		EXPECT_EQ(projected.front().requiredRank, MasteryLevel::BASIC);
		EXPECT_EQ(hero.getHeroClass(), hero.getHeroType()->heroClass);
	}
};

class ApprovedEightStarts : public NewHorizonsStartingDevelopmentTest,
	public ::testing::WithParamInterface<ExpectedStart> {};

TEST_P(ApprovedEightStarts, ActualDefaultStartSelectsActiveBasicPerkAndPreservesParentRank)
{
	ASSERT_NO_FATAL_FAILURE(prepare(GetParam()));
	expectStart(GetParam(), *actor());
}
INSTANTIATE_TEST_SUITE_P(RegisteredProfiles, ApprovedEightStarts, ::testing::ValuesIn(starts),
	[](const auto & info) { return std::string(info.param.hero).substr(5); });

TEST_F(NewHorizonsStartingDevelopmentTest, ExplicitMapSkillsAndBookAreNotReplacedOrGivenProfilePerks)
{
	explicitSkills = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(actor()->getPerkSkillRank("new-horizons:offense"), MasteryLevel::ADVANCED);
	EXPECT_EQ(actor()->getPerkSkillRank(starts[0].parent), 0);
	EXPECT_TRUE(actor()->getPerkState().selected.empty());
	EXPECT_TRUE(actor()->spellbookContainsSpell(SpellID::MAGIC_ARROW));
}

TEST_F(NewHorizonsStartingDevelopmentTest, AbsentProfileKeepsExistingFactionOnlyMigration)
{
	absent = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(actor()->getPerkSkillRank(starts[0].faction), MasteryLevel::BASIC);
	EXPECT_EQ(actor()->getPerkSkillRank(starts[0].parent), 0);
	EXPECT_TRUE(actor()->getPerkState().selected.empty());
}

TEST_F(NewHorizonsStartingDevelopmentTest, CapturedLegacyKeepsOriginalStartingRoster)
{
	legacy = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(actor()->secSkills, actor()->getHeroType()->secSkillsInit);
	EXPECT_TRUE(actor()->getPerkState().selected.empty());
}

TEST_F(NewHorizonsStartingDevelopmentTest, WorldRoundtripAndRepeatedSavedInitializationDoNotRepeatSelection)
{
	ASSERT_NO_FATAL_FAILURE(prepare(starts[2]));
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	auto * loaded = restored.getMap().getHero(actor()->getHeroTypeID());
	ASSERT_NE(loaded, nullptr);
	ASSERT_EQ(loaded->getHeroTypeID(), actor()->getHeroTypeID());
	ASSERT_EQ(loaded->id, actor()->id);
	expectStart(starts[2], *loaded);
	GameRandomizer randomizer(restored);
	ASSERT_NO_THROW(loaded->initHero(randomizer));
	expectStart(starts[2], *loaded);
}

TEST_F(NewHorizonsStartingDevelopmentTest, InvalidProfilesRejectWithoutMutatingCapturedPerkState)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto original = actor()->getPerkState();
	const auto check = [&](const std::function<void(JsonNode &)> & corrupt)
	{
		auto rules = actor()->getPrimaryGrowthRules();
		corrupt(rules["startingSkills"]["startingDevelopmentProfiles"][starts[0].hero]);
		EXPECT_THROW(newHorizonsHeroes::startingDevelopmentProfile(rules, original,
			actor()->getHeroTypeID(), actor()->getHeroClass()->getId()), std::runtime_error);
		EXPECT_EQ(actor()->getPerkState().toJson(), original.toJson());
	};
	check([](JsonNode & profile) { profile["skills"].Vector()[0]["skill"].String() = "new-horizons:missingSkill"; });
	check([](JsonNode & profile) { profile["skills"].Vector()[0]["rank"].Float() = 1.5; });
	check([](JsonNode & profile)
	{
		profile["skills"].Vector()[0]["skill"].String() = "new-horizons:wisdom";
		profile["startingPerks"].Vector()[0]["skill"].String() = "new-horizons:wisdom";
		profile["startingPerks"].Vector()[0]["perk"].String() = "new-horizons:wisdom.insight";
	});
	check([](JsonNode & profile) { profile["skills"].Vector()[1]["skill"].String() = "new-horizons:metamagic"; });
	check([](JsonNode & profile) { profile["startingPerks"].Vector()[0]["perk"].String() = "new-horizons:learning.scholar"; });
	check([](JsonNode & profile) { profile["startingPerks"].Vector()[0]["perk"].String() = "new-horizons:logistics.roadmaster"; });
	auto inactive = original;
	for(auto & perk : inactive.rules["skills"][starts[0].parent]["perks"].Vector())
		if(perk["id"].String() == starts[0].perk)
			perk["effect"]["status"].String() = "planned";
	EXPECT_THROW(newHorizonsHeroes::startingDevelopmentProfile(actor()->getPrimaryGrowthRules(), inactive,
		actor()->getHeroTypeID(), actor()->getHeroClass()->getId()), std::runtime_error);
	EXPECT_EQ(actor()->getPerkState().toJson(), original.toJson());
}

TEST_F(NewHorizonsStartingDevelopmentTest, UnsupportedSettingsHeroMapWorldAndLobbyRejectBeforePrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto oldVersion = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_STARTING_DEVELOPMENT_PROFILES) - 1);
	const auto reject = [oldVersion](auto & value)
	{
		CMemorySerializer bytes;
		bytes.oser.version = oldVersion;
		EXPECT_THROW(value.serialize(bytes.oser), std::runtime_error);
		EXPECT_TRUE(bytes.extractBuffer().empty());
	};
	reject(*actor());
	reject(gameState()->getMap());
	reject(*gameState());
	LobbyStartGame lobby;
	lobby.initializedStartInfo = std::make_shared<StartInfo>(*gameState()->getStartInfo());
	lobby.initializedGameState = gameState();
	reject(lobby);
	JsonNode raw;
	raw["heroes"]["newHorizons"] = actor()->getPrimaryGrowthRules();
	GameSettings settings;
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, actor()->getPrimaryGrowthRules());
	reject(settings);
	CMemorySerializer incoming;
	incoming.oser & raw;
	incoming.iser.version = oldVersion;
	GameSettings decoded;
	EXPECT_THROW(decoded.serialize(incoming.iser), std::runtime_error);
}

TEST_F(NewHorizonsStartingDevelopmentTest, KeyPresenceAdmissionDistinguishesAbsentEmptyAndNull)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
	rules["startingSkills"].Struct().erase("startingDevelopmentProfiles");
	EXPECT_NO_THROW(newHorizonsHeroes::validateStartingDevelopmentSerialization(rules, false));
	rules["startingSkills"]["startingDevelopmentProfiles"].Struct();
	EXPECT_THROW(newHorizonsHeroes::validateStartingDevelopmentSerialization(rules, false), std::runtime_error);
	rules["startingSkills"]["startingDevelopmentProfiles"] = JsonNode();
	EXPECT_THROW(newHorizonsHeroes::validateStartingDevelopmentSerialization(rules, false), std::runtime_error);
	EXPECT_THROW(newHorizonsHeroes::validateHeroRules(rules, true), std::runtime_error);
}

TEST_F(NewHorizonsStartingDevelopmentTest, AbsentCollectionRemainsWritableInPreviousFormat)
{
	absent = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	CMemorySerializer bytes;
	bytes.oser.version = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_STARTING_DEVELOPMENT_PROFILES) - 1);
	EXPECT_NO_THROW(actor()->serialize(bytes.oser));
	EXPECT_FALSE(bytes.extractBuffer().empty());
}
}
