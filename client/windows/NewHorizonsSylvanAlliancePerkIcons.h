/* Part of VCMI / New Horizons; GPL-2.0-or-later; see license.txt. */
#pragma once

#include "NewHorizonsCapturedPerkIcons.h"

/// Resolve the revised pool from captured rules without changing historical art.
inline std::string newHorizonsCapturedPerkIcon(const std::string & perkId,
	std::string_view capturedName, const JsonNode & capturedRules)
{
	if(perkId.starts_with("new-horizons:sylvanLuck.")
		&& newHorizonsHeroes::usesSylvanTacticsV2(capturedRules))
	{
		struct Binding
		{
			std::string_view id;
			std::string_view name;
			const char * resource;
		};
		static constexpr Binding bindings[] = {
		{"new-horizons:sylvanLuck.elvenPrecision", "Hunting Pack", "NH_perk_sylvan_tactics_wild_hunt"},
		{"new-horizons:sylvanLuck.forestSFavor", "Dwarven Hardiness", "NH_perk_sylvan_tactics_dwarven_hardiness"},
		{"new-horizons:sylvanLuck.serendipity", "Disarming Shot", "NH_perk_sylvan_tactics_covering_fire"},
		{"new-horizons:sylvanLuck.luckyRecovery", "Briar Grip", "NH_perk_sylvan_tactics_briar_grip"},
		{"new-horizons:sylvanLuck.sharedFortune", "Evergreen Blessings", "NH_perk_sylvan_tactics_evergreen_blessings"},
		{"new-horizons:sylvanLuck.natureSProvidence", "Silver Wings", "NH_perk_sylvan_tactics_silver_wings"},
		{"new-horizons:sylvanLuck.fortunateAim", "Sacred Grove", "NH_perk_sylvan_tactics_dappled_cover"},
		{"new-horizons:sylvanLuck.wildChance", "Kindred Flame", "NH_perk_sylvan_tactics_kindred_flame"},
		{"new-horizons:sylvanLuck.cascadingFortune", "Ancient Accord", "NH_perk_sylvan_tactics_ancient_accord"},
		};
		for(const auto & binding : bindings)
			if(perkId == binding.id && capturedName == binding.name)
				return binding.resource;
	}
	return newHorizonsPerkIcon(perkId, capturedName, capturedRules);
}
