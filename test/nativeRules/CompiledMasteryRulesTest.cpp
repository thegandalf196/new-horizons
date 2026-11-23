/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../OriginalDataRequirement.h"
#include "../../lib/nativeRules/CapturedRuleRef.h"
#include "../../lib/filesystem/ResourcePath.h"

using CompiledMasteryRules = vcmiTestData::OriginalDataTest;

TEST_F(CompiledMasteryRules, EverySupportedPolicyMatchesRawOptionsAndRetainsAbsence)
{
	JsonNode current(JsonPath::builtin("config/newHorizonsMasteries"));
	JsonNode historical = current;
	historical["rulesetVersion"].Integer() = 1;
	historical["skills"].Struct().erase("core:logistics");
	for(const auto & raw : {JsonNode(), historical, current})
	{
		nativeRules::CapturedRuleRef<nativeRules::CapturedRuleKind::Mastery> captured(raw);
		for(const auto skill : {SecondarySkill::ARTILLERY, SecondarySkill::LOGISTICS, SecondarySkill::WISDOM})
			EXPECT_EQ(captured.masteryOptions(skill), newHorizonsHeroes::masteryOptions(raw, skill));
	}
}

TEST_F(CompiledMasteryRules, IndependentOwnersKeepOldMagnitudesAndRejectMalformedReplacement)
{
	JsonNode raw(JsonPath::builtin("config/newHorizonsMasteries"));
	nativeRules::CapturedRuleRef<nativeRules::CapturedRuleKind::Mastery> old(raw);
	const auto owner = old.owner();
	const auto before = old.masteryOptions(SecondarySkill::ARTILLERY);
	ASSERT_TRUE(before);
	raw["skills"]["core:artillery"]["options"][0]["magnitude"].Integer() = 2;
	nativeRules::CapturedRuleRef<nativeRules::CapturedRuleKind::Mastery> changed(raw);
	EXPECT_EQ(old.masteryOptions(SecondarySkill::ARTILLERY), before);
	EXPECT_NE(changed.masteryOptions(SecondarySkill::ARTILLERY), before);
	raw["skills"]["core:artillery"]["options"][1]["magnitude"].Integer() = 2;
	EXPECT_THROW(old.replace(raw), std::runtime_error);
	EXPECT_EQ(old.owner(), owner);
	EXPECT_EQ(old.masteryOptions(SecondarySkill::ARTILLERY), before);
}
