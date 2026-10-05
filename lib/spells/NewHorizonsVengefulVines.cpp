/*
 * NewHorizonsVengefulVines.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "NewHorizonsVengefulVines.h"

#include "NewHorizonsMagic.h"
#include "NewHorizonsSpellAvailability.h"
#include "CSpell.h"

#include <algorithm>
#include <array>
#include <set>

namespace newHorizonsVengefulVines
{
bool enabled(const JsonNode & savedRules, const SpellID spell)
{
	const auto * definition = spell.toSpell();
	return definition && definition->getJsonKey() == SPELL_KEY
		&& newHorizonsMagic::rulesActive(savedRules)
		&& savedRules["rulesetVersion"].Integer()
			== newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		&& newHorizonsMagic::spellAllowedBySavedRoster(savedRules, spell);
}

BattleHexArray footprint(const battle::Target & target)
{
	if(target.size() != 3)
		return {};

	BattleHexArray selected;
	for(const auto & destination : target)
	{
		const auto & hex = destination.hexValue;
		if(destination.unitValue || !hex.isAvailable() || selected.contains(hex))
			return {};

		if(!selected.empty())
		{
			const bool touchesSelection = std::ranges::any_of(selected, [&hex](const BattleHex & previous)
			{
				return BattleHex::mutualPosition(previous, hex) != BattleHex::NONE;
			});
			if(!touchesSelection)
				return {};
		}

		selected.insert(hex);
	}

	return selected;
}

const std::vector<BattleHexArray> & connectedTriples()
{
	static const std::vector<BattleHexArray> triples = []
	{
		std::vector<BattleHexArray> result;
		std::set<std::array<int, 3>> seen;

		for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		{
			const BattleHex first(index);
			if(!first.isAvailable())
				continue;

			for(const auto & second : BattleHexArray::getNeighbouringTiles(first))
			{
				BattleHexArray thirdCandidates = BattleHexArray::getNeighbouringTiles(first);
				thirdCandidates.insert(BattleHexArray::getNeighbouringTiles(second));

				for(const auto & third : thirdCandidates)
				{
					if(third == first || third == second || !third.isAvailable())
						continue;

					auto key = std::array<int, 3>{first.toInt(), second.toInt(), third.toInt()};
					std::ranges::sort(key);
					if(!seen.insert(key).second)
						continue;

					BattleHexArray triple;
					triple.insert(first);
					triple.insert(second);
					triple.insert(third);
					result.push_back(std::move(triple));
				}
			}
		}
		return result;
	}();

	return triples;
}
}
