/*
 * SpellPointTestUtils.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../lib/mapObjects/CGHeroInstance.h"
#include "../lib/spells/NewHorizonsMagic.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

/// Keep legacy scalar-style test setup readable while preserving the active
/// runtime invariant that Normal cannot exceed capacity. Excess fixture points
/// are represented as Buffer only for saved rules that opt into the two pools.
inline void setTestSpellPointTotal(CGHeroInstance * hero, int64_t total)
{
	if(!hero)
		throw std::invalid_argument("Spell Point test setup requires a hero");
	if(total < 0 || total > std::numeric_limits<int32_t>::max())
		throw std::invalid_argument("Spell Point test total is outside the legacy scalar range");

	if(newHorizonsMagic::spellPointRulesActive(hero->getMagicRules()))
	{
		const int32_t normal = static_cast<int32_t>(std::min<int64_t>(total, hero->manaLimit()));
		const int32_t buffer = static_cast<int32_t>(total - normal);
		hero->initializeSpellPoints(normal, buffer);
	}
	else
		hero->setNormalSpellPoints(static_cast<int32_t>(total));
}
