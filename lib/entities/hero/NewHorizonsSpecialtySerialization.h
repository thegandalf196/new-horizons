/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

class JsonNode;

namespace newHorizonsHeroes
{
/// Captured family support at one serialization boundary. Named fields prevent
/// positional-flag drift; absence remains meaningful at every format epoch.
struct SpecialtySerializationContext
{
	bool fortuneAndArcane = false;
	bool individualRiders = false;
	bool schoolSpellcraft = false;
	bool additional = false;
	bool recovery = false;
	bool remainingRiders = false;
	bool chosenSpecialties = false;
	bool manaLeeches = false;

	template<class Handler>
	static SpecialtySerializationContext fromHandler(const Handler & h)
	{
		SpecialtySerializationContext result;
		result.fortuneAndArcane = h.hasFeature(Handler::Version::NEW_HORIZONS_FORTUNE_CHILD_AND_ARCANE_PREPARATION);
		result.individualRiders = h.hasFeature(Handler::Version::NEW_HORIZONS_INDIVIDUAL_SPECIALTY_RIDERS);
		result.schoolSpellcraft = h.hasFeature(Handler::Version::NEW_HORIZONS_SCHOOL_SPELLCRAFT_SPECIALTIES);
		result.additional = h.hasFeature(Handler::Version::NEW_HORIZONS_ADDITIONAL_SPECIALTIES);
		result.recovery = h.hasFeature(Handler::Version::NEW_HORIZONS_RECOVERY_SPECIALTIES);
		result.remainingRiders = h.hasFeature(Handler::Version::NEW_HORIZONS_REMAINING_SPECIALTY_RIDERS);
		result.chosenSpecialties = h.hasFeature(Handler::Version::NEW_HORIZONS_REVISED_ORDERS);
		result.manaLeeches = h.hasFeature(Handler::Version::NEW_HORIZONS_MANA_LEECHES);
		return result;
	}
};

/// Fresh authored/admission checks support every currently implemented family.
DLL_LINKAGE void validateCurrentSpecialtyRules(const JsonNode & development);

/// Validate all captured specialty families in the historical order, without
/// resolving installed defaults, changing the snapshot or consuming wire bytes.
DLL_LINKAGE void validateCapturedSpecialtiesSerialization(const JsonNode & development,
	const SpecialtySerializationContext & context);
}
