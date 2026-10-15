/*
 * NewHorizonsPerkRulesTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"
#include "../../lib/entities/hero/NewHorizonsPerkRules.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/constants/EntityIdentifiers.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameSettings.h"
#include "../../lib/json/JsonUtils.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/entities/artifact/CArtifactInstance.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/MiscObjects.h"
#include "../../lib/mapObjects/ObjectTemplate.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/serializer/CMemorySerializer.h"

#include <stdexcept>

TEST(NewHorizonsPerkRules, CanonicalRegistryValidatesAndLookupOwnsItsData)
{
	using namespace newHorizonsHeroes;
	JsonNode rules(JsonPath::builtin("config/newHorizonsPerks"));
	ASSERT_TRUE(JsonUtils::validate(rules, "vcmi:newHorizonsPerks", "canonical perk rules"));
	ASSERT_NO_THROW(validatePerkRules(rules));
	const auto skill = perkSkill(rules, "new-horizons:offense");
	ASSERT_TRUE(skill);
	EXPECT_EQ(skill->perks.size(), PERK_CANONICAL_POOL_SIZE);
	const auto perk = perkDefinition(rules, skill->id, skill->perks.front().id);
	ASSERT_TRUE(perk);
	EXPECT_EQ(perkRequiredRank(perk->requiredRank), 1);
	rules["skills"].Struct().clear();
	EXPECT_EQ(perk->id, skill->perks.front().id);
}

TEST(NewHorizonsPerkRules, EmptyRegistryIsLegacyAndUnknownLookupStaysEmpty)
{
	using namespace newHorizonsHeroes;
	EXPECT_NO_THROW(validatePerkRules(JsonNode()));
	EXPECT_NO_THROW(validatePerkRules(JsonNode(JsonMap{})));
	EXPECT_FALSE(perkSkill(JsonNode(), "new-horizons:anything"));
	EXPECT_FALSE(perkDefinition(JsonNode(), "new-horizons:anything", "new-horizons:anything.perk"));
}

TEST(NewHorizonsPerkRules, VersionedCatalogAcceptsHistoricalAndCurrentButRejectsUnknownRevisions)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsPerks"));
	EXPECT_EQ(rules["rulesetVersion"].Integer(), 2);
	for(const int version : {1, 2})
	{
		rules["rulesetVersion"].Integer() = version;
		EXPECT_NO_THROW(newHorizonsHeroes::validatePerkRules(rules));
		EXPECT_TRUE(JsonUtils::validate(rules, "vcmi:newHorizonsPerks", "supported perk catalog"));
	}
	for(const int version : {0, 3})
	{
		rules["rulesetVersion"].Integer() = version;
		EXPECT_THROW(newHorizonsHeroes::validatePerkRules(rules), std::runtime_error);
		EXPECT_FALSE(JsonUtils::validate(rules, "vcmi:newHorizonsPerks", "unknown perk catalog"));
	}
}

TEST(NewHorizonsPerkRules, RuntimeRejectsIdentityCardinalityRankAndEffectForgery)
{
	using namespace newHorizonsHeroes;
	const JsonNode original(JsonPath::builtin("config/newHorizonsPerks"));
	std::vector<JsonNode> invalid(10, original);
	invalid[0]["skills"]["new-horizons:offense"]["id"].String() = "new-horizons:wrong";
	invalid[1]["skills"]["new-horizons:offense"]["perks"].Vector().pop_back();
	invalid[2]["skills"]["new-horizons:offense"]["perks"].Vector()[0]["requires"].String() = "master";
	invalid[3]["skills"]["new-horizons:offense"]["perks"].Vector()[0]["effect"]["status"].String() = "implemented";
	invalid[4]["skills"]["new-horizons:offense"]["perks"].Vector()[1]["id"] =
		invalid[4]["skills"]["new-horizons:offense"]["perks"].Vector()[0]["id"];
	invalid[5]["unexpected"].Bool() = true;
	invalid[6]["maxPerksPerSkill"].Integer() = 4;
	invalid[7]["skills"]["new-horizons:offense"]["perks"].Vector()[0]["id"].String() =
		"new-horizons:defense.borrowedPerk";
	invalid[8]["skills"]["new-horizons:offense"]["domain"].String() = "Anything";
	invalid[9]["skills"]["new-horizons:offense"]["availability"].String() = "Everyone sometimes";
	for(const auto & rules : invalid)
		EXPECT_THROW(validatePerkRules(rules), std::runtime_error);
}

TEST(NewHorizonsPerkRules, SettingsSchemaAcceptsTheActualRegistry)
{
	JsonNode settings;
	settings["heroes"]["newHorizonsPerks"] = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
	EXPECT_TRUE(JsonUtils::validate(settings, "vcmi:gameSettings", "perk settings wrapper"));
}

TEST(NewHorizonsPerkRules, EveryCanonicalSkillResolvesToItsExactRegisteredEntity)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsPerks"));
	for(const auto & [skillId, definition] : rules["skills"].Struct())
	{
		SCOPED_TRACE(skillId);
		const SecondarySkill decoded(SecondarySkill::decode(skillId));
		ASSERT_TRUE(decoded.hasValue());
		const auto * skill = LIBRARY->skills()->getById(decoded);
		ASSERT_NE(skill, nullptr);
		EXPECT_EQ(skill->getJsonKey(), skillId);
	}
}

TEST(NewHorizonsPerkRules, MissingOptionalSettingRemainsLegacyInsteadOfAsserting)
{
	GameSettings settings;
	EXPECT_TRUE(settings.getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS).isNull());
}

TEST(NewHorizonsPerkRules, WeightedOfferPolicyIsExplicitAndAbsentCapturedRegistryRemainsLegacy)
{
	using namespace newHorizonsHeroes;
	const JsonNode current(JsonPath::builtin("config/newHorizonsPerks"));
	ASSERT_EQ(current["offerWeightPolicyVersion"].Integer(), PERK_OFFER_WEIGHT_POLICY_VERSION);
	EXPECT_TRUE(usesWeightedPerkOffers(current));
	EXPECT_TRUE(JsonUtils::validate(current, "vcmi:newHorizonsPerks", "weighted perk registry"));
	EXPECT_NO_THROW(validateWeightedPerkSerialization(current, true, true, true));
	EXPECT_THROW(validateWeightedPerkSerialization(current, false), std::runtime_error);

	auto capturedLegacy = current;
	capturedLegacy.Struct().erase("offerWeightPolicyVersion");
	capturedLegacy.Struct().erase("perkOfferNormalization");
	capturedLegacy.Struct().erase("perkProgressionPolicyVersion");
	const auto before = capturedLegacy;
	EXPECT_NO_THROW(validatePerkRules(capturedLegacy));
	EXPECT_FALSE(usesWeightedPerkOffers(capturedLegacy));
	EXPECT_NO_THROW(validateWeightedPerkSerialization(capturedLegacy, false));
	EXPECT_TRUE(JsonUtils::validate(capturedLegacy, "vcmi:newHorizonsPerks", "captured legacy perk registry"));
	EXPECT_EQ(capturedLegacy, before);
	EXPECT_FALSE(capturedLegacy.Struct().contains("offerWeightPolicyVersion"));
	EXPECT_FALSE(usesWeightedPerkOffers(JsonNode()));
	EXPECT_FALSE(usesWeightedPerkOffers(JsonNode(JsonMap{})));
}

TEST(NewHorizonsPerkRules, WeightedOfferPolicyRejectsMalformedAndUnknownVersions)
{
	using namespace newHorizonsHeroes;
	const JsonNode current(JsonPath::builtin("config/newHorizonsPerks"));
	std::vector<JsonNode> invalid = {JsonNode(), JsonNode(false), JsonNode(0), JsonNode(-1),
		JsonNode(3), JsonNode(1.5), JsonNode("1"), JsonNode(JsonMap{})};
	JsonNode array;
	array.Vector().emplace_back(1);
	invalid.push_back(array);
	for(const auto & marker : invalid)
	{
		SCOPED_TRACE(marker.toString());
		auto rules = current;
		rules["offerWeightPolicyVersion"] = marker;
		const auto before = rules;
		EXPECT_THROW(validatePerkRules(rules), std::runtime_error);
		EXPECT_THROW(usesWeightedPerkOffers(rules), std::runtime_error);
		EXPECT_THROW(validateWeightedPerkSerialization(rules, true), std::runtime_error);
		EXPECT_THROW(validateWeightedPerkSerialization(rules, false), std::runtime_error);
		EXPECT_FALSE(JsonUtils::validate(rules, "vcmi:newHorizonsPerks", "malformed weighted perk marker"));
		EXPECT_EQ(rules, before);
	}
}

TEST(NewHorizonsPerkRules, GlobalSettingsCodecGuardsWeightedPolicyBeforeInstallingOverridesWithoutHeroes)
{
	JsonNode current(JsonPath::builtin("config/newHorizonsPerks"));
	// Retain this exact historical policy1/1113 boundary control.
	current["offerWeightPolicyVersion"].Integer() = 1;
	current.Struct().erase("perkOfferNormalization");
	current.Struct().erase("perkProgressionPolicyVersion");
	current.setOverrideFlag(true);
	JsonNode noDevelopment(JsonMap{});
	noDevelopment.setOverrideFlag(true);
	ASSERT_TRUE(newHorizonsHeroes::usesWeightedPerkOffers(current));
	auto legacy = current;
	legacy.Struct().erase("offerWeightPolicyVersion");
	GameSettings source;
	source.addOverride(EGameSettings::HEROES_NEW_HORIZONS, noDevelopment);
	source.addOverride(EGameSettings::HEROES_NEW_HORIZONS_PERKS, current);
	ASSERT_FALSE(newHorizonsHeroes::usesRules(source.getValue(EGameSettings::HEROES_NEW_HORIZONS)));
	CMemorySerializer oldWrite;
	oldWrite.oser.version = ESerializationVersion::NEW_HORIZONS_HERO_LUCK;
	EXPECT_THROW(source.serialize(oldWrite.oser), std::runtime_error);
	EXPECT_TRUE(oldWrite.extractBuffer().empty()); // preflight, before any override bytes.

	CMemorySerializer currentWire;
	currentWire.oser.version = ESerializationVersion::NEW_HORIZONS_WEIGHTED_PERKS_AND_DRUID_SPECIALTIES;
	currentWire.iser.version = ESerializationVersion::NEW_HORIZONS_WEIGHTED_PERKS_AND_DRUID_SPECIALTIES;
	ASSERT_NO_THROW(source.serialize(currentWire.oser));
	GameSettings decoded;
	ASSERT_NO_THROW(decoded.serialize(currentWire.iser));
	EXPECT_EQ(decoded.getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS), current);
	EXPECT_FALSE(newHorizonsHeroes::usesRules(decoded.getValue(EGameSettings::HEROES_NEW_HORIZONS)));

	// JsonNode's raw codec remains readable; actual GameSettings admission
	// rejects that known newer policy before replacing existing overrides.
	JsonNode raw;
	raw["heroes"]["newHorizons"] = noDevelopment;
	raw["heroes"]["newHorizonsPerks"] = current;
	CMemorySerializer oldRead;
	oldRead.oser & raw;
	oldRead.iser.version = ESerializationVersion::NEW_HORIZONS_HERO_LUCK;
	GameSettings unchanged;
	unchanged.addOverride(EGameSettings::HEROES_NEW_HORIZONS, noDevelopment);
	unchanged.addOverride(EGameSettings::HEROES_NEW_HORIZONS_PERKS, legacy);
	const auto before = unchanged.getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS);
	EXPECT_THROW(unchanged.serialize(oldRead.iser), std::runtime_error);
	EXPECT_EQ(unchanged.getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS), before);
	EXPECT_FALSE(newHorizonsHeroes::usesWeightedPerkOffers(before));

	GameSettings oldSource;
	oldSource.addOverride(EGameSettings::HEROES_NEW_HORIZONS, noDevelopment);
	oldSource.addOverride(EGameSettings::HEROES_NEW_HORIZONS_PERKS, legacy);
	CMemorySerializer legacyWire;
	legacyWire.oser.version = ESerializationVersion::NEW_HORIZONS_HERO_LUCK;
	legacyWire.iser.version = ESerializationVersion::NEW_HORIZONS_HERO_LUCK;
	ASSERT_NO_THROW(oldSource.serialize(legacyWire.oser));
	GameSettings oldDecoded;
	ASSERT_NO_THROW(oldDecoded.serialize(legacyWire.iser));
	EXPECT_EQ(oldDecoded.getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS), legacy);
	EXPECT_FALSE(oldDecoded.getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS).Struct().contains("offerWeightPolicyVersion"));
}

TEST(NewHorizonsPerkRules, EmptyMapCodecDelegatesGlobalWeightedGuardWithoutAnyHeroOrDruidPolicy)
{
	JsonNode current(JsonPath::builtin("config/newHorizonsPerks"));
	// Retain this exact historical policy1/1113 boundary control.
	current["offerWeightPolicyVersion"].Integer() = 1;
	current.Struct().erase("perkOfferNormalization");
	current.Struct().erase("perkProgressionPolicyVersion");
	current.setOverrideFlag(true);
	auto legacy = current;
	legacy.Struct().erase("offerWeightPolicyVersion");
	JsonNode noDevelopment(JsonMap{});
	noDevelopment.setOverrideFlag(true);
	CMap source(nullptr);
	source.width = source.height = 1;
	source.mapLayers = {MapLayerId::SURFACE};
	source.initTerrain();
	source.overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, noDevelopment);
	source.overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, current);
	ASSERT_TRUE(source.objects.empty());
	ASSERT_TRUE(source.getHeroesOnMap().empty());
	ASSERT_TRUE(source.getHeroesInPool().empty());
	ASSERT_FALSE(newHorizonsHeroes::usesRules(source.getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS)));
	CMemorySerializer oldWrite;
	oldWrite.oser.version = ESerializationVersion::NEW_HORIZONS_HERO_LUCK;
	EXPECT_THROW(source.serialize(oldWrite.oser), std::runtime_error);
	EXPECT_TRUE(oldWrite.extractBuffer().empty()); // outer map guard, before its header.

	CMemorySerializer currentWire;
	currentWire.oser.version = ESerializationVersion::NEW_HORIZONS_WEIGHTED_PERKS_AND_DRUID_SPECIALTIES;
	currentWire.iser.version = ESerializationVersion::NEW_HORIZONS_WEIGHTED_PERKS_AND_DRUID_SPECIALTIES;
	ASSERT_NO_THROW(source.serialize(currentWire.oser));
	const auto bytes = currentWire.extractBuffer();
	CMemorySerializer currentRead(bytes);
	currentRead.iser.version = ESerializationVersion::NEW_HORIZONS_WEIGHTED_PERKS_AND_DRUID_SPECIALTIES;
	CMap decoded(nullptr);
	ASSERT_NO_THROW(decoded.serialize(currentRead.iser));
	EXPECT_TRUE(decoded.objects.empty());
	EXPECT_TRUE(decoded.getHeroesOnMap().empty());
	EXPECT_TRUE(decoded.getHeroesInPool().empty());
	EXPECT_EQ(decoded.getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS), current);
	EXPECT_FALSE(newHorizonsHeroes::usesRules(decoded.getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS)));

	CMemorySerializer olderRead(bytes);
	olderRead.iser.version = ESerializationVersion::NEW_HORIZONS_HERO_LUCK;
	CMap refused(nullptr);
	refused.overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, noDevelopment);
	refused.overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, legacy);
	EXPECT_THROW(refused.serialize(olderRead.iser), std::runtime_error);
	// The map header has already been decoded; only override installation is
	// asserted atomic here, not a transactional whole-map decoder guarantee.
	EXPECT_EQ(refused.getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS), legacy);

	source.overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, legacy);
	CMemorySerializer legacyWire;
	legacyWire.oser.version = ESerializationVersion::NEW_HORIZONS_HERO_LUCK;
	legacyWire.iser.version = ESerializationVersion::NEW_HORIZONS_HERO_LUCK;
	ASSERT_NO_THROW(source.serialize(legacyWire.oser));
	CMap oldDecoded(nullptr);
	ASSERT_NO_THROW(oldDecoded.serialize(legacyWire.iser));
	EXPECT_EQ(oldDecoded.getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS), legacy);
	EXPECT_FALSE(newHorizonsHeroes::usesWeightedPerkOffers(oldDecoded.getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS)));
}

TEST(NewHorizonsPerkRules, R2NormalizationRequiresFiniteRatioAtLeastOneAndExactMathVersion)
{
	const JsonNode baseline(JsonPath::builtin("config/newHorizonsPerks"));
	ASSERT_TRUE(newHorizonsHeroes::usesR2PerkOffers(baseline));
	ASSERT_NO_THROW(newHorizonsHeroes::validatePerkRules(baseline));
	ASSERT_TRUE(JsonUtils::validate(baseline, "vcmi:newHorizonsPerks", "R2 baseline"));
	for(const auto & invalid : {JsonNode(), JsonNode(false), JsonNode("2"), JsonNode(0.0), JsonNode(0.5)})
	{
		SCOPED_TRACE(invalid.toCompactString());
		auto bad = baseline;
		bad["perkOfferNormalization"]["maximumPreferenceRatio"] = invalid;
		EXPECT_THROW(newHorizonsHeroes::validatePerkRules(bad), std::runtime_error);
		EXPECT_FALSE(JsonUtils::validate(bad, "vcmi:newHorizonsPerks", "R2 malformed ratio"));
	}
	auto bad = baseline;
	bad["perkOfferNormalization"] = JsonNode();
	EXPECT_THROW(newHorizonsHeroes::validatePerkRules(bad), std::runtime_error);
	EXPECT_FALSE(JsonUtils::validate(bad, "vcmi:newHorizonsPerks", "R2 null normalization"));
	bad = baseline;
	bad["perkOfferNormalization"]["mathVersion"].Integer() = 2;
	EXPECT_THROW(newHorizonsHeroes::validatePerkRules(bad), std::runtime_error);
	for(double ratio : {1.0, 1.5, 2.0, 3.0})
	{
		auto valid = baseline;
		valid["perkOfferNormalization"]["maximumPreferenceRatio"].Float() = ratio;
		EXPECT_NO_THROW(newHorizonsHeroes::validatePerkRules(valid));
	}
}

TEST(NewHorizonsPerkRules, R2GlobalOnlyCodecRejectsPreviousEpochBeforeWritingAndPreservesOldAbsence)
{
	JsonNode current(JsonPath::builtin("config/newHorizonsPerks"));
	current.Struct().erase("perkProgressionPolicyVersion"); // Explicit historical R2 codec.
	current.setOverrideFlag(true);
	GameSettings settings;
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS_PERKS, current);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_MASTER_SPELLWARD;
	EXPECT_THROW(settings.serialize(old.oser), std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	CMemorySerializer wire;
	ASSERT_NO_THROW(settings.serialize(wire.oser));
	GameSettings decoded;
	ASSERT_NO_THROW(decoded.serialize(wire.iser));
	EXPECT_EQ(decoded.getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS), current);
	auto legacy = current;
	legacy.Struct().erase("offerWeightPolicyVersion");
	legacy.Struct().erase("perkOfferNormalization");
	legacy.Struct().erase("perkProgressionPolicyVersion");
	GameSettings oldSettings;
	oldSettings.addOverride(EGameSettings::HEROES_NEW_HORIZONS_PERKS, legacy);
	CMemorySerializer oldWire;
	oldWire.oser.version = ESerializationVersion::NEW_HORIZONS_MASTER_SPELLWARD;
	EXPECT_NO_THROW(oldSettings.serialize(oldWire.oser));
}

TEST(NewHorizonsPerkRules, R2RawPreviousSettingsReaderRejectsBeforeReplacingPolicyOne)
{
	JsonNode current(JsonPath::builtin("config/newHorizonsPerks"));
	current.Struct().erase("perkProgressionPolicyVersion"); // Explicit historical R2 codec.
	ASSERT_EQ(current["offerWeightPolicyVersion"].Integer(), 2);
	current.setOverrideFlag(true);
	auto policyOne = current;
	policyOne["offerWeightPolicyVersion"].Integer() = 1;
	policyOne.Struct().erase("perkOfferNormalization");
	policyOne.Struct().erase("perkProgressionPolicyVersion");
	JsonNode noDevelopment(JsonMap{});
	noDevelopment.setOverrideFlag(true);
	GameSettings target;
	target.addOverride(EGameSettings::HEROES_NEW_HORIZONS, noDevelopment);
	target.addOverride(EGameSettings::HEROES_NEW_HORIZONS_PERKS, policyOne);
	// Successful binary loads deliberately capture these historical defaults.
	// Make them explicit so full-byte equality also checks the policy-one round trip.
	JsonNode finalLuck(false);
	JsonNode moraleDamage(100);
	target.addOverride(EGameSettings::COMBAT_NEW_HORIZONS_FINAL_LUCK, finalLuck);
	target.addOverride(EGameSettings::COMBAT_MORALE_EXTRA_DAMAGE_PERCENT, moraleDamage);
	const auto snapshot = [](GameSettings & settings)
	{
		CMemorySerializer memory;
		settings.serialize(memory.oser);
		return memory.extractBuffer();
	};
	const auto before = snapshot(target);

	// Forge only the raw settings payload; the normal writer refuses R2.
	JsonNode raw;
	raw["heroes"]["newHorizons"] = noDevelopment;
	raw["heroes"]["newHorizonsPerks"] = current;
	CMemorySerializer forged;
	forged.oser.version = ESerializationVersion::NEW_HORIZONS_MASTER_SPELLWARD;
	forged.iser.version = forged.oser.version;
	forged.oser & raw;
	EXPECT_THROW(target.serialize(forged.iser), std::runtime_error);
	EXPECT_EQ(snapshot(target), before);

	CMemorySerializer previous;
	previous.oser.version = ESerializationVersion::NEW_HORIZONS_MASTER_SPELLWARD;
	previous.iser.version = previous.oser.version;
	ASSERT_NO_THROW(target.serialize(previous.oser));
	GameSettings restored;
	ASSERT_NO_THROW(restored.serialize(previous.iser));
	EXPECT_EQ(snapshot(restored), before);
	EXPECT_EQ(restored.getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS), policyOne);
}

TEST(NewHorizonsPerkRules, R2EmptyMapCodecGuardsPreviousEpochWithoutHeroesAndRetainsPolicyOne)
{
	JsonNode current(JsonPath::builtin("config/newHorizonsPerks"));
	current.Struct().erase("perkProgressionPolicyVersion"); // Explicit historical R2 codec.
	ASSERT_EQ(current["offerWeightPolicyVersion"].Integer(), 2);
	current.setOverrideFlag(true);
	auto policyOne = current;
	policyOne["offerWeightPolicyVersion"].Integer() = 1;
	policyOne.Struct().erase("perkOfferNormalization");
	policyOne.Struct().erase("perkProgressionPolicyVersion");
	JsonNode noDevelopment(JsonMap{});
	noDevelopment.setOverrideFlag(true);
	CMap source(nullptr);
	source.width = source.height = 1;
	source.mapLayers = {MapLayerId::SURFACE};
	source.initTerrain();
	source.overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, noDevelopment);
	source.overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, current);
	ASSERT_TRUE(source.objects.empty());
	ASSERT_TRUE(source.getHeroesOnMap().empty());
	ASSERT_TRUE(source.getHeroesInPool().empty());
	ASSERT_FALSE(newHorizonsHeroes::usesRules(
		source.getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS)));
	CMemorySerializer previousWrite;
	previousWrite.oser.version = ESerializationVersion::NEW_HORIZONS_MASTER_SPELLWARD;
	EXPECT_THROW(source.serialize(previousWrite.oser), std::runtime_error);
	EXPECT_TRUE(previousWrite.extractBuffer().empty());

	CMemorySerializer currentWire;
	ASSERT_NO_THROW(source.serialize(currentWire.oser));
	const auto bytes = currentWire.extractBuffer();
	CMemorySerializer currentRead(bytes);
	CMap decoded(nullptr);
	ASSERT_NO_THROW(decoded.serialize(currentRead.iser));
	EXPECT_EQ(decoded.getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS), current);
	EXPECT_TRUE(decoded.objects.empty());
	EXPECT_TRUE(decoded.getHeroesOnMap().empty());
	EXPECT_TRUE(decoded.getHeroesInPool().empty());
	CMemorySerializer previousRead(bytes);
	previousRead.iser.version = ESerializationVersion::NEW_HORIZONS_MASTER_SPELLWARD;
	CMap refused(nullptr);
	refused.overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, noDevelopment);
	refused.overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, policyOne);
	EXPECT_THROW(refused.serialize(previousRead.iser), std::runtime_error);
	// Only override installation is atomic; the map header may be decoded.
	EXPECT_EQ(refused.getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS), policyOne);

	source.overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, policyOne);
	CMemorySerializer policyOneWire;
	policyOneWire.oser.version = ESerializationVersion::NEW_HORIZONS_MASTER_SPELLWARD;
	policyOneWire.iser.version = policyOneWire.oser.version;
	ASSERT_NO_THROW(source.serialize(policyOneWire.oser));
	CMap oldDecoded(nullptr);
	ASSERT_NO_THROW(oldDecoded.serialize(policyOneWire.iser));
	EXPECT_EQ(oldDecoded.getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS), policyOne);
}

TEST(NewHorizonsPerkRules, StrictProgressionMarkerIsOrthogonalExactAndHistoricalAbsenceIsPreserved)
{
	const JsonNode current(JsonPath::builtin("config/newHorizonsPerks"));
	ASSERT_EQ(current["offerWeightPolicyVersion"].Integer(), 2);
	ASSERT_EQ(current["perkProgressionPolicyVersion"].Integer(), 1);
	ASSERT_TRUE(newHorizonsHeroes::usesStrictPerkProgression(current));
	EXPECT_TRUE(newHorizonsHeroes::usesR2PerkOffers(current));
	auto historical = current;
	historical.Struct().erase("perkProgressionPolicyVersion");
	const auto before = historical;
	EXPECT_FALSE(newHorizonsHeroes::usesStrictPerkProgression(historical));
	EXPECT_TRUE(newHorizonsHeroes::usesR2PerkOffers(historical));
	EXPECT_NO_THROW(newHorizonsHeroes::validateWeightedPerkSerialization(historical, true, true));
	EXPECT_EQ(historical, before);
	for(const auto & marker : {JsonNode(), JsonNode(false), JsonNode(0), JsonNode(2), JsonNode(1.0), JsonNode(1.5), JsonNode("1")})
	{
		auto invalid = current;
		invalid["perkProgressionPolicyVersion"] = marker;
		EXPECT_THROW(newHorizonsHeroes::validatePerkRules(invalid), std::runtime_error);
		EXPECT_FALSE(JsonUtils::validate(invalid, "vcmi:newHorizonsPerks", "invalid strict progression marker"));
	}
	auto mismatched = current;
	mismatched["offerWeightPolicyVersion"].Integer() = 1;
	mismatched.Struct().erase("perkOfferNormalization");
	EXPECT_THROW(newHorizonsHeroes::validatePerkRules(mismatched), std::runtime_error);
}

TEST(NewHorizonsPerkRules, StrictSettings1117FenceRejectsBeforeWriteAndBeforeOverrideReplacement)
{
	JsonNode current(JsonPath::builtin("config/newHorizonsPerks"));
	ASSERT_TRUE(newHorizonsHeroes::usesStrictPerkProgression(current));
	current.setOverrideFlag(true);
	auto historical = current;
	historical.Struct().erase("perkProgressionPolicyVersion");
	GameSettings source;
	source.addOverride(EGameSettings::HEROES_NEW_HORIZONS_PERKS, current);
	CMemorySerializer refused;
	refused.oser.version = ESerializationVersion::NEW_HORIZONS_R2_PERK_OFFERS;
	EXPECT_THROW(source.serialize(refused.oser), std::runtime_error);
	EXPECT_TRUE(refused.extractBuffer().empty());
	GameSettings target;
	target.addOverride(EGameSettings::HEROES_NEW_HORIZONS_PERKS, historical);
	const auto bytes = [](GameSettings & value)
	{
		CMemorySerializer wire;
		value.serialize(wire.oser);
		return wire.extractBuffer();
	};
	const auto before = bytes(target);
	JsonNode raw;
	raw["heroes"]["newHorizonsPerks"] = current;
	CMemorySerializer forged;
	forged.oser.version = forged.iser.version = ESerializationVersion::NEW_HORIZONS_R2_PERK_OFFERS;
	forged.oser & raw; // Deliberately bypass ONLY the writer for this malformed input.
	EXPECT_THROW(target.serialize(forged.iser), std::runtime_error);
	EXPECT_EQ(bytes(target), before);
	CMemorySerializer oldWire;
	oldWire.oser.version = oldWire.iser.version = ESerializationVersion::NEW_HORIZONS_R2_PERK_OFFERS;
	ASSERT_NO_THROW(target.serialize(oldWire.oser));
	GameSettings restored;
	ASSERT_NO_THROW(restored.serialize(oldWire.iser));
	EXPECT_EQ(restored.getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS), historical);
	CMemorySerializer currentWire;
	ASSERT_NO_THROW(source.serialize(currentWire.oser));
	GameSettings currentRead;
	ASSERT_NO_THROW(currentRead.serialize(currentWire.iser));
	EXPECT_EQ(currentRead.getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS), current);
}

TEST(NewHorizonsPerkRules, StrictEmptyMap1117WriterRefusesBeforeHeaderAndCurrentCodecPreservesMarker)
{
	JsonNode current(JsonPath::builtin("config/newHorizonsPerks"));
	current.setOverrideFlag(true);
	CMap source(nullptr);
	source.width = source.height = 1;
	source.mapLayers = {MapLayerId::SURFACE};
	source.initTerrain();
	source.overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, current);
	ASSERT_TRUE(source.objects.empty());
	CMemorySerializer refused;
	refused.oser.version = ESerializationVersion::NEW_HORIZONS_R2_PERK_OFFERS;
	EXPECT_THROW(source.serialize(refused.oser), std::runtime_error);
	EXPECT_TRUE(refused.extractBuffer().empty());
	CMemorySerializer wire;
	ASSERT_NO_THROW(source.serialize(wire.oser));
	CMap decoded(nullptr);
	ASSERT_NO_THROW(decoded.serialize(wire.iser));
	EXPECT_EQ(decoded.getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS), current);
	EXPECT_TRUE(decoded.objects.empty());
}
