/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../PerkStateFixture.h"
#include "../mock/TinyMapGameTest.h"
#include "../../lib/entities/hero/HeroStartingProjection.h"
#include "../../lib/entities/hero/NewHorizonsStartingPerks.h"
#include "../../lib/entities/hero/CHeroHandler.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <map>

namespace
{
using namespace newHorizonsHeroes;
constexpr auto AUTHORITY = "new-horizons:authority";
PerkState freshPerks()
{
	PerkState result;
	auto rules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
	result.replaceRules(std::move(rules));
	return result;
}
JsonNode development() { return JsonNode(JsonPath::builtin("config/newHorizonsHeroes")); }
int authorityExpert(const std::string & skill) { return skill == AUTHORITY ? 3 : 0; }
StartingHeroContext context()
{
	StartingHeroContext result;
	result.development = development();
	result.capabilities = JsonNode(JsonPath::builtin("config/newHorizonsCapabilities"));
	result.perks = freshPerks().getRules();
	result.magic = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
	return result;
}

// Pure captured predecessor fixture: current skill identities with the former
// two-parent Basic perk policy. No installed default is retroactively migrated.
JsonNode historicalDevelopment(int version)
{
	auto result = development();
	auto & starts = result["startingSkills"];
	starts["startingPerksPolicyVersion"].Integer() = version;
	const auto perks = freshPerks().getRules();
	for(auto & [identity, row] : starts["startingDevelopmentProfiles"].Struct())
	{
		const auto * hero = HeroTypeID(HeroTypeID::decode(identity)).toHeroType();
		const auto own = starts["factionSkills"][FactionID::encode(hero->heroClass->faction.getNum())].String();
		for(const auto & skill : row["skills"].Vector())
		{
			const auto key = skill["skill"].String();
			const auto & candidates = perks["skills"][key]["perks"].Vector();
			const auto chosen = std::ranges::find_if(candidates, [](const auto & candidate)
				{ return candidate["requires"].String() == "basic" && candidate["effect"]["status"].String() == "active"; });
			if(chosen == candidates.end()) throw std::runtime_error("Missing historical Basic test candidate");
			JsonNode choice;
			choice["skill"].String() = key;
			choice["perk"].String() = (*chosen)["id"].String();
			starts["startingPerkProfiles"][identity].Vector().push_back(choice);
			if(key != own) row["startingPerks"].Vector().push_back(choice);
		}
	}
	return result;
}
class FreshHeroInitializationTest : public TinyMapGameTest
{
protected:
	Services * gameServices() override { return LIBRARY; }
};
}

TEST(StartingPerkDefaults, SkillsOnlyCustomRanksNeverBackfillAnyTier)
{
	auto perks = freshPerks();
	initializeStartingHeroPerks(perks, development(), HeroTypeID::NONE, {}, {}, authorityExpert);
	EXPECT_TRUE(perks.getSelected().empty());
	EXPECT_FALSE(perks.canAdvanceSkillNormally(AUTHORITY, 1));
	EXPECT_FALSE(perks.canAdvanceSkillNormally(AUTHORITY, 2));
}

TEST(StartingPerkDefaults, SkillsOnlyCreationIgnoresPrototypeAndProfilePerkPreferences)
{
	auto perks = freshPerks();
	const auto pool = perks.candidatesForSkillTier(AUTHORITY, 1);
	ASSERT_GE(pool.size(), 2);
	initializeStartingHeroPerks(perks, development(), HeroTypeID::NONE,
		{pool.front().selection}, {pool.back().selection}, authorityExpert);
	EXPECT_TRUE(perks.getSelected().empty());
}

TEST(StartingPerkDefaults, SkillsOnlyInitializationNeverChangesEarnedSelections)
{
	auto perks = freshPerks();
	const auto choice = perks.candidatesForSkillTier(AUTHORITY, 1).front().selection;
	perks.select(choice.skillId, choice.perkId, 1);
	const auto before = perks.toJson();
	initializeStartingHeroPerks(perks, development(), HeroTypeID::NONE, {}, {}, authorityExpert);
	EXPECT_EQ(perks.toJson(), before);
	CMemorySerializer storage;
	storage.oser & perks;
	PerkState restored;
	storage.iser & restored;
	EXPECT_EQ(restored.toJson(), before);
}

