/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/entities/hero/NewHorizonsPerkState.h"
namespace perkWriteReference
{
// Frozen pre-change saving branch only, including the deliberate duplicate admission.
// The production reader is neither copied nor exercised through this reference.
template<typename Handler> void write(Handler & h, newHorizonsHeroes::PerkState & state)
{
	using namespace newHorizonsHeroes;
	const auto & rules = state.getRules();
	if(h.saving)
	{
		validateUnifiedSkillProgressionSerialization(rules, h.hasFeature(Handler::Version::NEW_HORIZONS_LEADERSHIP_EFFICIENCY));
		newHorizonsNecromancy::validateLeadershipPolicySerialization(rules, h.hasFeature(Handler::Version::NEW_HORIZONS_LEADERSHIP_EFFICIENCY));
		validateCommandPerkSerialization(rules, h.hasFeature(Handler::Version::NEW_HORIZONS_COMMAND_AUTHORITY),
			h.hasFeature(Handler::Version::NEW_HORIZONS_LEADERSHIP_EFFICIENCY));
		if(!h.hasFeature(Handler::Version::NEW_HORIZONS_NATIVE_RULES_REDESIGN))
			nativeRules::validatePilotPerks(rules, false);
		newHorizonsCommand::validateRulesSerialization(rules, h.hasFeature(Handler::Version::NEW_HORIZONS_NATIVE_RULES_REDESIGN));
		newHorizonsElementalAttunement::validatePolicySerialization(rules, h.hasFeature(Handler::Version::NEW_HORIZONS_NATIVE_RULES_REDESIGN));
	}
	if(h.saving) newHorizonsHeroes::validateSylvanTacticsSerialization(rules,
		h.hasFeature(Handler::Version::NEW_HORIZONS_SYLVAN_TACTICS));
	const JsonNode & capturedRules = rules;
	if(h.saving)
		state.validate();
	if(h.saving)
		validateCrisisCommandProfileSerialization(rules, h.hasFeature(Handler::Version::NEW_HORIZONS_CRISIS_COMMAND));
	if(h.saving)
		validateWeightedPerkSerialization(capturedRules,
			h.hasFeature(Handler::Version::NEW_HORIZONS_WEIGHTED_PERKS_AND_DRUID_SPECIALTIES),
			h.hasFeature(Handler::Version::NEW_HORIZONS_R2_PERK_OFFERS),
			h.hasFeature(Handler::Version::NEW_HORIZONS_STRICT_PERK_PROGRESSION),
			h.hasFeature(Handler::Version::NEW_HORIZONS_OPTIONAL_QUICK_STUDY),
			h.hasFeature(Handler::Version::NEW_HORIZONS_PASSIVE_FACTION_SKILLS));
	if(h.saving)
		newHorizonsNecromancy::validatePolicySerialization(capturedRules,
			h.hasFeature(Handler::Version::NEW_HORIZONS_SKELETON_NECROMANCY),
			h.hasFeature(Handler::Version::NEW_HORIZONS_NECROMANCY_POLICY_V2));
	h & rules;
	const auto & incomingRules = rules;
	validateUnifiedSkillProgressionSerialization(incomingRules, h.hasFeature(Handler::Version::NEW_HORIZONS_LEADERSHIP_EFFICIENCY));
	newHorizonsNecromancy::validateLeadershipPolicySerialization(incomingRules, h.hasFeature(Handler::Version::NEW_HORIZONS_LEADERSHIP_EFFICIENCY));
	validateCommandPerkSerialization(incomingRules, h.hasFeature(Handler::Version::NEW_HORIZONS_COMMAND_AUTHORITY),
		h.hasFeature(Handler::Version::NEW_HORIZONS_LEADERSHIP_EFFICIENCY));
	if(!h.hasFeature(Handler::Version::NEW_HORIZONS_NATIVE_RULES_REDESIGN))
		nativeRules::validatePilotPerks(incomingRules, false);
	newHorizonsCommand::validateRulesSerialization(incomingRules, h.hasFeature(Handler::Version::NEW_HORIZONS_NATIVE_RULES_REDESIGN));
	newHorizonsElementalAttunement::validatePolicySerialization(incomingRules, h.hasFeature(Handler::Version::NEW_HORIZONS_NATIVE_RULES_REDESIGN));
	validateSylvanTacticsSerialization(incomingRules, h.hasFeature(Handler::Version::NEW_HORIZONS_SYLVAN_TACTICS));
	h & state.getSelected();
 }
}
