/*
 * SpellCostBreakdown.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../Global.h"

#include <cstdint>
#include <vector>

/// One ordered, read-only step in an ordinary spell-cost calculation.
struct DLL_LINKAGE SpellCostStage
{
	enum class Kind
	{
		LISTED_MULTIPLIER,
		WISDOM,
		ADVENTURE_ARTIFACT,
		KNIGHTLY_SEQUENCE,
		PREPARED_CASTER,
		ARCHMAGE,
		ALLIED_ARMY,
		ENEMY_ARMY,
		MINIMUM_COST,
		METAMAGIC_ARCANE_ECONOMY
	};

	Kind kind = Kind::LISTED_MULTIPLIER;
	int32_t before = 0;
	int32_t after = 0;
};

/// Read-only listed/final Mana cost and its applicable calculation stages.
struct DLL_LINKAGE SpellCostBreakdown
{
	int32_t listedCost = 0;
	int32_t finalCost = 0;
	std::vector<SpellCostStage> stages;
};
