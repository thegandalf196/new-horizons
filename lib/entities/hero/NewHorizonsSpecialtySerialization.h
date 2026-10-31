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
	bool serpentsEmbrace = false;
	bool ghostSpecialty = false;
	bool paralyzingGaze = false;
	bool forestAmbush = false;
	bool petrifyingBlood = false;
	bool livingInferno = false;
	bool aerialSupremacy = false;
	bool dreadCavalry = false;
	bool genieSage = false;
	bool ordinaryCreatureSpecialties = false;
	bool adjacencyFilters = false;
	bool ordinaryCreaturePolicyExtensions = false;
	bool ordinaryRangedSpecialties = false;
	bool ordinaryMaintenanceSpecialties = false;
	bool ordinaryMachineSpecialties = false;
	bool ordinaryCombatSpecialties = false;

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
		result.serpentsEmbrace = h.hasFeature(Handler::Version::NEW_HORIZONS_SERPENTS_EMBRACE);
		result.ghostSpecialty = h.hasFeature(Handler::Version::NEW_HORIZONS_GHOST_UPGRADE);
		result.paralyzingGaze = h.hasFeature(Handler::Version::NEW_HORIZONS_PARALYZING_GAZE);
		result.forestAmbush = h.hasFeature(Handler::Version::NEW_HORIZONS_FOREST_AMBUSH);
		result.petrifyingBlood = h.hasFeature(Handler::Version::NEW_HORIZONS_PETRIFYING_BLOOD);
		result.livingInferno = h.hasFeature(Handler::Version::NEW_HORIZONS_LIVING_INFERNO);
		result.aerialSupremacy = h.hasFeature(Handler::Version::NEW_HORIZONS_AERIAL_SUPREMACY);
		result.dreadCavalry = h.hasFeature(Handler::Version::NEW_HORIZONS_DREAD_CAVALRY);
		result.genieSage = h.hasFeature(Handler::Version::NEW_HORIZONS_GENIE_SAGE);
		result.ordinaryCreatureSpecialties = h.hasFeature(Handler::Version::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES);
		result.adjacencyFilters = h.hasFeature(Handler::Version::NEW_HORIZONS_ADJACENCY_FILTERS);
		result.ordinaryCreaturePolicyExtensions = h.hasFeature(Handler::Version::NEW_HORIZONS_ORDINARY_CREATURE_POLICY_EXTENSIONS);
		result.ordinaryRangedSpecialties = h.hasFeature(Handler::Version::NEW_HORIZONS_ORDINARY_RANGED_SPECIALTIES);
		result.ordinaryMaintenanceSpecialties = h.hasFeature(Handler::Version::NEW_HORIZONS_ORDINARY_MAINTENANCE_SPECIALTIES);
		result.ordinaryMachineSpecialties = h.hasFeature(Handler::Version::NEW_HORIZONS_ORDINARY_MACHINE_SPECIALTIES);
		result.ordinaryCombatSpecialties = h.hasFeature(Handler::Version::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES);
		return result;
	}
};

/// Fresh authored/admission checks support every currently implemented family.
DLL_LINKAGE void validateCurrentSpecialtyRules(const JsonNode & development);

/// Validate all captured specialty families in the historical order, without
/// resolving installed defaults, changing the snapshot or consuming wire bytes.
DLL_LINKAGE void validateCapturedSpecialtiesSerialization(const JsonNode & development,
	const SpecialtySerializationContext & context);
/// Copy present specialty families in their existing deterministic resolution order.
DLL_LINKAGE void copyCapturedIndividualSpecialties(JsonNode & target, const JsonNode & source);

}
