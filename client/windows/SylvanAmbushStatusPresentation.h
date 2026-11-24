/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/NewHorizonsSylvanAmbush.h"
#include "../../lib/battle/Unit.h"
#include <functional>
#include <string>
#include <vector>

/// Sylvan Tactics 5 status cards for the creature window. Both states are
/// public battlefield facts of the observed stack, like Forest Ambush.
namespace sylvanAmbushStatusPresentation
{
inline constexpr const char * CONCEALED_ICON = "NH_status_sylvan_concealed.png";
inline constexpr const char * STRIKE_ICON = "NH_status_sylvan_ambush_strike.png";

struct Card
{
	std::string text;
	std::string image;
	std::string help;
};

using Translate = std::function<std::string(const std::string &)>;

inline std::vector<Card> cards(const battle::Unit * unit, const Translate & translate)
{
	std::vector<Card> result;
	const auto card = [&translate, &result](const std::string & key, const char * image)
	{
		const auto prefix = "new-horizons.combat.sylvanAmbush." + key;
		result.push_back({translate(prefix + ".row"), image,
			"{" + translate(prefix + ".title") + "}\n\n" + translate(prefix + ".help")});
	};
	if(newHorizonsSylvanAmbush::concealed(unit))
		card("concealed", CONCEALED_ICON);
	if(newHorizonsSylvanAmbush::strikeReady(unit))
		card("strike", STRIKE_ICON);
	return result;
}
}
