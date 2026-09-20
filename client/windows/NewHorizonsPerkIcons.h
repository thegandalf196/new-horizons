/*
 * New Horizons provisional perk art bindings.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <map>
#include <string>

inline const std::string & newHorizonsPerkIcon(const std::string & perkId)
{
	static const std::map<std::string, std::string> icons = {
		{"new-horizons:sorceryMagic.overcharger", "NH_perk_overcharger"},
		{"new-horizons:sorceryMagic.selectiveDispel", "NH_perk_selective_dispel"},
		{"new-horizons:sorceryMagic.temporalist", "NH_perk_temporalist"},
		{"new-horizons:sorceryMagic.matterShaper", "NH_perk_matter_shaper"},
		{"new-horizons:sorceryMagic.teleporter", "NH_perk_teleporter"},
		{"new-horizons:sorceryMagic.countermage", "NH_perk_countermage"},
		{"new-horizons:sorceryMagic.temporalField", "NH_perk_temporal_field"},
		{"new-horizons:sorceryMagic.chronomancer", "NH_perk_chronomancer"},
		{"new-horizons:sylvanLuck.elvenPrecision", "NH_perk_elven_precision"},
		{"new-horizons:metamagic.spellSequencing", "NH_perk_spell_sequencing"},
		{"new-horizons:metamagic.arcaneEconomy", "NH_perk_arcane_economy"},
		{"new-horizons:metamagic.focusedPairing", "NH_perk_focused_pairing"},
		{"new-horizons:metamagic.countersequence", "NH_perk_countersequence"},
		{"new-horizons:metamagic.echoedDuration", "NH_perk_echoed_duration"},
		{"new-horizons:metamagic.splitFocus", "NH_perk_split_focus"},
		{"new-horizons:metamagic.formulaReserve", "NH_perk_formula_reserve"},
		{"new-horizons:metamagic.spellBuffer", "NH_perk_spell_buffer"},
		{"new-horizons:metamagic.grandMetamagic", "NH_perk_grand_metamagic"},
		{"new-horizons:metamagic.perfectSequence", "NH_perk_perfect_sequence"},
		{"new-horizons:necromancy.darkConversion", "NH_perk_dark_conversion"},
		{"new-horizons:necromancy.blackHarvest", "NH_perk_black_harvest"},
		{"new-horizons:bloodrage.warDrums", "NH_perk_war_drums"},
		{"new-horizons:necromancy.boneCollector", "NH_perk_bone_collector"},
	};
	static const std::string fallback = "NH_perk_neutral";
	const auto found = icons.find(perkId);
	return found == icons.end() ? fallback : found->second;
}

