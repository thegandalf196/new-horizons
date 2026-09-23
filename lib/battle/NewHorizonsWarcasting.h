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

/// Readiness created by an accepted action, including its direction-specific
/// channeling perk. Martial Channeling applies after a Spell; Arcane Channeling
/// applies after an Order.
inline int empowerment(const CGHeroInstance * hero, AlternatingHeroActionState::Action acceptedAction)
{
	if(acceptedAction != AlternatingHeroActionState::Action::SPELL
		&& acceptedAction != AlternatingHeroActionState::Action::ORDER)
		return 0;

	int result = rank(hero) * 10;
	if(hero && acceptedAction == AlternatingHeroActionState::Action::SPELL
		&& hero->hasActivePerk("new-horizons:warcasting", "new-horizons:warcasting.martialChanneling"))
		result += 10;
	if(hero && acceptedAction == AlternatingHeroActionState::Action::ORDER
		&& hero->hasActivePerk("new-horizons:warcasting", "new-horizons:warcasting.arcaneChanneling"))
		result += 10;
	return result;
}

inline int readinessLifetimeRounds(const CGHeroInstance * hero)
{
	// Tactical Weaving extends the readiness armed by the accepted action through
	// the following two rounds (inclusive).
	return hero && hero->hasActivePerk("new-horizons:warcasting", "new-horizons:warcasting.tacticalWeaving")
		? 2 : 1;
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
