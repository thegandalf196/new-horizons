/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/entities/hero/HeroStartingProjection.h"
#include "../../lib/entities/hero/NewHorizonsStartingPerks.h"
#include "../../lib/entities/hero/CHeroHandler.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <map>

namespace
{
constexpr auto AUTHORITY = "new-horizons:authority";
newHorizonsHeroes::PerkState freshPerks()
{
	newHorizonsHeroes::PerkState result;
	result.rules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
	return result;
}
JsonNode development() { return JsonNode(JsonPath::builtin("config/newHorizonsHeroes")); }
int authorityExpert(const std::string & skill) { return skill == AUTHORITY ? 3 : 0; }
}

TEST(StartingPerkDefaults, CustomInitialRanksReceiveOnePerTierAndRemainSerializable)
{
	auto perks = freshPerks();
	newHorizonsHeroes::initializeStartingHeroPerks(perks, development(), HeroTypeID::NONE, {}, {}, authorityExpert);
	ASSERT_EQ(perks.selected.size(), 3u);
	for(int tier = 1; tier <= 3; ++tier) EXPECT_TRUE(perks.hasPerkForTier(AUTHORITY, tier));
	EXPECT_TRUE(perks.nextRequiredPerkOffer(authorityExpert).empty());
	CMemorySerializer storage;
	storage.oser & perks;
	newHorizonsHeroes::PerkState restored;
	storage.iser & restored;
	EXPECT_EQ(restored.toJson(), perks.toJson());
	EXPECT_TRUE(restored.nextRequiredPerkOffer(authorityExpert).empty());
}

TEST(StartingPerkDefaults, ExplicitChoicesWinAndCompletionIsIdempotent)
{
	auto perks = freshPerks();
	const auto pool = perks.candidatesForSkillTier(AUTHORITY, 1);
	ASSERT_GE(pool.size(), 2u);
	const auto explicitChoice = pool.back().selection;
	const auto profileChoice = pool.front().selection;
	newHorizonsHeroes::initializeStartingHeroPerks(perks, development(), HeroTypeID::NONE,
		{profileChoice}, {explicitChoice}, authorityExpert);
	EXPECT_TRUE(perks.hasSelection(explicitChoice.skillId, explicitChoice.perkId));
	const auto before = perks.toJson();
	newHorizonsHeroes::initializeStartingHeroPerks(perks, development(), HeroTypeID::NONE,
		{profileChoice}, {}, authorityExpert);
	EXPECT_EQ(before, perks.toJson());
}

TEST(StartingPerkDefaults, DoesNotGrantUnownedSkillsAndInvalidChoicesAreAtomic)
{
	auto perks = freshPerks();
	const auto rules = development();
	newHorizonsHeroes::initializeStartingHeroPerks(perks, rules, HeroTypeID(HeroTypeID::decode("core:nimbus")),
		{}, {}, [](const auto &) { return 0; });
	EXPECT_TRUE(perks.selected.empty());
	const auto before = perks.toJson();
	EXPECT_THROW(newHorizonsHeroes::initializeStartingHeroPerks(perks, rules, HeroTypeID::NONE,
		{}, {{AUTHORITY, "new-horizons:authority.invalid"}}, authorityExpert), std::runtime_error);
	EXPECT_EQ(before, perks.toJson());
}

TEST(StartingPerkDefaults, HistoricalCaptureDoesNotFillMandatoryPerkDebt)
{
	auto rules = development();
	rules["startingSkills"].Struct().erase("startingPerksPolicyVersion");
	rules["startingSkills"].Struct().erase("startingPerkProfiles");
	auto perks = freshPerks();
	newHorizonsHeroes::initializeStartingHeroPerks(perks, rules, HeroTypeID::NONE, {}, {}, authorityExpert);
	EXPECT_TRUE(perks.selected.empty());
	EXPECT_FALSE(perks.nextRequiredPerkOffer(authorityExpert).empty());
}

