/* Part of VCMI / New Horizons; GPL-2.0-or-later; see license.txt. */
#pragma once

#include "NewHorizonsSylvanAlliancePerkIcons.h"

/// Match the current mixed roster without reinterpreting historical artwork.
inline std::string newHorizonsHybridCapturedPerkIcon(const std::string & perkId,
	std::string_view capturedName, const JsonNode & capturedRules)
{
	if(perkId.starts_with("new-horizons:sylvanLuck.")
		&& newHorizonsHeroes::usesSylvanTacticsV3(capturedRules))
	{
		struct Binding
		{
			std::string_view id;
			std::string_view name;
			const char * resource;
		};
		static constexpr Binding bindings[] = {
			{"new-horizons:sylvanLuck.elvenPrecision", "Hunting Pack", "NH_perk_sylvan_tactics_wild_hunt"},
			{"new-horizons:sylvanLuck.forestSFavor", "Fleetfoot", "NH_perk_sylvan_tactics_fleetfoot"},
			{"new-horizons:sylvanLuck.serendipity", "Patient Hunter", "NH_perk_sylvan_tactics_patient_hunter"},
			{"new-horizons:sylvanLuck.luckyRecovery", "Dappled Cover", "NH_perk_sylvan_tactics_dappled_cover"},
			{"new-horizons:sylvanLuck.sharedFortune", "Evergreen Blessings", "NH_perk_sylvan_tactics_evergreen_blessings"},
			{"new-horizons:sylvanLuck.natureSProvidence", "Quick Redeployment", "NH_perk_sylvan_tactics_quick_redeployment"},
			{"new-horizons:sylvanLuck.fortunateAim", "Disruptive Strike", "NH_perk_sylvan_tactics_disruptive_strike"},
			{"new-horizons:sylvanLuck.wildChance", "Kindred Flame", "NH_perk_sylvan_tactics_kindred_flame"},
			{"new-horizons:sylvanLuck.perfectMoment", "Perfect Moment", "NH_perk_sylvan_tactics_perfect_moment"},
			{"new-horizons:sylvanLuck.cascadingFortune", "Ancient Accord", "NH_perk_sylvan_tactics_ancient_accord"},
		};
		for(const auto & binding : bindings)
			if(perkId == binding.id && capturedName == binding.name)
				return binding.resource;
		// A mismatched name must not resolve through an older Sylvan mapping.
		return newHorizonsPerkIcon(perkId, capturedName);
	}
	return newHorizonsCapturedPerkIcon(perkId, capturedName, capturedRules);
}
