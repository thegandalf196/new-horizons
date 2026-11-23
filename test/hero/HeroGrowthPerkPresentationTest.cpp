/*
 * HeroGrowthPerkPresentationTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../OriginalDataRequirement.h"
#include "../PerkStateFixture.h"

#include "../../client/windows/HeroGrowthPerkPresentation.h"

namespace
{
const std::string SKILL = "new-horizons:discipline";
const std::string BASIC = SKILL + ".inspirationalLeader";
const std::string OTHER_BASIC = SKILL + ".steadfast";
const std::string ADVANCED = SKILL + ".standardBearer";
const std::string EXPERT = SKILL + ".unbreakable";

newHorizonsHeroes::PerkState stateFor(bool historicalR2 = false)
{
	newHorizonsHeroes::PerkState state;
	auto rules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
	rules.Struct().erase("fourOfferProgressionVersion");
	rules["unifiedSkillProgressionVersion"].Integer() = 1;
	if(historicalR2)
	{
		rules.Struct().erase("perkProgressionPolicyVersion");
		rules.Struct().erase("unifiedSkillProgressionVersion");
		rules.Struct().erase("startingPerksPolicyVersion");
		rules.Struct().erase("startingPerkProfiles");
	}
	state.replaceRules(std::move(rules));
	return state;
}

newHorizonsHeroes::SkillDefinition skillFor(const newHorizonsHeroes::PerkState & state)
{
	const auto skill = newHorizonsHeroes::perkSkill(state.getRules(), SKILL);
	if(!skill)
		throw std::runtime_error("Missing discipline fixture");
	return *skill;
}

heroGrowthPerkPresentation::Card cardFor(const newHorizonsHeroes::PerkState & state,
	const std::string & id, int rank = 3)
{
	const auto skill = skillFor(state);
	const auto perk = newHorizonsHeroes::perkDefinition(state.getRules(), SKILL, id);
	if(!perk)
		throw std::runtime_error("Missing fixture perk");
	return heroGrowthPerkPresentation::card(state, skill, rank, *perk);
}

heroGrowthPerkPresentation::SkillSummary summaryFor(const newHorizonsHeroes::PerkState & state, int rank = 3)
{
	return heroGrowthPerkPresentation::skillSummary(state, skillFor(state), rank,
		state.project([rank](const auto &) { return rank; }));
}

void expectContains(const std::string & text, const std::string & part)
{
	EXPECT_NE(text.find(part), std::string::npos) << text;
}

void markPlanned(newHorizonsHeroes::PerkState & state, const std::string & id)
{
	perkStateFixture::editRules(state, [&](JsonNode & rules)
	{
		for(auto & perk : rules["skills"][SKILL]["perks"].Vector())
			if(perk["id"].String() == id)
				perk["effect"]["status"].String() = "planned";
	});
}
}

TEST(HeroGrowthPerkPresentation, HistoricalSameTierAppearsInBothConsumersUntilCap)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto state = stateFor(true);
	ASSERT_TRUE(newHorizonsHeroes::usesR2PerkOffers(state.getRules()));
	ASSERT_FALSE(newHorizonsHeroes::usesStrictPerkProgression(state.getRules()));
	state.select(SKILL, BASIC, 1);
	const auto before = state.toJson();
	const auto card = cardFor(state, OTHER_BASIC, 1);
	EXPECT_EQ(card.status, "Available");
	expectContains(card.reason, "Eligible for a future level-up offer");
	const auto summary = summaryFor(state, 1);
	EXPECT_EQ(summary.eligible, 3);
	EXPECT_EQ(summary.locked, 6);
	EXPECT_EQ(summary.unavailable, 0);
	expectContains(summary.text, "Eligible for future level-up offers:");
	expectContains(summary.text, newHorizonsHeroes::perkDefinition(state.getRules(), SKILL, OTHER_BASIC)->name);
	EXPECT_TRUE(state.canAcquirePerk(SKILL, OTHER_BASIC, 1));
	EXPECT_EQ(state.toJson(), before);
	state.select(SKILL, OTHER_BASIC, 1);
	state.select(SKILL, ADVANCED, 2);
	EXPECT_EQ(summaryFor(state).eligible, 0);
	expectContains(summaryFor(state).text, "per-Skill perk limit");
	expectContains(cardFor(state, EXPERT).reason, "per-Skill perk limit");
}

TEST(HeroGrowthPerkPresentation, StrictAndPolicyOneRetainOnePerTier)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	for(const int policy : {1, 2})
	{
		auto state = stateFor();
		if(policy == 1)
		{
			perkStateFixture::editRules(state, [&](JsonNode & rules)
			{
				rules["offerWeightPolicyVersion"].Integer() = 1;
				rules.Struct().erase("perkOfferNormalization");
				rules.Struct().erase("perkProgressionPolicyVersion");
				rules.Struct().erase("unifiedSkillProgressionVersion");
			});
		}
		state.select(SKILL, BASIC, 1);
		const auto before = state.toJson();
		const auto card = cardFor(state, OTHER_BASIC, 1);
		EXPECT_EQ(card.status, "Locked");
		expectContains(card.reason, "one selected perk");
		const auto summary = summaryFor(state, 1);
		EXPECT_EQ(summary.eligible, 0);
		EXPECT_EQ(summary.locked, 9);
		expectContains(summary.text, "one selected perk");
		EXPECT_FALSE(state.canAcquirePerk(SKILL, OTHER_BASIC, 1));
		EXPECT_EQ(state.toJson(), before);
	}
}

TEST(HeroGrowthPerkPresentation, ExceptionalRanksAndRepeatedBasicDoNotSkipEarlierTiers)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	for(const bool historical : {false, true})
	{
		auto state = stateFor(historical);
		const auto before = state.toJson();
		EXPECT_EQ(cardFor(state, ADVANCED).status, "Locked");
		expectContains(cardFor(state, ADVANCED).reason, "earlier Discipline perk tier");
		expectContains(summaryFor(state).text, "earlier Discipline perk tier");
		EXPECT_EQ(summaryFor(state).eligible, 4);
		EXPECT_EQ(summaryFor(state).locked, 6);
		EXPECT_FALSE(state.canAcquirePerk(SKILL, ADVANCED, 3));
		EXPECT_EQ(state.toJson(), before);
		state.select(SKILL, BASIC, 3);
		if(historical)
			state.select(SKILL, OTHER_BASIC, 3);
		expectContains(cardFor(state, EXPERT).reason, "earlier Discipline perk tier");
		expectContains(summaryFor(state).text, "earlier Discipline perk tier");
		EXPECT_FALSE(state.canAcquirePerk(SKILL, EXPERT, 3));
	}
}

TEST(HeroGrowthPerkPresentation, PlannedAndSavedInactiveRemainVisibleWithoutAvailability)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto state = stateFor();
	markPlanned(state, BASIC);
	const auto before = state.toJson();
	EXPECT_EQ(cardFor(state, BASIC).status, "Unavailable");
	expectContains(cardFor(state, BASIC).reason, "planned and its effect is inactive");
	const auto unselected = summaryFor(state);
	EXPECT_EQ(unselected.eligible, 3);
	EXPECT_EQ(unselected.locked, 6);
	EXPECT_EQ(unselected.unavailable, 1);
	expectContains(unselected.text, "Unavailable: this perk is planned");
	EXPECT_FALSE(state.canAcquirePerk(SKILL, BASIC, 3));
	EXPECT_EQ(state.toJson(), before);
	// Only historical captured policy permits this retained planned selection.
	// Strict validation rejects it; never manufacture a strict saved history.
	state = stateFor(true);
	markPlanned(state, BASIC);
	perkStateFixture::editSelected(state, [&](auto & selected) { selected.push_back({SKILL, BASIC}); });
	ASSERT_FALSE(newHorizonsHeroes::usesStrictPerkProgression(state.getRules()));
	ASSERT_NO_THROW(state.validate());
	const auto selectedBefore = state.toJson();
	ASSERT_NO_THROW(state = newHorizonsHeroes::PerkState::fromJson(selectedBefore));
	ASSERT_EQ(state.toJson(), selectedBefore);
	EXPECT_EQ(cardFor(state, BASIC).status, "Acquired");
	expectContains(cardFor(state, BASIC).reason, "acquired, but its planned effect is inactive");
	expectContains(summaryFor(state).text, "(planned; inactive)");
	EXPECT_FALSE(state.canAcquirePerk(SKILL, BASIC, 3));
	EXPECT_EQ(state.toJson(), selectedBefore);
	// Active captured selection with an exceptionally lowered Skill rank stays acquired.
	state = stateFor();
	state.select(SKILL, BASIC, 1);
	EXPECT_EQ(cardFor(state, BASIC, 0).status, "Acquired");
	expectContains(summaryFor(state, 0).text, "(inactive; requires Basic)");
}

TEST(HeroGrowthPerkPresentation, AggregateCountsAndBrowserClassifierAgreeWithoutMutation)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	for(const bool historical : {false, true})
	{
		auto state = stateFor(historical);
		state.select(SKILL, BASIC, 1);
		markPlanned(state, EXPERT);
		const auto before = state.toJson();
		const auto skill = skillFor(state);
		const auto summary = summaryFor(state);
		size_t available = 0, locked = 0, unavailable = 0, acquired = 0;
		for(const auto & perk : skill.perks)
		{
			const auto card = heroGrowthPerkPresentation::card(state, skill, 3, perk);
			// This is the unchanged active browser's exact classification contract.
			const auto browser = newHorizonsPerkAvailability::evaluatePerk(state, SKILL, 3, perk);
			EXPECT_EQ(card.evaluation.status, browser.status);
			EXPECT_EQ(card.evaluation.reason, browser.reason);
			EXPECT_EQ(card.evaluation.requiredRank, browser.requiredRank);
			EXPECT_EQ(card.status, newHorizonsPerkAvailability::statusName(browser.status));
			EXPECT_EQ(card.reason, newHorizonsPerkAvailability::explanation(browser, skill.name));
			if(card.status == "Available")
				++available;
			else if(card.status == "Locked")
				++locked;
			else if(card.status == "Unavailable")
				++unavailable;
			else
				++acquired;
		}
		EXPECT_EQ(summary.eligible, available);
		EXPECT_EQ(summary.locked, locked);
		EXPECT_EQ(summary.unavailable, unavailable);
		EXPECT_EQ(available + locked + unavailable + acquired, skill.perks.size());
		const auto full = heroGrowthPerkPresentation::summary(state, [](const auto & id) { return id == SKILL ? 3 : 0; });
		expectContains(full, std::to_string(available) + " eligible for future level-up offers");
		expectContains(full, summary.text);
		EXPECT_EQ(state.toJson(), before);
	}
}

TEST(HeroGrowthPerkPresentation, MissingCapturedCatalogueFailsClosedEvenWithStaleDisplayDefinition)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	const auto captured = stateFor();
	const auto skill = skillFor(captured);
	newHorizonsHeroes::PerkState legacy;
	for(const auto & perk : skill.perks)
	{
		const auto card = heroGrowthPerkPresentation::card(legacy, skill, 3, perk);
		EXPECT_EQ(card.status, "Unavailable");
		expectContains(card.reason, "no matching saved perk catalogue");
	}
}

TEST(HeroGrowthPerkPresentation, FourOfferSummariesStartEmptyRetainOnePerTierAndRoundTrip)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	auto state = stateFor();
	perkStateFixture::editRules(state, [&](JsonNode & rules)
	{
		rules.Struct().erase("unifiedSkillProgressionVersion");
		rules["fourOfferProgressionVersion"].Integer() = 1;
	});
	EXPECT_TRUE(state.getSelected().empty());
	expectContains(summaryFor(state).text, "0/3 selected, 3 remaining");
	state.select(SKILL, BASIC, 3);
	const auto oneTier = state.toJson();
	EXPECT_THROW(state.select(SKILL, OTHER_BASIC, 3), std::runtime_error);
	EXPECT_EQ(state.toJson(), oneTier);
	state.select(SKILL, ADVANCED, 3);
	const auto restored = newHorizonsHeroes::PerkState::fromJson(state.toJson());
	EXPECT_EQ(restored.toJson(), state.toJson());
	EXPECT_EQ(summaryFor(restored).text, summaryFor(state).text);
	const auto before = state.toJson();
	const auto summary = summaryFor(state);
	expectContains(summary.text, newHorizonsHeroes::perkDefinition(state.getRules(), SKILL, BASIC)->name);
	expectContains(summary.text, newHorizonsHeroes::perkDefinition(state.getRules(), SKILL, ADVANCED)->name);
	expectContains(summary.text, "2/3 selected, 1 remaining");
	EXPECT_EQ(cardFor(state, ADVANCED).status, "Acquired");
	EXPECT_EQ(cardFor(state, EXPERT).status, "Available");
	const auto full = heroGrowthPerkPresentation::summary(state, [](const auto & id) { return id == SKILL ? 3 : 0; });
	expectContains(full, "up to two Skills and two perk groups; choose one advancement");
	EXPECT_EQ(full.find("a level-up offers at most"), std::string::npos);
	EXPECT_EQ(state.toJson(), before);
}