TEST(StartingPerkDefaults, EveryRegisteredAuthoredHeroHasCompleteFinalNormalizedTiers)
{
	newHorizonsHeroes::StartingHeroContext context;
	context.development = development();
	context.capabilities = JsonNode(JsonPath::builtin("config/newHorizonsCapabilities"));
	context.perks = freshPerks().rules;
	context.magic = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
	const auto & profiles = context.development["startingSkills"]["startingPerkProfiles"].Struct();
	ASSERT_EQ(profiles.size(), 163u);
	for(const auto & [identity, authored] : profiles)
	{
		SCOPED_TRACE(identity);
		const auto index = HeroTypeID::decode(identity);
		ASSERT_GE(index, 0);
		ASSERT_LT(static_cast<size_t>(index), LIBRARY->heroh->objects.size());
		const auto & hero = LIBRARY->heroh->objects.at(index);
		ASSERT_NE(hero, nullptr);
		const auto projection = newHorizonsHeroes::projectStartingHero(*hero, context);
		ASSERT_EQ(projection.skills.size(), 2u);
		EXPECT_NE(projection.skills[0].first, projection.skills[1].first);
		std::map<std::string, int> ranks;
		size_t expected = 0;
		for(const auto & [skill, rank] : projection.skills)
		{
			EXPECT_EQ(rank, MasteryLevel::BASIC);
			ranks[SecondarySkill::encode(skill.getNum())] = rank;
			expected += rank;
		}
		auto perks = freshPerks();
		perks.selected = projection.perks;
		const auto lookup = [&](const auto & skill) { return ranks.contains(skill) ? ranks.at(skill) : 0; };
		EXPECT_EQ(projection.perks.size(), expected);
		EXPECT_EQ(projection.perks.size(), authored.Vector().size());
		EXPECT_TRUE(perks.nextRequiredPerkOffer(lookup).empty());
		for(const auto & entry : authored.Vector())
			EXPECT_TRUE(perks.hasSelection(entry["skill"].String(), entry["perk"].String()));
		if(identity == "core:nimbus")
		{
			EXPECT_TRUE(perks.hasSelection("new-horizons:necromancy", "new-horizons:necromancy.graveKnowledge"));
		}
	}
}

TEST(StartingPerkDefaults, AstralHasExactlyConcentrationAndLingeringMagic)
{
	newHorizonsHeroes::StartingHeroContext context;
	context.development = development();
	context.capabilities = JsonNode(JsonPath::builtin("config/newHorizonsCapabilities"));
	context.perks = freshPerks().rules;
	context.magic = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
	const auto id = HeroTypeID(HeroTypeID::decode("core:astral"));
	const auto view = newHorizonsHeroes::projectStartingHero(*id.toHeroType(), context);
	EXPECT_EQ(view.skills, (std::vector<std::pair<SecondarySkill, ui8>>{
		{SecondarySkill(SecondarySkill::decode("new-horizons:spellcraft")), MasteryLevel::BASIC},
		{SecondarySkill(SecondarySkill::decode("new-horizons:metamagic")), MasteryLevel::BASIC}}));
	auto perks = freshPerks();
	perks.selected = view.perks;
	ASSERT_EQ(perks.selected.size(), 2u);
	EXPECT_TRUE(perks.hasSelection("new-horizons:spellcraft", "new-horizons:spellcraft.concentration"));
	EXPECT_TRUE(perks.hasSelection("new-horizons:metamagic", "new-horizons:metamagic.lingeringMagic"));
	EXPECT_FALSE(perks.hasSelection("new-horizons:metamagic", "new-horizons:metamagic.arcaneWellspring"));
}

