/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <cmath>
#include <cstdint>
#include <string>

/// Creature-inspector Damage row: the per-creature base range, plus the
/// Elemental Attunement range when the stack is Attuned (e.g. "7 - 10 (8.4 - 12)").
namespace creatureDamagePresentation
{
/// One-decimal per-creature amount after a percentage bonus; whole values drop ".0".
inline std::string boostedAmount(int64_t value, int percent)
{
	const auto tenths = static_cast<int64_t>(std::llround(static_cast<double>(value) * (100 + percent) / 10.0));
	std::string text = std::to_string(tenths / 10);
	if(tenths % 10 != 0)
		text += "." + std::to_string(tenths % 10);
	return text;
}

inline std::string range(const std::string & minimum, const std::string & maximum)
{
	return minimum == maximum ? minimum : minimum + " - " + maximum;
}

struct Readout
{
	std::string value;
	std::string hover;
	std::string help;
};

/// percent is the Attunement bonus for the attack type shown (ranged for shooters, else melee).
inline Readout attuned(int64_t minimum, int64_t maximum, int percent, bool ranged)
{
	const auto base = range(std::to_string(minimum), std::to_string(maximum));
	const auto boosted = range(boostedAmount(minimum, percent), boostedAmount(maximum, percent));
	Readout result;
	result.value = base + " (" + boosted + ")";
	result.hover = "Damage " + base + "; Attuned " + boosted;
	result.help = "Damage per creature: " + base + ".\n"
		"Elemental Attunement: +" + std::to_string(percent) + "% " + (ranged ? "ranged" : "melee")
		+ " attack damage, so each creature deals " + boosted + ".\n\n"
		"These are per-creature amounts before the target's Defense, protection and other modifiers."
		" The attack preview shows the full result against a specific target.";
	return result;
}
}
