/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../OriginalDataRequirement.h"
#include "../OverkillRandomPrayerRules.h"
#include "../../client/battle/CurrentFactionSkillPresentation.h"
#include "../../lib/nativeRules/CapturedPerkRules.h"
#include "../../lib/nativeRules/CompiledMetamagicRules.h"
#include "../../lib/json/JsonNode.h"

using namespace passiveFactionSkillPresentation;

TEST(HeroMetamagicPresentation, OptionalUnravelReadoutUsesCapturedChanceWithoutRetiredPointBenefits)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	const auto owner = nativeRules::CapturedPerkRules::compile(overkillRandomPrayerRules());
	const auto & rules = owner->metamagic();
	const int rank = 2;
	const auto status = metamagicText({rank, rules.overflowPercent(rank), rules.friendlyReductionPercent(rank),
		rules.debuffRetryPercent(rank), rules.removalChance(rank, HeroTypeID::NONE), std::nullopt});
	ASSERT_TRUE(status);
	EXPECT_EQ(status->value, "45%");
	EXPECT_NE(status->details.find("Unraveling Magic: 30% chance"), std::string::npos);
	EXPECT_EQ(status->details.find("Reconstituting Magic"), std::string::npos);
	for(const auto * retired : {"Metamagic Points", "Army Initiative", "Spell penetration", "Mana costs reduced"})
		EXPECT_EQ(status->details.find(retired), std::string::npos);
	EXPECT_TRUE(status->replacesProviderDescription);
}

TEST(HeroMetamagicPresentation, ReconstitutingAndCascadeDescribeTheirOwnSelectedEffects)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	const auto owner = nativeRules::CapturedPerkRules::compile(overkillRandomPrayerRules());
	const auto & rules = owner->metamagic();
	const int rank = 3;
	const auto status = metamagicText({rank, rules.overflowPercent(rank), 100,
		rules.debuffRetryPercent(rank), std::nullopt, rules.removalChance(rank, HeroTypeID::NONE), true});
	ASSERT_TRUE(status);
	EXPECT_EQ(status->value, "60%");
	EXPECT_NE(status->details.find("Reconstituting Magic: 40% chance"), std::string::npos);
	EXPECT_EQ(status->details.find("Unraveling Magic"), std::string::npos);
	EXPECT_NE(status->details.find("two nearest other distinct enemies"), std::string::npos);
	EXPECT_NE(status->details.find("0% damage to friendly creatures"), std::string::npos);
	EXPECT_NE(status->details.find("retry the normal resistance check once. Immunity is not bypassed."), std::string::npos);
	EXPECT_EQ(status->details.find("activates upon reaching"), std::string::npos);
}

TEST(HeroMetamagicPresentation, MissingRankAndUnselectedOptionalRemovalDoNotCreateAStatusOrPerk)
{
	EXPECT_FALSE(metamagicText({0, 60, 100, 60, 40, 40}));
	const auto absent = metamagicText({2, 45, 45, 45, std::nullopt, std::nullopt});
	const auto zeroChance = metamagicText({2, 45, 45, 45, std::nullopt, 0});
	ASSERT_TRUE(absent);
	ASSERT_TRUE(zeroChance);
	EXPECT_EQ(absent->details.find("Reconstituting Magic"), std::string::npos);
	EXPECT_NE(zeroChance->details.find("Reconstituting Magic: 0% chance"), std::string::npos);
	EXPECT_EQ(absent->value, zeroChance->value);
}
