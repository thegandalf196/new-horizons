/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/Unit.h"
#include "../../lib/battle/NewHorizonsElementalAttunement.h"
#include <string>

namespace elementalAttunementPresentation
{
inline constexpr auto ICON = "NH_perk_elemental_attunement";
struct Readout
{
	bool attuned = false;
	int meleePercent = 0;
	int rangedPercent = 0;
	int spellReductionBasisPoints = 0;
	int additionalRetaliations = 0;
	bool operator==(const Readout &) const = default;
};

inline Readout forUnit(const battle::Unit & unit)
{
	const auto facts = unit.elementalAttunementPassives();
	if(!unit.alive() || !facts.attuned)
		return {};
	return {true, facts.meleeDamagePercent, facts.rangedDamagePercent,
		facts.spellReductionBasisPoints, facts.additionalRetaliations};
}

inline std::string tooltip(const Readout & status)
{
	if(!status.attuned)
		return {};
	std::string result = "Attuned\n\nThis creature remains Attuned until combat ends."
		" Repeated elemental casts do not stack this benefit."
		"\nMelee attacks and retaliations: +" + std::to_string(status.meleePercent) + "% damage."
		"\nRanged attacks: +" + std::to_string(status.rangedPercent) + "% damage.";
	if(status.spellReductionBasisPoints > 0)
		result += "\nWater Veil: 20% less spell damage.";
	if(status.additionalRetaliations > 0)
		result += "\nPrimal Vigilance: one additional retaliation per round. Recasting does not replenish spent retaliations.";
	return result;
}
}
