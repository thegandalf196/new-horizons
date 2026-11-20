/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

/// Merges repeated ability cards that differ only by target (one card per
/// spell vulnerability, hated creature or immune spell) into a single card,
/// grouped by percentage where one applies. Pure text; no widgets or state.
namespace creatureAbilityGrouping
{
inline std::string joinNames(const std::vector<std::string> & names)
{
	std::string result;
	for(size_t i = 0; i < names.size(); ++i)
	{
		if(i > 0)
			result += i + 1 == names.size() ? " and " : ", ";
		result += names[i];
	}
	return result;
}

struct Card
{
	std::string description; ///< compact card text, "{Title}\n..."
	std::string tooltip;     ///< complete right-click text
};

/// entries: (percent, name), in display order. Lines are ordered by descending percent;
/// label turns a percent into its line prefix (e.g. "+100%" or "25% chance").
inline std::vector<std::pair<int, std::string>> percentLines(const std::vector<std::pair<int, std::string>> & entries,
	const std::function<std::string(int)> & label = [](int percent) { return "+" + std::to_string(percent) + "%"; })
{
	std::map<int, std::vector<std::string>, std::greater<int>> byPercent;
	for(const auto & [percent, name] : entries)
		byPercent[percent].push_back(name);
	std::vector<std::pair<int, std::string>> lines;
	for(const auto & [percent, names] : byPercent)
		lines.emplace_back(percent, label(percent) + ": " + joinNames(names));
	return lines;
}

inline Card percentCard(const std::string & title, const std::string & explanation,
	const std::vector<std::pair<int, std::string>> & entries,
	const std::function<std::string(int)> & label = [](int percent) { return "+" + std::to_string(percent) + "%"; })
{
	Card card{"{" + title + "}", "{" + title + "}\n\n" + explanation};
	for(const auto & line : percentLines(entries, label))
	{
		card.description += "\n" + line.second;
		card.tooltip += "\n" + line.second;
	}
	return card;
}

inline Card spellVulnerability(const std::vector<std::pair<int, std::string>> & entries)
{
	return percentCard("Spell Vulnerability",
		"This unit takes increased damage from these spells:", entries);
}

inline Card hatred(const std::vector<std::pair<int, std::string>> & entries)
{
	return percentCard("Hatred", "This unit deals extra damage to these creatures:", entries);
}

/// entries: (chance percent, spell); different chances stay on separate lines.
inline Card onHitEffects(const std::vector<std::pair<int, std::string>> & entries)
{
	return percentCard("On-hit Effects", "After attacking, this unit may cast:", entries,
		[](int percent) { return percent >= 100 ? std::string("Always") : std::to_string(percent) + "% chance"; });
}

inline Card spellImmunity(const std::vector<std::string> & spells)
{
	const auto list = joinNames(spells);
	return {"{Spell Immunity}\nUnaffected by " + list + ".",
		"{Spell Immunity}\n\nThis unit cannot be affected by " + list + "."};
}
}
