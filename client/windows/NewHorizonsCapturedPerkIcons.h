/* Part of VCMI / New Horizons; GPL-2.0-or-later; see license.txt. */
#pragma once

#include "NewHorizonsPerkIcons.h"
#include "../../lib/entities/hero/NewHorizonsPerkRules.h"

/// Reused Sylvan identities select Tactics artwork only from the hero's saved
/// policy and the supplied definition/offer name. Old captures retain Luck art.
inline std::string newHorizonsPerkIcon(const std::string & perkId,
	std::string_view capturedName, const JsonNode & capturedRules)
{
	if(perkId.starts_with("new-horizons:sylvanLuck.")
		&& newHorizonsHeroes::usesSylvanTactics(capturedRules))
	{
		struct Binding
		{
			std::string_view id;
			std::string_view name;
			const char * resource;
		};
		static constexpr Binding bindings[] = {
		{"new-horizons:sylvanLuck.elvenPrecision", "Fleetfoot", "NH_perk_sylvan_tactics_fleetfoot"},
		{"new-horizons:sylvanLuck.forestSFavor", "Patient Hunter", "NH_perk_sylvan_tactics_patient_hunter"},
		{"new-horizons:sylvanLuck.serendipity", "Dappled Cover", "NH_perk_sylvan_tactics_dappled_cover"},
		{"new-horizons:sylvanLuck.luckyRecovery", "Measured Advance", "NH_perk_sylvan_tactics_measured_advance"},
		{"new-horizons:sylvanLuck.sharedFortune", "Covering Fire", "NH_perk_sylvan_tactics_covering_fire"},
		{"new-horizons:sylvanLuck.natureSProvidence", "Crossfire", "NH_perk_sylvan_tactics_crossfire"},
		{"new-horizons:sylvanLuck.fortunateAim", "Disruptive Strike", "NH_perk_sylvan_tactics_disruptive_strike"},
		{"new-horizons:sylvanLuck.wildChance", "Quick Redeployment", "NH_perk_sylvan_tactics_quick_redeployment"},
		{"new-horizons:sylvanLuck.perfectMoment", "Perfect Moment", "NH_perk_sylvan_tactics_perfect_moment"},
		{"new-horizons:sylvanLuck.cascadingFortune", "Wild Hunt", "NH_perk_sylvan_tactics_wild_hunt"},
		};
		for(const auto & binding : bindings)
			if(perkId == binding.id && capturedName == binding.name)
				return binding.resource;
	}
	return newHorizonsPerkIcon(perkId, capturedName);
}
