/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../OriginalDataRequirement.h"
#include "../PerkStateFixture.h"
#include "../mock/TinyMapGameTest.h"
#include "../../lib/entities/hero/HeroStartingProjection.h"
#include "../../lib/entities/hero/NewHorizonsStartingPerks.h"
#include "../../lib/entities/hero/CHeroHandler.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/gameState/TavernHeroesPool.h"
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
// Policies before version 4 predate Cuthbert's three-Skill Castle start.
void restoreTwoSkillCuthbert(JsonNode & starts)
{
	auto & skills = starts["startingDevelopmentProfiles"]["core:cuthbert"]["skills"].Vector();
	skills.pop_back();
	skills.back()["skill"].String() = "new-horizons:spellcraft";
}
JsonNode skillsOnlyDevelopment()
{
	auto result = development();
	restoreTwoSkillCuthbert(result["startingSkills"]);
	result["startingSkills"]["startingPerksPolicyVersion"].Integer() = 3;
	result["startingSkills"].Struct().erase("startingPerkProfiles");
	return result;
}
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
	restoreTwoSkillCuthbert(starts);
	starts["startingPerksPolicyVersion"].Integer() = version;
	starts.Struct().erase("startingPerkProfiles");
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
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto perks = freshPerks();
	initializeStartingHeroPerks(perks, skillsOnlyDevelopment(), HeroTypeID::NONE, {}, {}, authorityExpert);
	EXPECT_TRUE(perks.getSelected().empty());
	EXPECT_FALSE(perks.canAdvanceSkillNormally(AUTHORITY, 1));
	EXPECT_FALSE(perks.canAdvanceSkillNormally(AUTHORITY, 2));
}

TEST(StartingPerkDefaults, SkillsOnlyCreationIgnoresPrototypeAndProfilePerkPreferences)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto perks = freshPerks();
	const auto pool = perks.candidatesForSkillTier(AUTHORITY, 1);
	ASSERT_GE(pool.size(), 2);
	initializeStartingHeroPerks(perks, skillsOnlyDevelopment(), HeroTypeID::NONE,
		{pool.front().selection}, {pool.back().selection}, authorityExpert);
	EXPECT_TRUE(perks.getSelected().empty());
}

TEST(StartingPerkDefaults, SkillsOnlyInitializationNeverChangesEarnedSelections)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto perks = freshPerks();
	const auto choice = perks.candidatesForSkillTier(AUTHORITY, 1).front().selection;
	perks.select(choice.skillId, choice.perkId, 1);
	const auto before = perks.toJson();
	initializeStartingHeroPerks(perks, skillsOnlyDevelopment(), HeroTypeID::NONE, {}, {}, authorityExpert);
	EXPECT_EQ(perks.toJson(), before);
	CMemorySerializer storage;
	storage.oser & perks;
	PerkState restored;
	storage.iser & restored;
	EXPECT_EQ(restored.toJson(), before);
}

TEST(StartingPerkDefaults, HistoricalPoliciesRetainExplicitCompletionAndCodecIdentity)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
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

TEST(StartingPerkDefaults, EveryRegisteredAuthoredProjectionHasTwoBasicSkillsOnePerkAndSpellbook)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	const auto configured = context();
	const auto & profiles = configured.development["startingSkills"]["startingDevelopmentProfiles"].Struct();
	ASSERT_EQ(profiles.size(), 163);
	for(const auto & [identity, authored] : profiles)
	{
		SCOPED_TRACE(identity);
		const auto * hero = HeroTypeID(HeroTypeID::decode(identity)).toHeroType();
		ASSERT_NE(hero, nullptr);
		const auto projection = projectStartingHero(*hero, configured);
		const bool threeSkills = usesThreeSkillStart(configured.development["startingSkills"], identity);
		EXPECT_EQ(threeSkills, identity == "core:cuthbert"); // The one deliberate exception.
		ASSERT_EQ(projection.skills.size(), threeSkills ? 3 : 2);
		EXPECT_NE(projection.skills[0].first, projection.skills[1].first);
		for(const auto & [skill, rank] : projection.skills) EXPECT_EQ(rank, MasteryLevel::BASIC);
		EXPECT_TRUE(projection.spellBook);
		if(threeSkills)
		{
			EXPECT_TRUE(projection.perks.empty());
			continue;
		}
		ASSERT_EQ(projection.perks.size(), 1);
		const auto & expected = configured.development["startingSkills"]["startingPerkProfiles"][identity].Vector().front();
		EXPECT_EQ(projection.perks.front().skillId, expected["skill"].String());
		EXPECT_EQ(projection.perks.front().perkId, expected["perk"].String());
	}
}

