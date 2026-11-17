/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../serializer/ESerializationVersion.h"
#include <array>
#include <type_traits>

class BinarySerializer;
class BinaryDeserializer;
namespace nativeRules
{
/// Native codecs have monotonic epochs. Explicit capability-profile adapters
/// must admit every dependent family before bypassing cold target preflight.
template<typename Handler> bool supportsCapturedRuleTargets(const Handler & h)
{
	if constexpr(std::is_same_v<Handler, BinarySerializer> || std::is_same_v<Handler, BinaryDeserializer>)
		return h.hasFeature(ESerializationVersion::NEW_HORIZONS_SHARED_CAPTURED_RULES);
	constexpr std::array features{
		ESerializationVersion::NEW_HORIZONS_ADDITIONAL_SPECIALTIES,
		ESerializationVersion::NEW_HORIZONS_ADJACENCY_FILTERS,
		ESerializationVersion::NEW_HORIZONS_AENAIN_FRAILTY_SPECIALTY,
		ESerializationVersion::NEW_HORIZONS_AERIAL_SUPREMACY,
		ESerializationVersion::NEW_HORIZONS_ARTIFACT_MANA_REGENERATION,
		ESerializationVersion::NEW_HORIZONS_BOTTOMLESS_MIND,
		ESerializationVersion::NEW_HORIZONS_COMMAND_AUTHORITY,
		ESerializationVersion::NEW_HORIZONS_CORONIUS_HOLY_WRATH,
		ESerializationVersion::NEW_HORIZONS_CRISIS_COMMAND,
		ESerializationVersion::NEW_HORIZONS_DEFENSIVE_START_SPECIALTIES,
		ESerializationVersion::NEW_HORIZONS_DREAD_CAVALRY,
		ESerializationVersion::NEW_HORIZONS_ESTATES,
		ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES,
		ESerializationVersion::NEW_HORIZONS_FOREST_AMBUSH,
		ESerializationVersion::NEW_HORIZONS_FORTUNE_CHILD_AND_ARCANE_PREPARATION,
		ESerializationVersion::NEW_HORIZONS_FOUR_OFFER_PROGRESSION,
		ESerializationVersion::NEW_HORIZONS_FRAILTY_SPECIALTIES,
		ESerializationVersion::NEW_HORIZONS_GENIE_SAGE,
		ESerializationVersion::NEW_HORIZONS_GHOST_UPGRADE,
		ESerializationVersion::NEW_HORIZONS_GLYPHS_OF_FEAR_AURA,
		ESerializationVersion::NEW_HORIZONS_HASTE_SPECIALTIES,
		ESerializationVersion::NEW_HORIZONS_INDIVIDUAL_SPECIALTY_RIDERS,
		ESerializationVersion::NEW_HORIZONS_LEADERSHIP_EFFICIENCY,
		ESerializationVersion::NEW_HORIZONS_LIGHTHOUSE_DEPARTURE,
		ESerializationVersion::NEW_HORIZONS_LIVING_INFERNO,
		ESerializationVersion::NEW_HORIZONS_MANA_LEECHES,
		ESerializationVersion::NEW_HORIZONS_MASTER_SPELLWARD,
		ESerializationVersion::NEW_HORIZONS_NATIVE_RULES_REDESIGN,
		ESerializationVersion::NEW_HORIZONS_NAVIGATION_START_REPLACEMENTS,
		ESerializationVersion::NEW_HORIZONS_NECROMANCY_POLICY_V2,
		ESerializationVersion::NEW_HORIZONS_OFFENSIVE_START_SPECIALTIES,
		ESerializationVersion::NEW_HORIZONS_OPTIONAL_QUICK_STUDY,
		ESerializationVersion::NEW_HORIZONS_ORDINARY_COMBAT_SPECIALTIES,
		ESerializationVersion::NEW_HORIZONS_ORDINARY_CREATURE_POLICY_EXTENSIONS,
		ESerializationVersion::NEW_HORIZONS_ORDINARY_CREATURE_SPECIALTIES,
		ESerializationVersion::NEW_HORIZONS_ORDINARY_MACHINE_SPECIALTIES,
		ESerializationVersion::NEW_HORIZONS_ORDINARY_MAINTENANCE_SPECIALTIES,
		ESerializationVersion::NEW_HORIZONS_ORDINARY_RANGED_SPECIALTIES,
		ESerializationVersion::NEW_HORIZONS_PARALYZING_GAZE,
		ESerializationVersion::NEW_HORIZONS_PASSIVE_FACTION_SKILLS,
		ESerializationVersion::NEW_HORIZONS_PETRIFYING_BLOOD,
		ESerializationVersion::NEW_HORIZONS_R2_PERK_OFFERS,
		ESerializationVersion::NEW_HORIZONS_RECOVERY_SPECIALTIES,
		ESerializationVersion::NEW_HORIZONS_REMAINING_SPECIALTY_RIDERS,
		ESerializationVersion::NEW_HORIZONS_REMAINING_SPELL_SPECIALTIES,
		ESerializationVersion::NEW_HORIZONS_REMAINING_START_REPLACEMENTS,
		ESerializationVersion::NEW_HORIZONS_REVISED_ORDERS,
		ESerializationVersion::NEW_HORIZONS_SCHOOL_SPELLCRAFT_SPECIALTIES,
		ESerializationVersion::NEW_HORIZONS_SERPENTS_EMBRACE,
		ESerializationVersion::NEW_HORIZONS_SKELETON_NECROMANCY,
		ESerializationVersion::NEW_HORIZONS_STARTING_BOOK_REPLACEMENTS,
		ESerializationVersion::NEW_HORIZONS_STARTING_DEVELOPMENT_PROFILES,
		ESerializationVersion::NEW_HORIZONS_STRICT_PERK_PROGRESSION,
		ESerializationVersion::NEW_HORIZONS_SYLVAN_TACTICS,
		ESerializationVersion::NEW_HORIZONS_THANT_REANIMATE,
		ESerializationVersion::NEW_HORIZONS_WEIGHTED_PERKS_AND_DRUID_SPECIALTIES,
	};
	for(const auto feature : features)
		if(!h.hasFeature(feature)) return false;
	return true;
}
}
