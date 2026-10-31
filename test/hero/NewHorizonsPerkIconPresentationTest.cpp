/* Part of VCMI / New Horizons; GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../client/windows/NewHorizonsCapturedPerkIcons.h"
#include "../../lib/entities/hero/NewHorizonsPerkRules.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
constexpr auto skillId = "new-horizons:sorceryMagic";
constexpr auto stableId = "new-horizons:sorceryMagic.countermage";
constexpr auto oldShield = "NH_perk_countermage";
constexpr auto newBallistics = "NH_perk_arcane_ballistics";

newHorizonsHeroes::PerkDefinition definition(const JsonNode & rules)
{
	const auto perk = newHorizonsHeroes::perkDefinition(rules, skillId, stableId);
	if(!perk)
		throw std::runtime_error("Missing Arcane Ballistics fixture");
	return *perk;
}

}

TEST(NewHorizonsPerkIconPresentation, CurrentDefinitionUsesNewArtWithoutChangingCapturedRules)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsPerks"));
	const auto before = rules;
	const auto perk = definition(rules);
	ASSERT_EQ(perk.name, "Arcane Ballistics");
	EXPECT_EQ(newHorizonsPerkIcon(perk.id, perk.name), newBallistics);
	EXPECT_EQ(newHorizonsPerkIcon(perk.id), oldShield);
	EXPECT_EQ(definition(rules), perk);
	EXPECT_EQ(rules, before);
}

TEST(NewHorizonsPerkIconPresentation, HistoricalCountermageUnknownAndLocalizedNamesRetainShield)
{
	auto captured = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
	for(auto & row : captured["skills"][skillId]["perks"].Vector())
		if(row["id"].String() == stableId)
		{
			row["name"].String() = "Countermage";
			row["description"].String() = "Counterspell uses 1.75 times the hostile spell's listed mana cost.";
			row["effect"]["description"] = row["description"];
		}
	const auto historical = definition(captured);
	const auto before = captured;
	EXPECT_EQ(historical.id, stableId);
	EXPECT_EQ(historical.name, "Countermage");
	EXPECT_EQ(newHorizonsPerkIcon(historical.id, historical.name), oldShield);
	EXPECT_EQ(newHorizonsPerkIcon(historical.id, ""), oldShield);
	EXPECT_EQ(newHorizonsPerkIcon(historical.id, "Unknown revised perk"), oldShield);
	EXPECT_EQ(newHorizonsPerkIcon(historical.id, "Balística Arcana"), oldShield);
	EXPECT_EQ(captured, before);
}

TEST(NewHorizonsPerkIconPresentation, ImmutableWireOfferRetainsItsOwnPresentationIdentity)
{
	const JsonNode installed(JsonPath::builtin("config/newHorizonsPerks"));
	auto current = definition(installed);
	for(const bool historical : {false, true})
	{
		auto supplied = current;
		if(historical)
		{
			supplied.name = "Countermage";
			supplied.description = "Historical Countermage offer";
		}
		HeroLevelUp outgoing;
		outgoing.heroId = ObjectInstanceID(7);
		outgoing.player = PlayerColor(0);
		outgoing.perks = {{{skillId, supplied.id}, supplied.name, supplied.description,
			newHorizonsHeroes::perkRequiredRank(supplied.requiredRank)}};
		CMemorySerializer wire;
		wire.oser & outgoing;
		HeroLevelUp incoming;
		wire.iser & incoming;
		ASSERT_EQ(incoming.perks, outgoing.perks);
		ASSERT_EQ(incoming.perks.size(), 1u);
		const auto & saved = incoming.perks.front();
		EXPECT_EQ(newHorizonsPerkIcon(saved.selection.perkId, saved.name), historical ? oldShield : newBallistics);
		EXPECT_EQ(definition(installed), current);
	}
}

TEST(NewHorizonsPerkIconPresentation, UnrelatedStableIdentitiesAndFallbacksAreUnchanged)
{
	for(const auto & [id, resource] : newHorizonsPerkIconBindings)
	{
		if(id == std::string_view(stableId))
			continue;
		EXPECT_EQ(newHorizonsPerkIcon(id, "Arcane Ballistics"), resource);
		EXPECT_EQ(newHorizonsPerkIcon(id, "Countermage"), resource);
	}
	EXPECT_EQ(newHorizonsPerkIcon("unknown:perk", "Arcane Ballistics"), "NH_perk_neutral");
}

namespace
{
struct SylvanArtExpectation
{
	const char * id;
	const char * name;
	const char * resource;
};
constexpr SylvanArtExpectation sylvanArt[] = {
		{"new-horizons:sylvanLuck.elvenPrecision", "Fleetfoot", "NH_perk_sylvan_tactics_fleetfoot"},
		{"new-horizons:sylvanLuck.forestSFavor", "Patient Hunter", "NH_perk_sylvan_tactics_patient_hunter"},
		{"new-horizons:sylvanLuck.serendipity", "Dappled Cover", "NH_perk_sylvan_tactics_dappled_cover"},
		{"new-horizons:sylvanLuck.luckyRecovery", "Measured Advance", "NH_perk_sylvan_tactics_measured_advance"},
		{"new-horizons:sylvanLuck.sharedFortune", "Covering Fire", "NH_perk_sylvan_tactics_covering_fire"},
		{"new-horizons:sylvanLuck.natureSProvidence", "Crossfire", "NH_perk_sylvan_tactics_crossfire"},
		{"new-horizons:sylvanLuck.fortunateAim", "Disruptive Strike", "NH_perk_sylvan_tactics_disruptive_strike"},
		{"new-horizons:sylvanLuck.wildChance", "Quick Redeployment", "NH_perk_sylvan_tactics_quick_redeployment"},
		{"new-horizons:sylvanLuck.perfectMoment", "Perfect Moment", "NH_perk_sylvan_tactics_perfect_moment"},
		{"new-horizons:sylvanLuck.cascadingFortune", "Wild Hunt", "NH_perk_sylvan_tactics_wild_hunt"},
};
}

TEST(NewHorizonsPerkIconPresentation, SylvanArtworkUsesCapturedPolicyAndExactDefinitionIdentity)
{
	const JsonNode current(JsonPath::builtin("config/newHorizonsPerks"));
	ASSERT_TRUE(newHorizonsHeroes::usesSylvanTactics(current));
	const auto before = current;
	auto legacy = current;
	legacy.Struct().erase("sylvanTacticsPolicy");
	ASSERT_FALSE(newHorizonsHeroes::usesSylvanTactics(legacy));
	for(const auto & expected : sylvanArt)
	{
		SCOPED_TRACE(expected.id);
		const auto perk = newHorizonsHeroes::perkDefinition(current, "new-horizons:sylvanLuck", expected.id);
		ASSERT_TRUE(perk);
		ASSERT_EQ(perk->name, expected.name);
		EXPECT_EQ(newHorizonsPerkIcon(perk->id, perk->name, current), expected.resource);
		// Names alone cannot distinguish historical Perfect Moment.
		EXPECT_EQ(newHorizonsPerkIcon(perk->id, perk->name, legacy), newHorizonsPerkIcon(perk->id));
		EXPECT_EQ(newHorizonsPerkIcon(perk->id, perk->name, JsonNode()), newHorizonsPerkIcon(perk->id));
		EXPECT_EQ(newHorizonsPerkIcon(perk->id, "Unknown revised name", current), newHorizonsPerkIcon(perk->id));
		EXPECT_EQ(newHorizonsPerkIcon(perk->id, "", current), newHorizonsPerkIcon(perk->id));
	}
	EXPECT_EQ(current, before);
}

TEST(NewHorizonsPerkIconPresentation, SylvanArtworkPreservesOtherFamiliesAndHistoricalResolver)
{
	const JsonNode current(JsonPath::builtin("config/newHorizonsPerks"));
	for(const auto & [id, resource] : newHorizonsPerkIconBindings)
	{
		EXPECT_EQ(newHorizonsPerkIcon(id), resource);
		EXPECT_EQ(newHorizonsPerkIcon(id, "unrelated captured name", current), resource);
	}
	EXPECT_EQ(newHorizonsPerkIcon("new-horizons:archery.crossfire", "Crossfire", current), "NH_perk_crossfire");
	EXPECT_EQ(newHorizonsPerkIcon("new-horizons:sylvanLuck.perfectMoment"), "NH_perk_perfect_moment");
	EXPECT_EQ(newHorizonsPerkIcon(stableId, "Arcane Ballistics", current), newBallistics);
	EXPECT_EQ(newHorizonsPerkIcon(stableId, "Countermage", current), oldShield);
	EXPECT_EQ(newHorizonsPerkIcon("unknown:perk", "Fleetfoot", current), "NH_perk_neutral");
}

TEST(NewHorizonsPerkIconPresentation, SylvanImmutableOfferKeepsItsCapturedArtContextAfterWireReload)
{
	const JsonNode current(JsonPath::builtin("config/newHorizonsPerks"));
	auto legacy = current;
	legacy.Struct().erase("sylvanTacticsPolicy");
	for(const auto & expected : sylvanArt)
	{
		HeroLevelUp outgoing;
		outgoing.heroId = ObjectInstanceID(7);
		outgoing.player = PlayerColor(0);
		outgoing.perks = {{{"new-horizons:sylvanLuck", expected.id}, expected.name, "Captured offer", 1}};
		CMemorySerializer wire;
		wire.oser & outgoing;
		HeroLevelUp incoming;
		wire.iser & incoming;
		ASSERT_EQ(incoming.perks, outgoing.perks);
		const auto & saved = incoming.perks.front();
		EXPECT_EQ(newHorizonsPerkIcon(saved.selection.perkId, saved.name, current), expected.resource);
		EXPECT_EQ(newHorizonsPerkIcon(saved.selection.perkId, saved.name, legacy), newHorizonsPerkIcon(expected.id));
	}
}