TEST(StartingPerkDefaults, HistoricalPoliciesRetainExplicitCompletionAndCodecIdentity)
{
	for(int version : {1, 2})
	{
		auto rules = historicalDevelopment(version);
		auto perks = freshPerks();
		initializeStartingHeroPerks(perks, rules, HeroTypeID::NONE, {}, {}, authorityExpert);
		ASSERT_EQ(perks.getSelected().size(), 3);
		for(int tier = 1; tier <= 3; ++tier) EXPECT_TRUE(perks.hasPerkForTier(AUTHORITY, tier));
		CMemorySerializer bytes;
		bytes.oser & rules;
		JsonNode restored;
		bytes.iser & restored;
		EXPECT_EQ(restored, rules);
		const auto earned = perks.toJson();
		initializeStartingHeroPerks(perks, development(), HeroTypeID::NONE, {}, {}, authorityExpert);
		EXPECT_EQ(perks.toJson(), earned);
	}
}

TEST(StartingPerkDefaults, EveryRegisteredAuthoredProjectionHasTwoBasicSkillsAndZeroPerks)
{
	const auto configured = context();
	const auto & profiles = configured.development["startingSkills"]["startingDevelopmentProfiles"].Struct();
	ASSERT_EQ(profiles.size(), 163);
	for(const auto & [identity, authored] : profiles)
	{
		SCOPED_TRACE(identity);
		const auto * hero = HeroTypeID(HeroTypeID::decode(identity)).toHeroType();
		ASSERT_NE(hero, nullptr);
		const auto projection = projectStartingHero(*hero, configured);
		ASSERT_EQ(projection.skills.size(), 2);
		EXPECT_NE(projection.skills[0].first, projection.skills[1].first);
		for(const auto & [skill, rank] : projection.skills) EXPECT_EQ(rank, MasteryLevel::BASIC);
		EXPECT_TRUE(projection.perks.empty());
	}
}

TEST(StartingPerkDefaults, AstralHasBasicSpellcraftAndMetamagicWithNoAutomaticPerks)
{
	const auto id = HeroTypeID(HeroTypeID::decode("core:astral"));
	const auto view = projectStartingHero(*id.toHeroType(), context());
	EXPECT_EQ(view.skills, (std::vector<std::pair<SecondarySkill, ui8>>{
		{SecondarySkill(SecondarySkill::decode("new-horizons:spellcraft")), MasteryLevel::BASIC},
		{SecondarySkill(SecondarySkill::decode("new-horizons:metamagic")), MasteryLevel::BASIC}}));
	EXPECT_TRUE(view.perks.empty());
}

TEST(StartingPerkDefaults, SkillsOnlyPolicyRejectsIncompleteRepeatedNonBasicAndPerkBearingDefaults)
{
	const auto reject = [](const std::function<void(JsonNode &)> & corrupt)
	{
		auto starts = development()["startingSkills"];
		corrupt(starts);
		EXPECT_THROW(validateStartingPerkProfiles(starts), std::runtime_error);
	};
	reject([](auto & starts) { starts["startingDevelopmentProfiles"].Struct().erase("core:adrienne"); });
	reject([](auto & starts) {
		auto & skills = starts["startingDevelopmentProfiles"]["core:astral"]["skills"].Vector();
		skills.back() = skills.front();
	});
	reject([](auto & starts) { starts["startingDevelopmentProfiles"]["core:astral"]["skills"].Vector().back()["rank"].Integer() = 2; });
	reject([](auto & starts) { starts["startingPerkProfiles"].Struct(); });
	reject([](auto & starts) { starts["startingDevelopmentProfiles"]["core:astral"]["startingPerks"].Vector(); });
	reject([](auto & starts) { starts["startingDevelopmentProfiles"]["core:astral"]["skills"].Vector().back()["skill"].String() = AUTHORITY; });
}

TEST(StartingPerkDefaults, MalformedSkillsOnlyTypesRejectWithoutMutatingEarnedState)
{
	auto perks = freshPerks();
	const auto before = perks.toJson();
	auto rules = development();
	rules["startingSkills"]["startingDevelopmentProfiles"]["core:astral"]["skills"].Vector().back().Integer() = 7;
	EXPECT_THROW(initializeStartingHeroPerks(perks, rules, HeroTypeID::NONE, {}, {}, authorityExpert), std::runtime_error);
	EXPECT_EQ(perks.toJson(), before);
}

