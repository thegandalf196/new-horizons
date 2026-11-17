/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../PerkStateFixture.h"
#include "../server/LegacyEstatesCapture.h"
#include "PerkInitializationReference.h"
#include "../../lib/entities/hero/NewHorizonsPerkOperation.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <map>
#include <random>
#include <stdexcept>
#include <type_traits>

namespace
{
using namespace newHorizonsHeroes;
constexpr auto AUTHORITY = "new-horizons:authority";
PerkState fresh()
{
	PerkState state;
	auto rules = estatesTest::legacyEstatesPerks();
	state.replaceRules(std::move(rules));
	return state;
}
JsonNode development() { return estatesTest::legacyEstatesHeroes(); }

void compareInitialization(const JsonNode & rules, HeroTypeID hero, const std::map<std::string, int> & ranks,
	const std::vector<PerkSelection> & explicitChoices)
{
	auto reference = fresh();
	auto optimized = reference;
	std::vector<std::string> referenceCalls, optimizedCalls;
	std::mt19937 referenceRng(1960911), optimizedRng(1960911);
	const auto lookup = [&](auto & calls, auto & rng, const auto & skill)
	{
		calls.push_back(skill);
		(void)rng(); // Makes callback order observable without changing returned ranks.
		return ranks.contains(skill) ? ranks.at(skill) : 0;
	};
	perkInitializationReference::initialize(reference, rules, hero, {}, explicitChoices,
		[&](const auto & skill) { return lookup(referenceCalls, referenceRng, skill); });
	initializeStartingHeroPerks(optimized, rules, hero, {}, explicitChoices,
		[&](const auto & skill) { return lookup(optimizedCalls, optimizedRng, skill); });
	EXPECT_EQ(referenceCalls, optimizedCalls);
	EXPECT_EQ(referenceRng, optimizedRng);
	EXPECT_EQ(reference.getSelected(), optimized.getSelected());
	EXPECT_EQ(reference.getRules(), optimized.getRules());
	EXPECT_EQ(reference.toJson(), optimized.toJson());
	CMemorySerializer referenceBytes, optimizedBytes;
	referenceBytes.oser & reference;
	optimizedBytes.oser & optimized;
	EXPECT_EQ(referenceBytes.extractBuffer(), optimizedBytes.extractBuffer());
}
}

static_assert(!std::is_constructible_v<newHorizonsHeroes::PerkSelectionOperation, newHorizonsHeroes::PerkState &&>);
static_assert(!std::is_constructible_v<newHorizonsHeroes::PerkSelectionOperation, const newHorizonsHeroes::PerkState &&>);

TEST(NewHorizonsPerkOperation, All163AuthoredHeroesMatchPreOptimizationTraversalAndBytes)
{
	const auto rules = development();
	const auto & profiles = rules["startingSkills"]["startingDevelopmentProfiles"].Struct();
	ASSERT_EQ(profiles.size(), 163u);
	for(const auto & [hero, profile] : profiles)
	{
		SCOPED_TRACE(hero);
		std::map<std::string, int> ranks;
		for(const auto & row : profile["skills"].Vector())
			ranks[row["skill"].String()] = row["rank"].Integer();
		compareInitialization(rules, HeroTypeID(HeroTypeID::decode(hero)), ranks, {});
	}
}

TEST(NewHorizonsPerkOperation, ExpertAndExplicitPreferencesPreserveCallbacksChoicesAndBytes)
{
	const auto state = fresh();
	const auto candidates = state.candidatesForSkillTier(AUTHORITY, 1);
	ASSERT_GE(candidates.size(), 2u);
	compareInitialization(development(), HeroTypeID::NONE, {{AUTHORITY, 3}}, {candidates.back().selection});
}

TEST(NewHorizonsPerkOperation, CommitIsExplicitAndCannotTargetAnotherState)
{
	auto state = fresh();
	const auto original = state.getSelected();
	PerkSelectionOperation operation(state);
	const auto choice = operation.candidates(AUTHORITY, 1).front().selection;
	operation.select(choice.skillId, choice.perkId, 1);
	EXPECT_EQ(state.getSelected(), original);
	auto other = state;
	EXPECT_THROW(operation.commit(other), std::logic_error);
	EXPECT_EQ(other.getSelected(), original);
	operation.commit(state);
	EXPECT_EQ(state.getSelected(), (std::vector<PerkSelection>{choice}));
	EXPECT_THROW(operation.commit(state), std::logic_error);
	EXPECT_THROW(operation.select(choice.skillId, choice.perkId, 1), std::logic_error);
}

TEST(NewHorizonsPerkOperation, FailedLaterSelectionPreservesWorkingAndPublishedChoices)
{
	auto state = fresh();
	PerkSelectionOperation operation(state);
	const auto choice = operation.candidates(AUTHORITY, 1).front().selection;
	operation.select(choice.skillId, choice.perkId, 1);
	const auto working = operation.selections();
	EXPECT_THROW(operation.select(choice.skillId, choice.perkId, 1), std::runtime_error);
	EXPECT_EQ(operation.selections(), working);
	EXPECT_TRUE(state.getSelected().empty());
	operation.commit(state);
	EXPECT_EQ(state.getSelected(), working);
}

TEST(NewHorizonsPerkOperation, InvalidUnrequestedCatalogueRowStillRejectsEveryRawBoundary)
{
	auto state = fresh();
	const auto before = state.toJson();
	auto malformed = state.getRules();
	malformed["skills"].Struct().rbegin()->second["perks"].Vector().back()["requires"].String() = "invalid";
	EXPECT_THROW(state.replaceRules(malformed), std::runtime_error);
	EXPECT_THROW((ValidatedPerkRules(malformed)), std::runtime_error);
	auto payload = before;
	payload["rules"] = malformed;
	EXPECT_THROW(PerkState::fromJson(payload), std::runtime_error);
	EXPECT_EQ(state.toJson(), before);
	EXPECT_NO_THROW((PerkSelectionOperation(state)));
}

TEST(NewHorizonsPerkOperation, InvalidLateRankLeavesOriginalInitializerStateUntouched)
{
	auto state = fresh();
	const auto original = state.toJson();
	const auto lastSkill = state.getRules()["skills"].Struct().rbegin()->first;
	EXPECT_THROW(initializeStartingHeroPerks(state, development(), HeroTypeID::NONE, {}, {},
		[&](const std::string & skill) { return skill == AUTHORITY ? 3 : (skill == lastSkill ? 4 : 0); }), std::runtime_error);
	EXPECT_EQ(state.toJson(), original);
}