TEST(StartingPerkDefaults, AstralHasBasicSpellcraftMetamagicAndOneAuthoredPerk)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	const auto id = HeroTypeID(HeroTypeID::decode("core:astral"));
	const auto view = projectStartingHero(*id.toHeroType(), context());
	EXPECT_EQ(view.skills, (std::vector<std::pair<SecondarySkill, ui8>>{
		{SecondarySkill(SecondarySkill::decode("new-horizons:spellcraft")), MasteryLevel::BASIC},
		{SecondarySkill(SecondarySkill::decode("new-horizons:metamagic")), MasteryLevel::BASIC}}));
	ASSERT_EQ(view.perks.size(), 1);
	EXPECT_TRUE(view.spellBook);
}

TEST(StartingPerkDefaults, SkillsOnlyPolicyRejectsIncompleteRepeatedNonBasicAndPerkBearingDefaults)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	const auto reject = [](const std::function<void(JsonNode &)> & corrupt)
	{
		auto starts = skillsOnlyDevelopment()["startingSkills"];
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
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto perks = freshPerks();
	const auto before = perks.toJson();
	auto rules = skillsOnlyDevelopment();
	rules["startingSkills"]["startingDevelopmentProfiles"]["core:astral"]["skills"].Vector().back().Integer() = 7;
	EXPECT_THROW(initializeStartingHeroPerks(perks, rules, HeroTypeID::NONE, {}, {}, authorityExpert), std::runtime_error);
	EXPECT_EQ(perks.toJson(), before);
}

TEST(StartingPerkDefaults, SkillsOnlyProfileDoesNotReadThePerkCatalogue)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	const auto id = HeroTypeID(HeroTypeID::decode("core:astral"));
	PerkState unavailable;
	ASSERT_EQ(unavailable.compiledCatalogue(), nullptr); // No capture is needed for skills-only starts.
	const auto profile = startingDevelopmentProfile(resolveHeroRules(skillsOnlyDevelopment(), id.toHeroType()->heroClass->getId()),
		unavailable, id, id.toHeroType()->heroClass->getId());
	ASSERT_TRUE(profile);
	EXPECT_EQ(profile->skills.size(), 2);
	EXPECT_TRUE(profile->perks.empty());
}

TEST(StartingPerkDefaults, SkillsOnlyProfilesAreIndependentOfHistoricalNavigationMarker)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	for(const auto * identity : {"core:sylvia", "core:voy"})
	{
		const auto id = HeroTypeID(HeroTypeID::decode(identity));
		auto rules = skillsOnlyDevelopment();
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
	EXPECT_TRUE(usesAuthoredStartingPerks(rules));
	EXPECT_TRUE(usesBasicStartingProfiles(rules["startingSkills"]));
	EXPECT_FALSE(usesSkillsOnlyStartingProfiles(rules["startingSkills"]));
	EXPECT_TRUE(usesSingleStartingPerkProfiles(rules["startingSkills"]));
	for(const auto marker : {JsonNode(0), JsonNode(5), JsonNode(4.0), JsonNode("4")})
	{
		auto starts = rules["startingSkills"];
		starts["startingPerksPolicyVersion"] = marker;
		EXPECT_FALSE(usesSkillsOnlyStartingProfiles(starts));
		EXPECT_THROW(validateStartingPerkProfiles(starts), std::runtime_error);
	}
}

TEST_F(FreshHeroInitializationTest, EveryRegisteredCoreHeroActuallyStartsWithTwoBasicSkillsOnePerkAndSpellbook)
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
		const bool threeSkills = usesThreeSkillStart(configured["startingSkills"], HeroTypeID::encode(id.getNum()));
		const size_t perks = threeSkills ? 0 : 1;
		ASSERT_EQ(hero->secSkills.size(), threeSkills ? 3 : 2);
		EXPECT_NE(hero->secSkills[0].first, hero->secSkills[1].first);
		for(const auto & [skill, rank] : hero->secSkills) EXPECT_EQ(rank, MasteryLevel::BASIC);
		ASSERT_EQ(hero->getPerkState().getSelected().size(), perks);
		EXPECT_TRUE(hero->hasSpellbook());
		const auto before = hero->secSkills;
		const auto book = hero->getArtID(ArtifactPosition::SPELLBOOK);
		hero->initHero(randomizer);
		EXPECT_EQ(hero->secSkills, before);
		EXPECT_EQ(hero->getArtID(ArtifactPosition::SPELLBOOK), book);
		ASSERT_EQ(hero->getPerkState().getSelected().size(), perks);
		EXPECT_TRUE(hero->hasSpellbook());
	}
	// Earned state is serialized and reinitialized without retroactive default grants.
	auto * hero = findHeroAt(expected.front().second);
	const auto other = std::ranges::find_if(hero->secSkills, [&](const auto & row)
		{ return !hero->getPerkState().hasPerkForTier(SecondarySkill::encode(row.first.getNum()), 1); });
	ASSERT_NE(other, hero->secSkills.end());
	const auto parent = SecondarySkill::encode(other->first.getNum());
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

