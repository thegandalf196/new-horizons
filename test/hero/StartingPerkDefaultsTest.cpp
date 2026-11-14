/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/entities/hero/HeroStartingProjection.h"
#include "../../lib/entities/hero/NewHorizonsStartingPerks.h"
#include "../../lib/entities/hero/CHeroHandler.h"
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
	ASSERT_FALSE(profiles.empty());
	for(const auto & [identity, authored] : profiles)
	{
		SCOPED_TRACE(identity);
		const auto * hero = LIBRARY->heroh->getById(HeroTypeID(HeroTypeID::decode(identity)));
		ASSERT_NE(hero, nullptr);
		const auto projection = newHorizonsHeroes::projectStartingHero(*hero, context);
		std::map<std::string, int> ranks;
		size_t expected = 0;
		for(const auto & [skill, rank] : projection.skills)
		{
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
			EXPECT_TRUE(perks.hasSelection("new-horizons:necromancy", "new-horizons:necromancy.graveKnowledge"));
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
