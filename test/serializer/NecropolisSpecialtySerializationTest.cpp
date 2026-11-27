/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/GameSettings.h"
#include "../../lib/entities/hero/NewHorizonsNecropolisCombatSpecialties.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/battle/SideInBattle.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/NecropolisSpecialtyPacks.h"
#include "../../lib/serializer/CMemorySerializer.h"
namespace
{
UnitChanges necropolisRestoration(int restored = 2, bool original = false)
{
	UnitChanges result(9, UnitChanges::EOperation::UPDATE);
	auto & cohort = result.data["state"][original ? "battleFormOriginalHealth" : "health"]["casualtyProvenance"].Vector().emplace_back();
	cohort["count"].Integer() = 5;
	cohort["provenance"].Integer() = 0;
	cohort["temporarilyRestored"].Integer() = 3;
	cohort["eternalServitudeRestored"].Integer() = restored;
	return result;
}
}
TEST(NecropolisSpecialtySerialization, ResidualSideRoundtripAndOldReadDefaultsZero)
{
	SideInBattle source(nullptr), restored(nullptr);
	source.residualLifeDamage = 91;
	CMemorySerializer current;
	source.serialize(current.oser);
	restored.serialize(current.iser);
	EXPECT_EQ(restored.residualLifeDamage, 91);
	CMemorySerializer historical;
	historical.oser.version = historical.iser.version = ESerializationVersion::NEW_HORIZONS_MARCH_LEFT;
	source.residualLifeDamage = 0;
	source.serialize(historical.oser);
	restored.serialize(historical.iser);
	EXPECT_EQ(restored.residualLifeDamage, 0);
}
TEST(NecropolisSpecialtySerialization, NonzeroResidualRefusesOldWriterBeforePrefix)
{
	SideInBattle source(nullptr);
	source.residualLifeDamage = 1;
	CMemorySerializer historical;
	historical.oser.version = ESerializationVersion::NEW_HORIZONS_MARCH_LEFT;
	EXPECT_THROW(source.serialize(historical.oser), std::runtime_error);
	EXPECT_TRUE(historical.extractBuffer().empty());
	source.residualLifeDamage = -1;
	CMemorySerializer malformed;
	EXPECT_THROW(source.serialize(malformed.oser), std::runtime_error);
	EXPECT_TRUE(malformed.extractBuffer().empty());
}
TEST(NecropolisSpecialtySerialization, EternalHealthAndOriginalFormRoundtripAndOldRefusal)
{
	for(const bool original : {false, true})
	{
		auto source = necropolisRestoration(2, original);
		CMemorySerializer current;
		source.serialize(current.oser);
		UnitChanges restored;
		restored.serialize(current.iser);
		EXPECT_EQ(restored.data, source.data);
		CMemorySerializer historical;
		historical.oser.version = ESerializationVersion::NEW_HORIZONS_MARCH_LEFT;
		EXPECT_THROW(source.serialize(historical.oser), std::runtime_error);
		EXPECT_TRUE(historical.extractBuffer().empty());
	}
}
TEST(NecropolisSpecialtySerialization, InvalidMarkerRefusesBeforeUnitPrefix)
{
	for(const int value : {-1, 4})
	{
		auto source = necropolisRestoration(value);
		CMemorySerializer malformed;
		EXPECT_THROW(source.serialize(malformed.oser), std::runtime_error);
		EXPECT_TRUE(malformed.extractBuffer().empty());
	}
}
TEST(NecropolisSpecialtySerialization, ReceiptRoundtripAndOldRefusal)
{
	RecordResidualLife source;
	source.battleID = BattleID(4);
	source.side = BattleSide::ATTACKER;
	source.damage = 67;
	CMemorySerializer current;
	source.serialize(current.oser);
	RecordResidualLife restored;
	restored.serialize(current.iser);
	EXPECT_EQ(restored.battleID, source.battleID);
	EXPECT_EQ(restored.side, source.side);
	EXPECT_EQ(restored.damage, source.damage);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_MARCH_LEFT;
	EXPECT_THROW(source.serialize(old.oser), std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
}

TEST(NecropolisSpecialtySerialization, NewStartingBookRefusesOldFormatEvenWithoutOptionalSpecialties)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
	for(const auto * key : {"hoardedPower", "residualLife", "earthboundDrain", "eternalServitude", "unyieldingBones",
		"bloodScent", "soulHarvest", "ancientOathsDiscount"})
		rules["revisedSpecialties"].Struct().erase(key);
	ASSERT_TRUE(newHorizonsHeroes::requiresNecropolisSpecialtyFormat(rules));
	GameSettings settings;
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, rules);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_MARCH_LEFT;
	EXPECT_THROW(settings.serialize(old.oser), std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	rules["startingSkills"]["startingBookReplacements"].Struct().erase("core:aislinn");
	EXPECT_FALSE(newHorizonsHeroes::requiresNecropolisSpecialtyFormat(rules));
}
TEST(NecropolisSpecialtySerialization, WorldHeroOverridesRetainTheStartingBookFormatClaim)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
	for(const auto * key : {"hoardedPower", "residualLife", "earthboundDrain", "eternalServitude", "unyieldingBones",
		"bloodScent", "soulHarvest", "ancientOathsDiscount"})
		rules["revisedSpecialties"].Struct().erase(key);
	for(const auto * key : {"newHorizons", "newHorizonsPerks"})
	{
		JsonNode wrapped;
		wrapped["heroes"][key] = rules;
		EXPECT_TRUE(newHorizonsHeroes::requiresNecropolisSpecialtyFormat(wrapped));
	}
}