TEST(StartingPerkDefaults, SkillsOnlyProfileDoesNotReadThePerkCatalogue)
{
	const auto id = HeroTypeID(HeroTypeID::decode("core:astral"));
	PerkState unavailable;
	ASSERT_EQ(unavailable.compiledCatalogue(), nullptr); // No capture is needed for skills-only starts.
	const auto profile = startingDevelopmentProfile(resolveHeroRules(development(), id.toHeroType()->heroClass->getId()),
		unavailable, id, id.toHeroType()->heroClass->getId());
	ASSERT_TRUE(profile);
	EXPECT_EQ(profile->skills.size(), 2);
	EXPECT_TRUE(profile->perks.empty());
}

TEST(StartingPerkDefaults, SkillsOnlyProfilesAreIndependentOfHistoricalNavigationMarker)
{
	for(const auto * identity : {"core:sylvia", "core:voy"})
	{
		const auto id = HeroTypeID(HeroTypeID::decode(identity));
		auto rules = development();
		rules["skillSpecialties"].Struct().erase("navigationStartReplacements");
		const auto profile = startingDevelopmentProfile(resolveHeroRules(rules, id.toHeroType()->heroClass->getId()),
			freshPerks(), id, id.toHeroType()->heroClass->getId());
		ASSERT_TRUE(profile);
		EXPECT_EQ(profile->skills.size(), 2);
		EXPECT_TRUE(profile->perks.empty());
	}
}

TEST(StartingPerkDefaults, PolicyMarkerIsExactAndRejectsMalformedCaptures)
{
	const auto rules = development();
	EXPECT_FALSE(usesAuthoredStartingPerks(rules));
	EXPECT_TRUE(usesBasicStartingProfiles(rules["startingSkills"]));
	EXPECT_TRUE(usesSkillsOnlyStartingProfiles(rules["startingSkills"]));
	for(const auto marker : {JsonNode(0), JsonNode(4), JsonNode(3.0), JsonNode("3")})
	{
		auto starts = rules["startingSkills"];
		starts["startingPerksPolicyVersion"] = marker;
		EXPECT_FALSE(usesSkillsOnlyStartingProfiles(starts));
		EXPECT_THROW(validateStartingPerkProfiles(starts), std::runtime_error);
	}
}

TEST_F(FreshHeroInitializationTest, EveryRegisteredCoreHeroActuallyStartsWithTwoBasicSkillsAndZeroPerks)
{
	const auto configured = development();
	const auto & profiles = configured["startingSkills"]["startingDevelopmentProfiles"].Struct();
	std::vector<std::pair<HeroTypeID, int3>> expected;
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(72).playerActive(PlayerColor(0));
	for(const auto & [identity, row] : profiles)
	{
		const auto index = expected.size();
		const int3 position(3 + 4 * static_cast<int>(index % 14), 3 + 4 * static_cast<int>(index / 14), 0);
		const HeroTypeID id(HeroTypeID::decode(identity));
		builder.hero(position, id, PlayerColor(0)).heroExperience(0);
		expected.emplace_back(id, position);
	}
	ASSERT_EQ(expected.size(), 163);
	startWithMap(std::move(builder));
	GameRandomizer randomizer(*gameState());
	randomizer.setSeed(196);
	for(const auto & [id, position] : expected)
	{
		SCOPED_TRACE(HeroTypeID::encode(id.getNum()));
		auto * hero = findHeroAt(position);
		ASSERT_NE(hero, nullptr);
		EXPECT_EQ(hero->getHeroTypeID(), id);
		ASSERT_EQ(hero->secSkills.size(), 2);
		EXPECT_NE(hero->secSkills[0].first, hero->secSkills[1].first);
		for(const auto & [skill, rank] : hero->secSkills) EXPECT_EQ(rank, MasteryLevel::BASIC);
		EXPECT_TRUE(hero->getPerkState().getSelected().empty());
		const auto before = hero->secSkills;
		hero->initHero(randomizer);
		EXPECT_EQ(hero->secSkills, before);
		EXPECT_TRUE(hero->getPerkState().getSelected().empty());
	}
	// Earned state is serialized and reinitialized without retroactive default grants.
	auto * hero = findHeroAt(expected.front().second);
	const auto parent = SecondarySkill::encode(hero->secSkills.front().first.getNum());
	const auto choice = hero->getPerkState().candidatesForSkillTier(parent, 1).front().selection;
	hero->applyPerkSelection(choice);
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	auto * reloaded = restored.getHero(hero->id);
	ASSERT_NE(reloaded, nullptr);
	EXPECT_EQ(reloaded->getPerkState().getSelected(), hero->getPerkState().getSelected());
	GameRandomizer resumed(restored);
	reloaded->initHero(resumed);
	EXPECT_EQ(reloaded->getPerkState().getSelected(), hero->getPerkState().getSelected());
}
