/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsPerkState.h"

namespace newHorizonsHeroes
{
void PerkState::validateSerializationTarget(const std::function<bool(ESerializationVersion)> & supported) const
{
	const auto & rules = getRules();
	validateFourOfferProgressionSerialization(rules, supported(ESerializationVersion::NEW_HORIZONS_FOUR_OFFER_PROGRESSION));
	validateUnifiedSkillProgressionSerialization(rules, supported(ESerializationVersion::NEW_HORIZONS_LEADERSHIP_EFFICIENCY));
	newHorizonsNecromancy::validateLeadershipPolicySerialization(rules, supported(ESerializationVersion::NEW_HORIZONS_LEADERSHIP_EFFICIENCY));
	validateCommandPerkSerialization(rules, supported(ESerializationVersion::NEW_HORIZONS_COMMAND_AUTHORITY),
		supported(ESerializationVersion::NEW_HORIZONS_LEADERSHIP_EFFICIENCY),
		supported(ESerializationVersion::NEW_HORIZONS_FOUR_OFFER_PROGRESSION), supported(ESerializationVersion::NEW_HORIZONS_ESTATES));
	if(!supported(ESerializationVersion::NEW_HORIZONS_NATIVE_RULES_REDESIGN))
		nativeRules::validatePilotPerks(rules, false);
	newHorizonsCommand::validateRulesSerialization(rules, supported(ESerializationVersion::NEW_HORIZONS_NATIVE_RULES_REDESIGN));
	newHorizonsElementalAttunement::validatePolicySerialization(rules, supported(ESerializationVersion::NEW_HORIZONS_NATIVE_RULES_REDESIGN));
	validateSylvanTacticsSerialization(rules, supported(ESerializationVersion::NEW_HORIZONS_SYLVAN_TACTICS));
	std::optional<ValidatedPerkRules> uncaptured;
	if(!capturedRules) uncaptured.emplace(rules);
	const auto & admitted = capturedRules ? capturedRules->admitted() : *uncaptured;
	validateSelections();
	validateCrisisCommandProfileSerialization(rules, supported(ESerializationVersion::NEW_HORIZONS_CRISIS_COMMAND));
	admitted.validateWeightedSerialization(supported(ESerializationVersion::NEW_HORIZONS_WEIGHTED_PERKS_AND_DRUID_SPECIALTIES),
		supported(ESerializationVersion::NEW_HORIZONS_R2_PERK_OFFERS), supported(ESerializationVersion::NEW_HORIZONS_STRICT_PERK_PROGRESSION),
		supported(ESerializationVersion::NEW_HORIZONS_OPTIONAL_QUICK_STUDY), supported(ESerializationVersion::NEW_HORIZONS_PASSIVE_FACTION_SKILLS));
	newHorizonsNecromancy::validatePolicySerialization(rules, supported(ESerializationVersion::NEW_HORIZONS_SKELETON_NECROMANCY),
		supported(ESerializationVersion::NEW_HORIZONS_NECROMANCY_POLICY_V2));
}
}
