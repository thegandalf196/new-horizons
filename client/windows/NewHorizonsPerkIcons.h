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
		{"new-horizons:offense.shockAssault", "NH_perk_shock_assault"},
		{"new-horizons:offense.executioner", "NH_perk_executioner"},
		{"new-horizons:offense.armorPiercer", "NH_perk_armor_piercer"},
		{"new-horizons:offense.breakthrough", "NH_perk_breakthrough"},
		{"new-horizons:battlecraft.entrench", "NH_perk_entrench"},
		{"new-horizons:discipline.inspirationalLeader", "NH_perk_inspirational_leader"},
		{"new-horizons:sorceryMagic.overcharger", "NH_perk_overcharger"},
		{"new-horizons:sorceryMagic.selectiveDispel", "NH_perk_selective_dispel"},
		{"new-horizons:sorceryMagic.temporalist", "NH_perk_temporalist"},
		{"new-horizons:sorceryMagic.matterShaper", "NH_perk_matter_shaper"},
		{"new-horizons:sorceryMagic.teleporter", "NH_perk_teleporter"},
		{"new-horizons:sorceryMagic.countermage", "NH_perk_countermage"},
		{"new-horizons:sorceryMagic.temporalField", "NH_perk_temporal_field"},
		{"new-horizons:sorceryMagic.chronomancer", "NH_perk_chronomancer"},
		// Stormcaller is active in the authored perk registry.  Reuse the
		// school glyph until a dedicated lightning painting is available rather
		// than silently falling back to the neutral placeholder.
		{"new-horizons:havocMagic.stormcaller", "NH_perk_stormcaller"},
		// Provisional but distinct bindings for the newly playable higher-rank
		// Havoc perks.  Dedicated Havoc paintings can replace these asset keys
		// later without falling back to the neutral placeholder meanwhile.
		{"new-horizons:havocMagic.conductor", "NH_perk_conductor"},
		{"new-horizons:havocMagic.annihilator", "NH_perk_annihilator"},
		{"new-horizons:sylvanLuck.elvenPrecision", "NH_perk_elven_precision"},
		{"new-horizons:sylvanLuck.forestSFavor", "NH_perk_forests_favor"},
		{"new-horizons:sylvanLuck.serendipity", "NH_perk_serendipity"},
		{"new-horizons:sylvanLuck.luckyRecovery", "NH_perk_lucky_recovery"},
		{"new-horizons:sylvanLuck.sharedFortune", "NH_perk_shared_fortune"},
		{"new-horizons:sylvanLuck.natureSProvidence", "NH_perk_natures_providence"},
		{"new-horizons:sylvanLuck.fortunateAim", "NH_perk_fortunate_aim"},
		{"new-horizons:sylvanLuck.wildChance", "NH_perk_wild_chance"},
		{"new-horizons:sylvanLuck.perfectMoment", "NH_perk_perfect_moment"},
		{"new-horizons:sylvanLuck.cascadingFortune", "NH_perk_cascading_fortune"},
		{"new-horizons:metamagic.spellSequencing", "NH_perk_spell_sequencing"},
		{"new-horizons:metamagic.arcaneEconomy", "NH_perk_arcane_economy"},
		{"new-horizons:metamagic.focusedPairing", "NH_perk_focused_pairing"},
		{"new-horizons:metamagic.countersequence", "NH_perk_countersequence"},
		{"new-horizons:metamagic.echoedDuration", "NH_perk_echoed_duration"},
		{"new-horizons:metamagic.splitFocus", "NH_perk_split_focus"},
		{"new-horizons:metamagic.formulaReserve", "NH_perk_formula_reserve"},
		// Spell Echo has no dedicated painting yet; use the neutral placeholder
		// rather than exposing the retired Spell Buffer artwork for a new rule.
		{"new-horizons:metamagic.spellEcho", "NH_perk_neutral"},
		{"new-horizons:metamagic.grandMetamagic", "NH_perk_grand_metamagic"},
		{"new-horizons:metamagic.perfectSequence", "NH_perk_perfect_sequence"},
		{"new-horizons:necromancy.darkConversion", "NH_perk_dark_conversion"},
		{"new-horizons:necromancy.blackHarvest", "NH_perk_black_harvest"},
		{"new-horizons:bloodrage.warDrums", "NH_perk_war_drums"},
		{"new-horizons:necromancy.boneCollector", "NH_perk_bone_collector"},
		{"new-horizons:warcasting.martialChanneling", "NH_perk_martial_channeling"},
		{"new-horizons:warcasting.arcaneChanneling", "NH_perk_arcane_channeling"},
		{"new-horizons:warcasting.tacticalWeaving", "NH_perk_tactical_weaving"},
	};
	static const std::string fallback = "NH_perk_neutral";
	const auto found = icons.find(perkId);
	return found == icons.end() ? fallback : found->second;
}
