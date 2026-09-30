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
#include "OrientedSpellPattern.h"
#include "CSpell.h"

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

BattleHexArray footprint(const BattleHex & origin, const BattleHex::EDir direction)
{
	static constexpr std::array<int, 5> windingSteps{0, 1, 0, -1, 0};
	return spells::makeOrientedSpellPath(origin, direction, windingSteps);
}

BattleHexArray footprint(const battle::Target & target)
{
	if(target.size() != 2 || target[0].unitValue || target[1].unitValue
		|| !target[0].hexValue.isAvailable() || !target[1].hexValue.isAvailable())
		return {};

	const auto direction = spells::adjacentSpellDirection(target[0].hexValue, target[1].hexValue);
	if(direction == BattleHex::NONE)
		return {};

	return footprint(target[0].hexValue, direction);
}
}
