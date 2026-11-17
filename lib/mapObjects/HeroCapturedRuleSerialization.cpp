/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CGHeroInstance.h"

void CGHeroInstance::validateCapturedRuleTarget(
	const std::function<bool(ESerializationVersion)> & supported,
	const newHorizonsHeroes::SpecialtySerializationContext & specialties) const
{
	if(!supported(ESerializationVersion::NEW_HORIZONS_NATIVE_RULES_REDESIGN))
		nativeRules::validatePilotPerks(perkState.getRules(), false);
	nativeRules::validatePilotCreatures(primaryGrowthRules, supported(ESerializationVersion::NEW_HORIZONS_NATIVE_RULES_REDESIGN));
	newHorizonsHeroes::validateSylvanTacticsSerialization(perkState.getRules(),
		supported(ESerializationVersion::NEW_HORIZONS_SYLVAN_TACTICS));
	newHorizonsNecromancy::validatePolicySerialization(perkState.getRules(),
		supported(ESerializationVersion::NEW_HORIZONS_SKELETON_NECROMANCY),
		supported(ESerializationVersion::NEW_HORIZONS_NECROMANCY_POLICY_V2));
	newHorizonsHeroes::validateWeightedPerkSerialization(perkState.getRules(),
		supported(ESerializationVersion::NEW_HORIZONS_WEIGHTED_PERKS_AND_DRUID_SPECIALTIES),
		supported(ESerializationVersion::NEW_HORIZONS_R2_PERK_OFFERS),
		supported(ESerializationVersion::NEW_HORIZONS_STRICT_PERK_PROGRESSION),
		supported(ESerializationVersion::NEW_HORIZONS_OPTIONAL_QUICK_STUDY),
		supported(ESerializationVersion::NEW_HORIZONS_PASSIVE_FACTION_SKILLS));
	newHorizonsHeroes::validateFrailtySpecialtySerialization(primaryGrowthRules,
		supported(ESerializationVersion::NEW_HORIZONS_FRAILTY_SPECIALTIES));
	newHorizonsHeroes::validateCrisisCommandProfileSerialization(perkState.getRules(),
		supported(ESerializationVersion::NEW_HORIZONS_CRISIS_COMMAND));
	newHorizonsHeroes::validateAenainFrailtySpecialtySerialization(primaryGrowthRules,
		supported(ESerializationVersion::NEW_HORIZONS_AENAIN_FRAILTY_SPECIALTY));
	newHorizonsHeroes::validateDefensiveStartSpecialtySerialization(primaryGrowthRules,
		supported(ESerializationVersion::NEW_HORIZONS_DEFENSIVE_START_SPECIALTIES));
	newHorizonsHeroes::validateOffensiveStartSpecialtySerialization(primaryGrowthRules,
		supported(ESerializationVersion::NEW_HORIZONS_OFFENSIVE_START_SPECIALTIES));
	newHorizonsHeroes::validateNavigationStartSerialization(primaryGrowthRules,
		supported(ESerializationVersion::NEW_HORIZONS_NAVIGATION_START_REPLACEMENTS));
	newHorizonsHeroes::validateRemainingSpellSpecialtySerialization(primaryGrowthRules,
		supported(ESerializationVersion::NEW_HORIZONS_REMAINING_SPELL_SPECIALTIES));
	newHorizonsHeroes::validateArtifactManaRegenerationSerialization(capabilityRules,
		supported(ESerializationVersion::NEW_HORIZONS_ARTIFACT_MANA_REGENERATION));
	newHorizonsHeroes::validateGlyphsOfFearSerialization(capabilityRules,
		supported(ESerializationVersion::NEW_HORIZONS_GLYPHS_OF_FEAR_AURA));
	newHorizonsLighthouse::validateRulesSerialization(primaryGrowthRules,
		supported(ESerializationVersion::NEW_HORIZONS_LIGHTHOUSE_DEPARTURE));
	newHorizonsHeroes::validateDruidSpecialtySerialization(primaryGrowthRules,
		supported(ESerializationVersion::NEW_HORIZONS_WEIGHTED_PERKS_AND_DRUID_SPECIALTIES));
	newHorizonsHeroes::validateCapturedSpecialtiesSerialization(primaryGrowthRules,
		specialties);
	newHorizonsHeroes::validateBottomlessMindSerialization(primaryGrowthRules,
		supported(ESerializationVersion::NEW_HORIZONS_BOTTOMLESS_MIND));
	newHorizonsHeroes::validateMasterSpellwardSerialization(primaryGrowthRules,
		supported(ESerializationVersion::NEW_HORIZONS_MASTER_SPELLWARD));
	newHorizonsHeroes::validateStartingDevelopmentSerialization(primaryGrowthRules,
		supported(ESerializationVersion::NEW_HORIZONS_STARTING_DEVELOPMENT_PROFILES));
	newHorizonsHeroes::validateRemainingStartSerialization(primaryGrowthRules,
		supported(ESerializationVersion::NEW_HORIZONS_REMAINING_START_REPLACEMENTS));
	newHorizonsHeroes::validateCoroniusHolyWrathSerialization(primaryGrowthRules,
		supported(ESerializationVersion::NEW_HORIZONS_CORONIUS_HOLY_WRATH));
	newHorizonsHeroes::validateStartingBookSerialization(primaryGrowthRules,
		supported(ESerializationVersion::NEW_HORIZONS_STARTING_BOOK_REPLACEMENTS));
	newHorizonsHeroes::validateReanimateSpecialtySerialization(primaryGrowthRules,
		supported(ESerializationVersion::NEW_HORIZONS_THANT_REANIMATE));
	newHorizonsHeroes::validateHasteSpecialtySerialization(primaryGrowthRules,
		supported(ESerializationVersion::NEW_HORIZONS_HASTE_SPECIALTIES));
}
