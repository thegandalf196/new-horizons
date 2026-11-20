/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../OverkillRandomPrayerRules.h"
#include "../../lib/spells/NewHorizonsMetamagicDoubleCast.h"
#include "../../lib/spells/NewHorizonsMetamagicResolution.h"
#include "../../lib/json/JsonNode.h"
#include <functional>

using namespace newHorizonsMetamagic;

namespace
{
JsonNode installed() { return JsonNode(JsonPath::builtin("config/newHorizonsPerks")); }

JsonNode changed(const std::function<void(JsonNode &)> & mutate)
{
	auto rules = installed();
	mutate(rules["metamagicPolicy"]);
	return rules;
}
}

TEST(NewHorizonsMetamagicDoubleCast, InstalledPolicySevenDecodesEveryAuthoredValue)
{
	const auto policy = doubleCastPolicy(installed());
	ASSERT_TRUE(policy);
	EXPECT_EQ(policy->usesPerCombatByRank, (std::array<int32_t, 4>{0, 1, 2, 3}));
	EXPECT_TRUE(policy->followUpMustDiffer);
	EXPECT_EQ(policy->halonAdditionalUses, 1);
	EXPECT_EQ(policy->arcaneWardSpellPowerMultiplier, 5);
	EXPECT_EQ(policy->manaSiphonMana, 4);
	EXPECT_EQ(policy->deepReservesAdditionalUses, 1);
	EXPECT_EQ(policy->formulaReserveMana, 3);
	EXPECT_EQ(policy->arcaneCycleRoundInterval, 3);
	EXPECT_EQ(policy->afterglowAdditionalRounds, 1);
	EXPECT_EQ(policy->crescendoSpellPowerPerSpentUse, 5);
	EXPECT_EQ(policy->cleansingWeaveEffects, 1);
	EXPECT_EQ(policy->grandWeavingAdditionalUses, 2);
	EXPECT_EQ(policy->spellstormManaReductionPercent, 25);
	EXPECT_EQ(policy->spellstormDoubleCastThreshold, 3);
	EXPECT_TRUE(claimsDoubleCastPolicy(installed()));
}

TEST(NewHorizonsMetamagicDoubleCast, PoliciesSixAndSevenNeverDecodeAsEachOther)
{
	EXPECT_FALSE(resolutionPolicy(installed()));
	const auto overkill = overkillRandomPrayerRules();
	ASSERT_TRUE(resolutionPolicy(overkill));
	EXPECT_FALSE(doubleCastPolicy(overkill));
	EXPECT_FALSE(claimsDoubleCastPolicy(overkill));
	EXPECT_FALSE(doubleCastPolicy(JsonNode()));
}

TEST(NewHorizonsMetamagicDoubleCast, IncompleteUnknownOrOutOfRangeDocumentsDoNotDecode)
{
	std::vector<JsonNode> invalid;
	const auto source = installed();
	for(const auto & [key, value] : source["metamagicPolicy"].Struct())
		if(key != "version")
		{
			invalid.push_back(changed([&key](JsonNode & p) { p.Struct().erase(key); }));
			invalid.push_back(changed([&key](JsonNode & p) { p[key] = JsonNode("1"); }));
			invalid.push_back(changed([&key](JsonNode & p) { p[key].Float() = 1.5; }));
		}
	invalid.push_back(changed([](JsonNode & p) { p["unknown"].Integer() = 1; }));
	invalid.push_back(changed([](JsonNode & p) { p["followUpMustDiffer"].Integer() = 1; }));
	invalid.push_back(changed([](JsonNode & p) { p["halonAdditionalUses"].Integer() = -1; }));
	invalid.push_back(changed([](JsonNode & p) { p["halonAdditionalUses"].Integer() = 4; }));
	invalid.push_back(changed([](JsonNode & p) { p["arcaneCycleRoundInterval"].Integer() = 0; }));
	invalid.push_back(changed([](JsonNode & p) { p["spellstormManaReductionPercent"].Integer() = 91; }));
	invalid.push_back(changed([](JsonNode & p) { p["spellstormDoubleCastThreshold"].Integer() = 0; }));
	invalid.push_back(changed([](JsonNode & p) { p["usesPerCombatByRank"].Vector()[0].Integer() = 1; }));
	invalid.push_back(changed([](JsonNode & p) { p["usesPerCombatByRank"].Vector()[1].Integer() = 0; }));
	invalid.push_back(changed([](JsonNode & p) { p["usesPerCombatByRank"].Vector()[3].Integer() = 1; }));
	invalid.push_back(changed([](JsonNode & p) { p["usesPerCombatByRank"].Vector()[3].Integer() = 6; }));
	invalid.push_back(changed([](JsonNode & p) { p["usesPerCombatByRank"].Vector().pop_back(); }));
	invalid.push_back(changed([](JsonNode & p) { p["version"].Integer() = 8; }));
	for(const auto & rules : invalid)
		EXPECT_FALSE(doubleCastPolicy(rules)) << rules["metamagicPolicy"].toCompactString();
}

TEST(NewHorizonsMetamagicDoubleCast, AuthoredBoundsRemainDecodableForBalanceChanges)
{
	const auto tuned = changed([](JsonNode & p)
	{
		p["usesPerCombatByRank"].Vector() = {JsonNode(0), JsonNode(2), JsonNode(2), JsonNode(5)};
		p["spellstormManaReductionPercent"].Integer() = 0;
		p["arcaneCycleRoundInterval"].Integer() = 10;
		p["followUpMustDiffer"].Bool() = false;
	});
	const auto policy = doubleCastPolicy(tuned);
	ASSERT_TRUE(policy);
	EXPECT_EQ(policy->usesPerCombatByRank[3], 5);
	EXPECT_EQ(policy->spellstormManaReductionPercent, 0);
	EXPECT_EQ(policy->arcaneCycleRoundInterval, 10);
	EXPECT_FALSE(policy->followUpMustDiffer);
}
