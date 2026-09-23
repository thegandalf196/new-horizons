/*
 * NewHorizonsWarcasting.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "AlternatingHeroActionState.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../spells/NewHorizonsMagic.h"

#include <algorithm>

namespace newHorizonsWarcasting
{
/// Warcasting is an opt-in for battles created from the saved v2 magic profile.
/// Missing, legacy, or malformed values remain inactive.
inline bool enabled(const JsonNode & rules)
{
	return rules.isStruct()
		&& rules["rulesetVersion"].getType() == JsonNode::JsonType::DATA_INTEGER
		&& rules["rulesetVersion"].Integer() == newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION
		&& rules["warcasting"].isBool()
		&& rules["warcasting"].Bool();
}

inline int rank(const CGHeroInstance * hero)
{
	return hero ? std::clamp(hero->getPerkSkillRank("new-horizons:warcasting"), 0, 3) : 0;
}

/// Base Spell-to-Order / Order-to-Spell empowerment. Perk modifiers are not
/// included in this first runtime tranche.
inline int empowerment(const CGHeroInstance * hero)
{
	return rank(hero) * 10;
}

inline int orderBonus(const AlternatingHeroActionState & state, int32_t round)
{
	return state.bonusFor(AlternatingHeroActionState::Action::ORDER, round);
}

inline int spellBonus(const AlternatingHeroActionState & state, int32_t round)
{
	return state.bonusFor(AlternatingHeroActionState::Action::SPELL, round);
}
}
