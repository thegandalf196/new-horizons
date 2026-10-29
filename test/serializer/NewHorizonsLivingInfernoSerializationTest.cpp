/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../lib/GameSettings.h"
#include "../../lib/entities/hero/NewHorizonsLivingInferno.h"
#include "../../lib/entities/hero/NewHorizonsSpecialtySerialization.h"
#include "../../lib/serializer/CMemorySerializer.h"
namespace
{
JsonNode livingInfernoRules()
{
	JsonNode rules;
	rules["livingInfernoSpecialty"]["version"].Integer() = 1;
	rules["livingInfernoSpecialty"]["damagePerCreature"].Integer() = 1;
	rules["livingInfernoSpecialty"]["durationRounds"].Integer() = 1;
	return rules;
}
}
TEST(NewHorizonsLivingInfernoSerialization, CurrentSettingsPreserveExactOptionalPolicy)
{
	GameSettings settings;
	const auto rules = livingInfernoRules();
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, rules);
	CMemorySerializer wire;
	settings.serialize(wire.oser);
	GameSettings restored;
	restored.serialize(wire.iser);
	EXPECT_EQ(restored.getValue(EGameSettings::HEROES_NEW_HORIZONS), rules);
}
TEST(NewHorizonsLivingInfernoSerialization, OldWriterRejectsBeforeSettingsPrefix)
{
	GameSettings settings;
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, livingInfernoRules());
	CMemorySerializer wire;
	wire.oser.version = ESerializationVersion::NEW_HORIZONS_RECRUITMENT_GROWTH;
	EXPECT_THROW(settings.serialize(wire.oser), std::runtime_error);
	EXPECT_TRUE(wire.extractBuffer().empty());
}
TEST(NewHorizonsLivingInfernoSerialization, AbsentCaptureIsNeverReplaced)
{
	JsonNode old;
	const auto before = old;
	EXPECT_NO_THROW(newHorizonsHeroes::validateLivingInfernoSerialization(old, false));
	EXPECT_EQ(old, before);
	EXPECT_FALSE(newHorizonsHeroes::livingInfernoRules(old));
}
TEST(NewHorizonsLivingInfernoSerialization, MalformedPolicyAndUnknownFieldsReject)
{
	for(const auto field : {"version", "damagePerCreature", "durationRounds"})
	{
		auto rules = livingInfernoRules();
		rules["livingInfernoSpecialty"].Struct().erase(field);
		EXPECT_THROW(newHorizonsHeroes::validateLivingInfernoSerialization(rules, true), std::runtime_error);
	}
	auto negative = livingInfernoRules();
	negative["livingInfernoSpecialty"]["damagePerCreature"].Integer() = -1;
	EXPECT_THROW(newHorizonsHeroes::validateLivingInfernoSerialization(negative, true), std::runtime_error);
	auto unknown = livingInfernoRules();
	unknown["livingInfernoSpecialty"]["spellPower"].Integer() = 1;
	EXPECT_THROW(newHorizonsHeroes::validateLivingInfernoSerialization(unknown, true), std::runtime_error);
	EXPECT_THROW(newHorizonsHeroes::validateLivingInfernoSerialization(livingInfernoRules(), false), std::runtime_error);
}
