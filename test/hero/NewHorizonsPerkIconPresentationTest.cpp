/* Part of VCMI / New Horizons; GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../client/windows/NewHorizonsPerkIcons.h"
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
