/*
 * NewHorizonsFireWall.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "NewHorizonsMagic.h"

namespace newHorizonsFireWall
{
/// This policy changes grounded flyer susceptibility, not airborne transit.
/// Absence retains the immunity of profiles captured before this producer.
inline bool groundedFlyersBurn(const JsonNode & rules)
{
	const auto & flag = rules["spells"]["core:fireWall"]["burnGroundedFlyers"];
	return newHorizonsMagic::rulesActive(rules)
		&& rules["rulesetVersion"].Integer() == newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		&& flag.isBool() && flag.Bool();
}
}
