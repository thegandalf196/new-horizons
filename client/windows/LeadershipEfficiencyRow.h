/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <string>

/// Creature-inspector "Leadership Efficiency" row and its right-click help.
/// Wording follows private canon game-design.md #efficiency-presentation.
namespace leadershipEfficiencyRow
{
inline std::string oneDecimal(double value)
{
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "%.1f", value);
	std::string text(buffer);
	if(text.size() > 2 && text.compare(text.size() - 2, 2, ".0") == 0)
		text.resize(text.size() - 2);
	return text;
}

/// Percentage rounded to one decimal (canon: 9.789 of 10 reads 97.9%);
/// a value below full efficiency is capped at 99.9% so it never reads 100%.
inline std::string percentText(double efficiency)
{
	if(efficiency >= 1.0)
		return "100%";
	const double percent = std::min(99.9, std::round(efficiency * 1000.0) / 10.0);
	return oneDecimal(percent) + "%";
}

struct Row
{
	std::string value;
	std::string help;
};

inline Row describe(int64_t count, int64_t capacity, double efficiency)
{
	if(efficiency >= 1.0 || count <= capacity)
	{
		return {"100%",
			"{Your stack is within capacity.}\n\n"
			"All creatures contribute their full damage. Adding creatures increases health and damage. "
			"Once the stack exceeds capacity, each extra creature contributes less damage."};
	}
	// One rounded effective total; the excess contribution derives from it so the numbers agree.
	const double total = std::round(static_cast<double>(count) * efficiency * 10.0) / 10.0;
	const int64_t excess = count - capacity;
	const auto first = capacity == 1 ? std::string("The first creature deals")
		: "The first " + std::to_string(capacity) + " creatures deal";
	const auto above = "The " + std::to_string(excess) + " above capacity " + (excess == 1 ? "adds" : "add");
	const auto percent = percentText(efficiency);
	return {percent,
		"{Your stack exceeds its capacity.}\n\n"
		+ first + " full damage. " + above + " another " + oneDecimal(total - static_cast<double>(capacity))
		+ " creatures' worth of damage.\n\n"
		"{Result:} " + oneDecimal(total) + " creatures' worth of damage. {" + percent + " efficiency} compares that with what all "
		+ std::to_string(count) + " would deal at full strength. Adding creatures still increases total damage and health."};
}
}