TEST(StartingPerkDefaults, SinglePerkScenarioFallbackDoesNotBackfillAdvancedRanks)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto perks = freshPerks();
	initializeStartingHeroPerks(perks, development(), HeroTypeID(HeroTypeID::decode("core:astral")), {}, {}, authorityExpert);
	ASSERT_EQ(perks.getSelected().size(), 1);
	EXPECT_TRUE(perks.hasPerkForTier(AUTHORITY, 1));
	EXPECT_FALSE(perks.hasPerkForTier(AUTHORITY, 2));
	const auto before = perks.toJson();
	initializeStartingHeroPerks(perks, development(), HeroTypeID::NONE, {}, {}, authorityExpert);
	EXPECT_EQ(perks.toJson(), before);
}

TEST(StartingPerkDefaults, SinglePerkRejectsMissingDuplicateForeignParentAndAdvancedChoicesAtomically)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	const auto corruptAndReject = [](const std::function<void(JsonNode &)> & corrupt)
	{
		auto rules = development();
		corrupt(rules["startingSkills"]);
		auto state = freshPerks();
		const auto before = state.toJson();
		EXPECT_THROW(initializeStartingHeroPerks(state, rules,
			HeroTypeID(HeroTypeID::decode("core:astral")), {}, {}, authorityExpert), std::runtime_error);
		EXPECT_EQ(state.toJson(), before);
	};
	corruptAndReject([](auto & starts) { starts["startingPerkProfiles"].Struct().erase("core:astral"); });
	corruptAndReject([](auto & starts) {
		auto & choices = starts["startingPerkProfiles"]["core:astral"].Vector();
		choices.push_back(choices.front());
	});
	corruptAndReject([](auto & starts) {
		auto & choice = starts["startingPerkProfiles"]["core:astral"].Vector().front();
		choice["skill"].String() = AUTHORITY;
		choice["perk"].String() = "new-horizons:authority.massedRanks";
	});
	corruptAndReject([](auto & starts) {
		auto & choice = starts["startingPerkProfiles"]["core:astral"].Vector().front();
		const auto rules = freshPerks().getRules();
		const auto skill = choice["skill"].String();
		const auto & candidates = rules["skills"][skill]["perks"].Vector();
		const auto advanced = std::ranges::find_if(candidates, [](const auto & row)
			{ return row["requires"].String() == "advanced" && row["effect"]["status"].String() == "active"; });
		if(advanced == candidates.end()) throw std::runtime_error("Missing Advanced corruption fixture");
		choice["perk"].String() = (*advanced)["id"].String();
	});
}

TEST_F(FreshHeroInitializationTest, FreshPoolHeroKeepsSinglePerkAndBookWhenTakenForHiring)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:christian")), PlayerColor(0)).heroExperience(0);
	startWithMap(std::move(builder));
	const auto id = HeroTypeID(HeroTypeID::decode("core:nimbus"));
	const auto * poolHero = gameState()->getMap().tryGetFromHeroPool(id);
	ASSERT_NE(poolHero, nullptr);
	ASSERT_EQ(poolHero->secSkills.size(), 2);
	ASSERT_EQ(poolHero->getPerkState().getSelected().size(), 1);
	EXPECT_TRUE(poolHero->hasSpellbook());
	const auto selections = poolHero->getPerkState().getSelected();
	const auto book = poolHero->getArtID(ArtifactPosition::SPELLBOOK);
	auto hired = gameState()->heroesPool->takeHeroFromPool(id);
	ASSERT_NE(hired, nullptr);
	GameRandomizer randomizer(*gameState());
	hired->initHero(randomizer);
	EXPECT_EQ(hired->getPerkState().getSelected(), selections);
	EXPECT_EQ(hired->getArtID(ArtifactPosition::SPELLBOOK), book);
}

TEST_F(FreshHeroInitializationTest, RemovedUniversalBookIsNotRegrantedDuringReinitOrReload)
{
	const auto id = HeroTypeID(HeroTypeID::decode("core:christian"));
	ASSERT_FALSE(id.toHeroType()->haveSpellBook);
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36).playerActive(PlayerColor(0)).hero({5, 5, 0}, id, PlayerColor(0)).heroExperience(0);
	startWithMap(std::move(builder));
	auto * hero = findHeroAt({5, 5, 0});
	ASSERT_NE(hero, nullptr);
	ASSERT_TRUE(hero->hasSpellbook());
	hero->removeArtifact(ArtifactPosition::SPELLBOOK);
	GameRandomizer randomizer(*gameState());
	hero->initHero(randomizer);
	EXPECT_FALSE(hero->hasSpellbook());
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	auto * reloaded = restored.getHero(hero->id);
	ASSERT_NE(reloaded, nullptr);
	GameRandomizer resumed(restored);
	reloaded->initHero(resumed);
	EXPECT_FALSE(reloaded->hasSpellbook());
}
