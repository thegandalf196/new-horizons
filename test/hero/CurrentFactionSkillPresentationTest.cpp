/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../OriginalDataRequirement.h"
#include "../OverkillRandomPrayerRules.h"
#include "../../client/battle/CurrentFactionSkillPresentation.h"
#include "../../lib/nativeRules/CapturedPerkRules.h"
#include "../../lib/nativeRules/CompiledMetamagicRules.h"
#include "../../lib/json/JsonNode.h"

using namespace passiveFactionSkillPresentation;

TEST(CurrentFactionSkillPresentation, ShippingSkillsHaveExplicitRoutesWithoutLegacyProviders)
{
	const JsonNode skills(JsonPath::builtin("config/newHorizonsSkills"));
	ASSERT_FALSE(skills["metamagic"].isNull());
	ASSERT_FALSE(skills["divineMandate"].isNull());
	EXPECT_TRUE(skills["metamagic"]["combatStatus"].isNull());
	EXPECT_TRUE(skills["divineMandate"]["combatStatus"].isNull());
	EXPECT_EQ(currentSkill("new-horizons:metamagic"), CurrentSkill::METAMAGIC);
	EXPECT_EQ(currentSkill("new-horizons:divineMandate"), CurrentSkill::CONSECRATED_ARMS);
	EXPECT_EQ(currentSkill("new-horizons:bloodrage"), CurrentSkill::NONE);
}

TEST(CurrentFactionSkillPresentation, CapturedCoreUsesCurrentRankNumbersWithoutCounters)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	// Overflow status text belongs to captured policy 6; Double Cast status is Phase 2 UI.
	const auto owner = nativeRules::CapturedPerkRules::compile(overkillRandomPrayerRules());
	const auto & rules = owner->metamagic();
	for(int rank = 1; rank <= 3; ++rank)
	{
		const auto text = metamagicText({rank, rules.overflowPercent(rank), rules.friendlyReductionPercent(rank),
			rules.debuffRetryPercent(rank), std::nullopt, std::nullopt});
		ASSERT_TRUE(text);
		const int percent = std::array{30, 45, 60}[rank - 1];
		EXPECT_EQ(text->value, std::to_string(percent) + "%");
		EXPECT_NE(text->details.find(std::to_string(100 - percent) + "% damage to friendly"), std::string::npos);
		EXPECT_NE(text->details.find(std::to_string(percent) + "% chance to retry"), std::string::npos);
		EXPECT_EQ(text->details.find("remove each"), std::string::npos);
		EXPECT_EQ(text->details.find("points"), std::string::npos);
		EXPECT_TRUE(text->replacesProviderDescription);
	}
	EXPECT_FALSE(metamagicText({}));
	// Historical profiles have no current core channel: do not present one.
	EXPECT_FALSE(metamagicText({2, 0, 0, 0, std::nullopt, std::nullopt}));
}

TEST(CurrentFactionSkillPresentation, OptionalRemovalAndSculptingReadBackExplicitInputs)
{
	const auto text = metamagicText({3, 60, 100, 60, 35, std::nullopt});
	ASSERT_TRUE(text);
	EXPECT_NE(text->details.find("0% damage to friendly"), std::string::npos);
	EXPECT_NE(text->details.find("Unraveling Magic: 35%"), std::string::npos);
	EXPECT_EQ(text->details.find("Reconstituting Magic"), std::string::npos);
	const auto cascade = metamagicText({3, 60, 60, 60, std::nullopt, std::nullopt, true});
	ASSERT_TRUE(cascade);
	EXPECT_NE(cascade->details.find("60% of excess damage to each of the two nearest other distinct enemies"), std::string::npos);
	EXPECT_NE(text->details.find("to the nearest other enemy"), std::string::npos);
}

TEST(CurrentFactionSkillPresentation, FaithDurationAndPotencyUseSeparateCapturedInputs)
{
	consecratedArms::Policy policy{2, 0, 50, 100};
	const auto text = consecratedText(policy, 149);
	ASSERT_TRUE(text);
	EXPECT_EQ(text->value, "F 149");
	EXPECT_NE(text->details.find("Potency Faith: 199"), std::string::npos);
	EXPECT_NE(text->details.find("duration: 2 rounds"), std::string::npos);
	EXPECT_EQ(text->details.find("Light"), std::string::npos);
	EXPECT_TRUE(text->replacesProviderDescription);
	EXPECT_FALSE(consecratedText({}, 149));
}

TEST(CurrentFactionSkillPresentation, RecipientUsesCapturedPrayerFaithAndRemainingRounds)
{
	using namespace consecratedArms;
	PrayerSnapshot prayers;
	EXPECT_TRUE(prayerTexts(prayers).empty());
	Policy policy{2, static_cast<uint16_t>(Perk::SACRED_ACCORD)};
	prayers[0] = ActivePrayer{Prayer::DIVINE_PURPOSE, 149, 1, policy};
	prayers[2] = ActivePrayer{Prayer::STEADFAST_SOUL, 200, 3, policy};
	const auto rows = prayerTexts(prayers);
	ASSERT_EQ(rows.size(), 2);
	EXPECT_EQ(rows[0].value, "Divine Purpose (1 round)");
	EXPECT_NE(rows[0].details.find("Potency Faith: 149"), std::string::npos);
	EXPECT_NE(rows[1].details.find("22%"), std::string::npos);
	EXPECT_NE(rows[1].details.find("Potency Faith: 200"), std::string::npos);
	EXPECT_NE(rows[1].details.find("Remaining duration: 3 rounds"), std::string::npos);
	EXPECT_EQ(prayerPercent(1745), "17.45%");
}

TEST(CurrentFactionSkillPresentation, PrayerHelpIncludesOnlyItsCapturedAdvancedPerk)
{
	using namespace consecratedArms;
	ActivePrayer judgment{Prayer::HEAVENS_JUDGMENT, 200, 2, Policy{2}};
	EXPECT_EQ(prayerEffect(judgment, 1).find("50% greater"), std::string::npos);
	judgment.policy.perks = static_cast<uint16_t>(Perk::RIGHTEOUS_SENTENCE);
	EXPECT_NE(prayerEffect(judgment, 1).find("50% greater against enemies that have already completed an action this round"), std::string::npos);
	ActivePrayer redemption{Prayer::VICARIOUS_REDEMPTION, 200, 2, Policy{2}};
	EXPECT_EQ(prayerEffect(redemption, 1).find("Other friendly stacks"), std::string::npos);
	redemption.policy.perks = static_cast<uint16_t>(Perk::COMMUNION_OF_SAINTS);
	EXPECT_NE(prayerEffect(redemption, 1).find("Other friendly stacks' casualties also grant shield at half this conversion rate"), std::string::npos);
}
