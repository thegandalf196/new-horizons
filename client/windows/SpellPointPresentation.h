/*
 * SpellPointPresentation.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include <cstdint>
#include <string>

namespace spellPointPresentation
{
inline std::string bufferText(int32_t buffer)
{
	return "{+" + std::to_string(buffer) + "}";
}

/// Total already includes Buffer; never add the annotation to it a second time.
inline std::string readout(int64_t total, int32_t maximum, int32_t buffer)
{
	std::string result = std::to_string(total);
	if(maximum >= 0)
		result += " / " + std::to_string(maximum);
	if(buffer > 0)
		result += "  " + bufferText(buffer);
	return result;
}

inline std::string tooltip(int64_t total, int32_t maximum, int32_t buffer)
{
	std::string result = "{Spell Points}\n\n" + readout(total, maximum, buffer)
		+ "\n\n" + std::to_string(total) + " total Spell Points available.";
	if(buffer > 0)
		result += "\n" + std::to_string(total - buffer) + " Normal + {"
			+ std::to_string(buffer) + " Buffer}.";
	if(maximum >= 0)
		result += "\n" + std::to_string(maximum) + " Maximum Spell Points.";
	if(buffer > 0)
		result += "\n\n{Buffer Spell Points} are included in your total."
			" They may exceed your maximum and are spent first.";
	return result;
}
}
