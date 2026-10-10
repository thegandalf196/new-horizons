/*
 * GameSettings.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "IGameSettings.h"
#include "pathfinder/NewHorizonsLighthouse.h"
#include "json/JsonNode.h"
#include "entities/hero/NewHorizonsHeroRules.h"
#include "spells/NewHorizonsMagic.h"
#include "entities/hero/NewHorizonsCapabilityRules.h"
#include "battle/NewHorizonsPlague.h"
#include "entities/hero/NewHorizonsPerkRules.h"
#include "pathfinder/NewHorizonsProtectedMobility.h"
#include <optional>

class DLL_LINKAGE GameSettings final : public IGameSettings, boost::noncopyable
{
	struct SettingOption
	{
		EGameSettings setting;
		std::string group;
		std::string key;
	};

	static constexpr int32_t OPTIONS_COUNT = static_cast<int32_t>(EGameSettings::OPTIONS_COUNT);
	static const std::vector<SettingOption> settingProperties;

	// contains base settings, like those defined in base game or mods
	std::array<JsonNode, OPTIONS_COUNT> baseSettings;
	// contains settings that were overriden, in map or in random map template
	std::array<JsonNode, OPTIONS_COUNT> overridenSettings;
	// An explicit null versioned context differs from an absent override.
	bool magicOverridePresent = false;
	bool heroCommandsOverridePresent = false;
	// for convenience / performance, contains actual settings - combined version of base and override settings
	std::array<JsonNode, OPTIONS_COUNT> actualSettings;

	// converts all existing overrides into a single json node for serialization
	JsonNode getAllOverrides() const;
	void loadSavedOverrides(const JsonNode & input);

public:
	static void validateCombatScalarOverrides(const JsonNode & input, bool finalLuckSupported, bool moraleSupported);
	void validateCombatScalarSerialization(bool finalLuckSupported, bool moraleSupported) const;
	void validateProtectedAdventureMobilitySerialization(bool supported) const
	{
		newHorizonsProtectedMobility::validateRulesSerialization(getAllOverrides()["magic"]["newHorizons"], supported);
	}
	GameSettings();
	~GameSettings();

	/// Loads settings as 'base settings' that can be overriden
	/// For settings defined in vcmi or in mods
	void loadBase(const JsonNode & input);

	/// Loads setting as an override, for use in maps or rmg templates
	/// undefined behavior if setting was already overriden (TODO: decide which approach is better - replace or append)
	void addOverride(EGameSettings option, const JsonNode & input);

	// loads all overrides from provided json node, for deserialization
	void loadOverrides(const JsonNode &);

	std::optional<JsonNode> getMagicOverride() const;
	JsonNode getFullConfig() const override;
	const JsonNode & getValue(EGameSettings option) const override;
	void validateNewHorizonsHasteSpecialtySerialization(bool supported) const
	{
		newHorizonsHeroes::validateHasteSpecialtySerialization(
			getAllOverrides()["heroes"]["newHorizons"], supported);
	}
	void validateNewHorizonsThantReanimateSerialization(bool supported) const
	{
		newHorizonsHeroes::validateReanimateSpecialtySerialization(
			getAllOverrides()["heroes"]["newHorizons"], supported);
	}
	void validateNewHorizonsFrailtySpecialtySerialization(bool supported) const
	{
		newHorizonsHeroes::validateFrailtySpecialtySerialization(
			getAllOverrides()["heroes"]["newHorizons"], supported);
	}

	void validateNewHorizonsAenainFrailtySpecialtySerialization(bool supported) const
	{
		newHorizonsHeroes::validateAenainFrailtySpecialtySerialization(
			getAllOverrides()["heroes"]["newHorizons"], supported);
	}

	void validateNewHorizonsDefensiveStartSpecialtySerialization(bool supported) const
	{
		newHorizonsHeroes::validateDefensiveStartSpecialtySerialization(
			getAllOverrides()["heroes"]["newHorizons"], supported);
	}

	void validateNewHorizonsOffensiveStartSpecialtySerialization(bool supported) const
	{
		newHorizonsHeroes::validateOffensiveStartSpecialtySerialization(
			getAllOverrides()["heroes"]["newHorizons"], supported);
	}

	void validateNewHorizonsRemainingSpellSpecialtySerialization(bool supported) const
	{
		newHorizonsHeroes::validateRemainingSpellSpecialtySerialization(
			getAllOverrides()["heroes"]["newHorizons"], supported);
	}

	void validateNewHorizonsArtifactManaRegenerationSerialization(bool supported) const
	{
		newHorizonsHeroes::validateArtifactManaRegenerationSerialization(
			getAllOverrides()["heroes"]["newHorizonsCapabilities"], supported);
	}

	void validateNewHorizonsGlyphsOfFearSerialization(bool supported) const
	{
		newHorizonsHeroes::validateGlyphsOfFearSerialization(
			getAllOverrides()["heroes"]["newHorizonsCapabilities"], supported);
	}

	void validateNewHorizonsLighthouseSerialization(bool supported) const
	{
		newHorizonsLighthouse::validateRulesSerialization(
			getAllOverrides()["heroes"]["newHorizons"], supported);
	}

	void validateNewHorizonsStartingDevelopmentSerialization(bool supported) const
	{
		newHorizonsHeroes::validateStartingDevelopmentSerialization(
			getAllOverrides()["heroes"]["newHorizons"], supported);
	}
	void validateNavigationStartSerialization(bool supported) const
	{
		newHorizonsHeroes::validateNavigationStartSerialization(getAllOverrides()["heroes"]["newHorizons"], supported);
	}

	void validateDefaultCreatureLineSerialization(bool supported) const
	{
		newHorizonsHeroes::validateDefaultCreatureLineSerialization(getAllOverrides()["heroes"]["newHorizons"], supported);
	}

	void validateNewHorizonsRemainingStartSerialization(bool supported) const
	{
		newHorizonsHeroes::validateRemainingStartSerialization(
			getAllOverrides()["heroes"]["newHorizons"], supported);
	}

	void validateNewHorizonsWaterWalkDayEndSerialization(bool supported) const
	{
		newHorizonsMagic::validateWaterWalkDayEndSerialization(getAllOverrides()["magic"]["newHorizons"], supported);
	}

	void validateCanonicalSpellClausesSerialization(bool supported) const
	{
		newHorizonsMagic::validateCanonicalSpellClausesSerialization(getAllOverrides()["magic"]["newHorizons"], supported);
	}

	void validateNewHorizonsCoroniusHolyWrathSerialization(bool supported) const
	{
		newHorizonsHeroes::validateCoroniusHolyWrathSerialization(
			getAllOverrides()["heroes"]["newHorizons"], supported);
	}

	void validatePlagueRulesSerialization(bool supported) const
	{
		newHorizonsPlague::validateRuleSerialization(getAllOverrides()["magic"]["newHorizons"], supported);
	}

	void validateCrisisCommandSerialization(bool supported) const
	{
		newHorizonsHeroes::validateCrisisCommandProfileSerialization(
			getAllOverrides()["heroes"]["newHorizonsPerks"], supported);
	}

	void validateNewHorizonsStartingBookSerialization(bool supported) const
	{
		newHorizonsHeroes::validateStartingBookSerialization(
			getAllOverrides()["heroes"]["newHorizons"], supported);
	}

	template<typename Handler>
	void serialize(Handler & h)
	{
		if (h.saving)
		{
			JsonNode overrides = getAllOverrides();
			validateCombatScalarOverrides(overrides,
				h.hasFeature(Handler::Version::NEW_HORIZONS_FINAL_LUCK),
				h.hasFeature(Handler::Version::NEW_HORIZONS_MORALE_EXTRA_DAMAGE));
			newHorizonsMagic::validateCanonicalSpellClausesSerialization(overrides["magic"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_IMPLOSION));
			newHorizonsHeroes::validateNavigationStartSerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_NAVIGATION_START_REPLACEMENTS));
			newHorizonsHeroes::validateDefaultCreatureLineSerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_DEFAULT_CREATURE_LINE_SUCCESSORS));
			newHorizonsProtectedMobility::validateRulesSerialization(overrides["magic"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_PROTECTED_ADVENTURE_BARRIERS));
			newHorizonsMagic::validateWaterWalkDayEndSerialization(overrides["magic"]["newHorizons"], h.hasFeature(Handler::Version::NEW_HORIZONS_WATER_WALK_DAY_END));
			newHorizonsHeroes::validateArtifactManaRegenerationSerialization(overrides["heroes"]["newHorizonsCapabilities"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_ARTIFACT_MANA_REGENERATION));
			newHorizonsHeroes::validateGlyphsOfFearSerialization(overrides["heroes"]["newHorizonsCapabilities"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_GLYPHS_OF_FEAR_AURA));
			newHorizonsPlague::validateRuleSerialization(overrides["magic"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_PLAGUEBEARER));
			newHorizonsHeroes::validateCrisisCommandProfileSerialization(overrides["heroes"]["newHorizonsPerks"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_CRISIS_COMMAND));
			newHorizonsHeroes::validateStartingDevelopmentSerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_STARTING_DEVELOPMENT_PROFILES));
			newHorizonsHeroes::validateStartingBookSerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_STARTING_BOOK_REPLACEMENTS));
			newHorizonsHeroes::validateRemainingStartSerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_REMAINING_START_REPLACEMENTS));
			newHorizonsHeroes::validateCoroniusHolyWrathSerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_CORONIUS_HOLY_WRATH));
			newHorizonsHeroes::validateFrailtySpecialtySerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_FRAILTY_SPECIALTIES));
			newHorizonsHeroes::validateAenainFrailtySpecialtySerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_AENAIN_FRAILTY_SPECIALTY));
			newHorizonsHeroes::validateReanimateSpecialtySerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_THANT_REANIMATE));
			newHorizonsHeroes::validateDefensiveStartSpecialtySerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_DEFENSIVE_START_SPECIALTIES));
			newHorizonsHeroes::validateOffensiveStartSpecialtySerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_OFFENSIVE_START_SPECIALTIES));
			newHorizonsHeroes::validateRemainingSpellSpecialtySerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_REMAINING_SPELL_SPECIALTIES));
			newHorizonsLighthouse::validateRulesSerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_LIGHTHOUSE_DEPARTURE));
			newHorizonsHeroes::validateHasteSpecialtySerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_HASTE_SPECIALTIES));
			h & overrides;
		}
		else
		{
			JsonNode overrides;
			h & overrides;
			validateCombatScalarOverrides(overrides,
				h.hasFeature(Handler::Version::NEW_HORIZONS_FINAL_LUCK),
				h.hasFeature(Handler::Version::NEW_HORIZONS_MORALE_EXTRA_DAMAGE));
			newHorizonsMagic::validateCanonicalSpellClausesSerialization(overrides["magic"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_IMPLOSION));
			newHorizonsHeroes::validateDefaultCreatureLineSerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_DEFAULT_CREATURE_LINE_SUCCESSORS));
			newHorizonsHeroes::validateCrisisCommandProfileSerialization(overrides["heroes"]["newHorizonsPerks"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_CRISIS_COMMAND));
			newHorizonsHeroes::validateNavigationStartSerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_NAVIGATION_START_REPLACEMENTS));
			newHorizonsProtectedMobility::validateRulesSerialization(overrides["magic"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_PROTECTED_ADVENTURE_BARRIERS));
			newHorizonsMagic::validateWaterWalkDayEndSerialization(overrides["magic"]["newHorizons"], h.hasFeature(Handler::Version::NEW_HORIZONS_WATER_WALK_DAY_END));
			newHorizonsHeroes::validateArtifactManaRegenerationSerialization(overrides["heroes"]["newHorizonsCapabilities"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_ARTIFACT_MANA_REGENERATION));
			newHorizonsHeroes::validateGlyphsOfFearSerialization(overrides["heroes"]["newHorizonsCapabilities"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_GLYPHS_OF_FEAR_AURA));
			newHorizonsHeroes::validateStartingDevelopmentSerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_STARTING_DEVELOPMENT_PROFILES));
			newHorizonsHeroes::validateStartingBookSerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_STARTING_BOOK_REPLACEMENTS));
			newHorizonsHeroes::validateRemainingStartSerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_REMAINING_START_REPLACEMENTS));
			newHorizonsHeroes::validateCoroniusHolyWrathSerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_CORONIUS_HOLY_WRATH));
			newHorizonsHeroes::validateHasteSpecialtySerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_HASTE_SPECIALTIES));
			newHorizonsHeroes::validateReanimateSpecialtySerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_THANT_REANIMATE));
			newHorizonsHeroes::validateFrailtySpecialtySerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_FRAILTY_SPECIALTIES));
			newHorizonsHeroes::validateAenainFrailtySpecialtySerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_AENAIN_FRAILTY_SPECIALTY));
			newHorizonsHeroes::validateDefensiveStartSpecialtySerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_DEFENSIVE_START_SPECIALTIES));
			newHorizonsHeroes::validateOffensiveStartSpecialtySerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_OFFENSIVE_START_SPECIALTIES));
			newHorizonsHeroes::validateRemainingSpellSpecialtySerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_REMAINING_SPELL_SPECIALTIES));
			newHorizonsLighthouse::validateRulesSerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_LIGHTHOUSE_DEPARTURE));
			newHorizonsPlague::validateRuleSerialization(overrides["magic"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_PLAGUEBEARER));
			loadSavedOverrides(overrides);
		}
	}
};