TEST(StartingPerkDefaults, BasicPolicyRejectsMissingRepeatedNonBasicAndWrongTierDefaults)
{
	const auto original = development();
	const auto id = HeroTypeID(HeroTypeID::decode("core:astral"));
	ASSERT_TRUE(newHorizonsHeroes::startingDevelopmentProfile(
		newHorizonsHeroes::resolveHeroRules(original, id.toHeroType()->heroClass->getId()), freshPerks(),
		id, id.toHeroType()->heroClass->getId()));
	const auto reject = [&](const std::function<void(JsonNode &)> & corrupt)
	{
		auto rules = original;
		corrupt(rules["startingSkills"]);
		EXPECT_THROW(newHorizonsHeroes::startingDevelopmentProfile(
			newHorizonsHeroes::resolveHeroRules(rules, id.toHeroType()->heroClass->getId()), freshPerks(),
			id, id.toHeroType()->heroClass->getId()), std::runtime_error);
	};
	reject([](JsonNode & starts) { starts["startingDevelopmentProfiles"].Struct().erase("core:adrienne"); });
	reject([](JsonNode & starts) { starts["startingPerkProfiles"].Struct().erase("core:astral"); });
	reject([](JsonNode & starts) { starts["startingPerkProfiles"]["core:astral"].Vector().pop_back(); });
	reject([](JsonNode & starts) {
		auto & skills = starts["startingDevelopmentProfiles"]["core:astral"]["skills"].Vector();
		skills.back() = skills.front();
	});
	reject([](JsonNode & starts) {
		starts["startingDevelopmentProfiles"]["core:astral"]["skills"].Vector().back()["rank"].Integer() = 2;
	});
	reject([](JsonNode & starts) {
		auto & perks = starts["startingPerkProfiles"]["core:astral"].Vector();
		perks.back() = perks.front();
	});
	reject([](JsonNode & starts) {
		starts["startingPerkProfiles"]["core:astral"].Vector().back()["perk"].String() = "new-horizons:metamagic.arcaneWellspring";
	});
}

TEST(StartingPerkDefaults, BasicPolicyMalformedTypesThrowWithoutMutation)
{
	const auto id = HeroTypeID(HeroTypeID::decode("core:astral"));
	const auto reject = [&](const std::function<void(JsonNode &)> & corrupt)
	{
		auto rules = development();
		ASSERT_TRUE(newHorizonsHeroes::startingDevelopmentProfile(
			newHorizonsHeroes::resolveHeroRules(rules, id.toHeroType()->heroClass->getId()), freshPerks(),
			id, id.toHeroType()->heroClass->getId()));
		corrupt(rules["startingSkills"]);
		const auto beforeRules = rules;
		auto perks = freshPerks();
		const auto beforePerks = perks.toJson();
		EXPECT_THROW(newHorizonsHeroes::validateStartingPerkProfiles(rules["startingSkills"]), std::runtime_error);
		EXPECT_THROW(newHorizonsHeroes::startingDevelopmentProfile(
			newHorizonsHeroes::resolveHeroRules(rules, id.toHeroType()->heroClass->getId()), perks,
			id, id.toHeroType()->heroClass->getId()), std::runtime_error);
		EXPECT_EQ(rules, beforeRules);
		EXPECT_EQ(perks.toJson(), beforePerks);
	};
	reject([](JsonNode & starts) { starts["startingDevelopmentProfiles"]["core:astral"].Integer() = 7; });
	reject([](JsonNode & starts) { starts["factionSkills"].Integer() = 7; });
	reject([](JsonNode & starts) {
		starts["startingDevelopmentProfiles"]["core:astral"]["skills"].Vector().back().Integer() = 7;
	});
	reject([](JsonNode & starts) {
		starts["startingDevelopmentProfiles"]["core:astral"]["skills"].Vector().back()["skill"].Integer() = 7;
	});
}

