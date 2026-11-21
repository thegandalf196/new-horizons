/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <string>
#include <string_view>

/// Icon and timing help for the Sylvan Tactics status rows of the creature
/// inspector. Rows are "<Name>: ..." lines from newHorizonsSylvanFactionPresentation;
/// art reuses the 44x44 Sylvan Tactics perk icons.
namespace sylvanStatusArt
{
struct Art
{
	std::string image;
	std::string help;
};

inline Art forRow(std::string_view row)
{
	struct Entry
	{
		std::string_view name;
		const char * image;
		const char * timing;
		/// Sylvan Tactics 4.1 rows reuse a perk name; their row text selects the help.
		std::string_view revisedRow = {};
	};
	static constexpr Entry entries[] = {
		{"Dappled Cover", "NH_perk_sylvan_tactics_dappled_cover.png",
			"A creature that voluntarily moved at least 3 hexes during its activation takes 20% less ranged attack "
			"damage until its next activation. Teleports and forced movement do not count. It does not reduce melee "
			"attacks or spell damage.", "until this stack's next activation"},
		{"Quick Redeployment", "NH_perk_sylvan_tactics_quick_redeployment.png",
			"After a creature retaliates, it gains +2 Initiative until its next activation. Repeated retaliations "
			"refresh the bonus without stacking it.", "Initiative"},
		{"Dappled Cover", "NH_perk_sylvan_tactics_dappled_cover.png",
			"Waiting creatures take 20% less ranged attack damage. The protection applies while the creature is Waiting "
			"and ends when it leaves that state. It does not reduce melee attacks or spell damage."},
		{"Covering Fire", "NH_perk_sylvan_tactics_covering_fire.png",
			"After a friendly damaging physical ranged strike, adjacent friendly creatures gain 15% protection against "
			"physical ranged damage until their next activation."},
		{"Fleetfoot", "NH_perk_sylvan_tactics_fleetfoot.png",
			"Friendly creatures gain +1 Speed during their first activation of combat. Waiting, Defending and later "
			"activations do not renew it."},
		{"Quick Redeployment", "NH_perk_sylvan_tactics_quick_redeployment.png",
			"Defending grants +2 Speed during the creature's next activation. The bonus is consumed by that activation; "
			"repeated Defending does not stack it."},
		{"Wild Hunt", "NH_perk_sylvan_tactics_wild_hunt.png",
			"The first enemy creature stack destroyed by a friendly physical attack this round grants friendly creatures "
			"+2 Speed for the rest of the round."},
		{"Disruptive Strike", "NH_perk_sylvan_tactics_disruptive_strike.png",
			"Melee hits reduce the target's Initiative by 15% until its next activation. Repeated hits refresh the effect "
			"without stacking."},
	};
	for(const auto & entry : entries)
		if(row.starts_with(entry.name) && row.size() > entry.name.size() && row[entry.name.size()] == ':'
			&& (entry.revisedRow.empty() || row.find(entry.revisedRow) != std::string_view::npos))
			return {entry.image, "{" + std::string(entry.name) + "}\n\n" + std::string(row) + "\n\n" + entry.timing};
	return {};
}
}
