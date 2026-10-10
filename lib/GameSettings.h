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
#include "json/JsonNode.h"
#include "entities/hero/NewHorizonsHeroRules.h"
#include "battle/NewHorizonsPlague.h"
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

public:
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

	void validateNewHorizonsStartingDevelopmentSerialization(bool supported) const
	{
		newHorizonsHeroes::validateStartingDevelopmentSerialization(
			getAllOverrides()["heroes"]["newHorizons"], supported);
	}

	void validateNewHorizonsRemainingStartSerialization(bool supported) const
	{
		newHorizonsHeroes::validateRemainingStartSerialization(
			getAllOverrides()["heroes"]["newHorizons"], supported);
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

	template<typename Handler>
	void serialize(Handler & h)
	{
		if (h.saving)
		{
			JsonNode overrides = getAllOverrides();
			newHorizonsPlague::validateRuleSerialization(overrides["magic"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_PLAGUEBEARER));
			newHorizonsHeroes::validateStartingDevelopmentSerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_STARTING_DEVELOPMENT_PROFILES));
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
			newHorizonsHeroes::validateHasteSpecialtySerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_HASTE_SPECIALTIES));
			h & overrides;
		}
		else
		{
			JsonNode overrides;
			h & overrides;
			newHorizonsHeroes::validateStartingDevelopmentSerialization(overrides["heroes"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_STARTING_DEVELOPMENT_PROFILES));
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
			newHorizonsPlague::validateRuleSerialization(overrides["magic"]["newHorizons"],
				h.hasFeature(Handler::Version::NEW_HORIZONS_PLAGUEBEARER));
			loadOverrides(overrides);
		}
	}
};