TEST(StartingPerkDefaults, VersionOneCapturedAdvancedRanksAndEarnedPerksSurviveRoundtrip)
{
	auto rules = development();
	rules["startingSkills"]["startingPerksPolicyVersion"].Integer() = 1;
	const auto id = HeroTypeID(HeroTypeID::decode("core:astral"));
	rules["startingSkills"]["startingDevelopmentProfiles"]["core:astral"]["skills"].Vector().back()["rank"].Integer() = 2;
	JsonNode choice;
	choice["skill"].String() = "new-horizons:metamagic";
	choice["perk"].String() = "new-horizons:metamagic.arcaneWellspring";
	rules["startingSkills"]["startingPerkProfiles"]["core:astral"].Vector().push_back(choice);
	CMemorySerializer bytes;
	bytes.oser & rules;
	JsonNode restored;
	bytes.iser & restored;
	const auto profile = newHorizonsHeroes::startingDevelopmentProfile(
		newHorizonsHeroes::resolveHeroRules(restored, id.toHeroType()->heroClass->getId()), freshPerks(),
		id, id.toHeroType()->heroClass->getId());
	ASSERT_TRUE(profile);
	EXPECT_EQ(profile->skills.back().second, MasteryLevel::ADVANCED);
	auto perks = freshPerks();
	const auto rank = [](const std::string & skill) { return skill == "new-horizons:metamagic" ? 2 : skill == "new-horizons:spellcraft" ? 1 : 0; };
	newHorizonsHeroes::initializeStartingHeroPerks(perks, restored, id, profile->perks, {}, rank);
	ASSERT_EQ(perks.selected.size(), 3u);
	EXPECT_TRUE(perks.hasSelection("new-horizons:metamagic", "new-horizons:metamagic.arcaneWellspring"));
	const auto earned = perks.toJson();
	newHorizonsHeroes::initializeStartingHeroPerks(perks, development(), id, {}, {}, rank);
	EXPECT_EQ(perks.toJson(), earned);
}

TEST(StartingPerkDefaults, BasicProfilesAreIndependentOfHistoricalNavigationMarker)
{
	for(const auto * identity : {"core:sylvia", "core:voy"})
	{
		SCOPED_TRACE(identity);
		const auto id = HeroTypeID(HeroTypeID::decode(identity));
		auto rules = development();
		rules["skillSpecialties"].Struct().erase("navigationStartReplacements");
		auto perks = freshPerks();
		const auto profile = newHorizonsHeroes::startingDevelopmentProfile(
			newHorizonsHeroes::resolveHeroRules(rules, id.toHeroType()->heroClass->getId()), perks,
			id, id.toHeroType()->heroClass->getId());
		ASSERT_TRUE(profile);
		ASSERT_EQ(profile->skills.size(), 2u);
		EXPECT_NE(profile->skills[0].first, profile->skills[1].first);
		for(const auto & [skill, rank] : profile->skills)
			EXPECT_EQ(rank, MasteryLevel::BASIC);
		const auto rank = [&](const std::string & skill) {
			const auto parent = SecondarySkill(SecondarySkill::decode(skill));
			return std::ranges::any_of(profile->skills, [parent](const auto & row) { return row.first == parent; }) ? 1 : 0;
		};
		newHorizonsHeroes::initializeStartingHeroPerks(perks, rules, id, profile->perks, {}, rank);
		ASSERT_EQ(perks.selected.size(), 2u);
		for(const auto & [skill, mastery] : profile->skills)
			EXPECT_TRUE(perks.hasPerkForTier(SecondarySkill::encode(skill.getNum()), MasteryLevel::BASIC));
		// The missing marker still means no successor profile in old captures.
		rules["startingSkills"]["startingPerksPolicyVersion"].Integer() = 1;
		EXPECT_FALSE(newHorizonsHeroes::startingDevelopmentProfile(
			newHorizonsHeroes::resolveHeroRules(rules, id.toHeroType()->heroClass->getId()), freshPerks(),
			id, id.toHeroType()->heroClass->getId()));
	}
}

TEST(StartingPerkDefaults, PolicyRejectsPartialAndNonIntegerCaptures)
{
	auto rules = development();
	ASSERT_TRUE(newHorizonsHeroes::usesAuthoredStartingPerks(rules));
	rules["startingSkills"].Struct().erase("startingPerkProfiles");
	EXPECT_THROW(newHorizonsHeroes::validateStartingPerkProfiles(rules["startingSkills"]), std::runtime_error);
	rules = development();
	rules["startingSkills"]["startingPerksPolicyVersion"].Float() = 1.0;
	EXPECT_FALSE(newHorizonsHeroes::usesAuthoredStartingPerks(rules));
	EXPECT_THROW(newHorizonsHeroes::validateStartingPerkProfiles(rules["startingSkills"]), std::runtime_error);
}
