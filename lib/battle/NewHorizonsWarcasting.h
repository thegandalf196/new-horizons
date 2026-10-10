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
#include <limits>

namespace newHorizonsWarcasting
{
/// Warcasting is an opt-in for battles created from saved v2-or-later magic profiles.
/// Missing, legacy, or malformed values remain inactive.
inline bool enabled(const JsonNode & rules)
{
	return rules.isStruct()
		&& rules["rulesetVersion"].getType() == JsonNode::JsonType::DATA_INTEGER
		&& rules["rulesetVersion"].Integer() >= newHorizonsMagic::DIRECT_DAMAGE_RULESET_VERSION
		&& rules["rulesetVersion"].Integer() <= newHorizonsMagic::CURRENT_RULESET_VERSION
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

inline int reactiveEmpowerment(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk("new-horizons:warcasting", "new-horizons:warcasting.reactiveWeave")
		? empowerment(hero, AlternatingHeroActionState::Action::SPELL) / 2 : 0;
}

inline int readinessLifetimeRounds(const CGHeroInstance * hero)
{
	// Tactical Weaving extends the readiness armed by the accepted action through
	// the following two rounds (inclusive).
	return hero && hero->hasActivePerk("new-horizons:warcasting", "new-horizons:warcasting.tacticalWeaving")
		? 2 : 1;
}

inline bool hasMasterSynthesis(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk("new-horizons:warcasting", "new-horizons:warcasting.masterSynthesis");
}

inline bool hasPerfectRhythm(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk("new-horizons:warcasting", "new-horizons:warcasting.perfectRhythm");
}

/// Applies Master Synthesis only to an existing, matching Warcasting readiness.
/// The first accepted consumption is tracked by the shared battle state, so both
/// spell and Order projections use the same combat-long replacement. Perfect
/// Rhythm doubles only a normal matching bonus when this candidate completes its
/// accepted-action sequence; the two Expert perks cannot be selected together.
inline int effectiveBonus(const CGHeroInstance * hero, const AlternatingHeroActionState & state,
	AlternatingHeroActionState::Action action, int32_t round)
{
	const int raw = state.bonusFor(action, round);
	if(raw <= 0)
		return 0;
	if(hasMasterSynthesis(hero) && !state.hasConsumedBonus)
		return 50;
	if(hasPerfectRhythm(hero) && state.wouldCompleteAlternatingSpellOrderSequence(action))
	{
		// The shared readiness field is int32_t. Saturate only at that representable
		// boundary rather than overflowing while doubling a legal positive value.
		const auto doubled = static_cast<int64_t>(raw) * 2;
		return static_cast<int>(std::min<int64_t>(doubled, std::numeric_limits<int32_t>::max()));
	}
	return raw;
}

inline int orderBonus(const AlternatingHeroActionState & state, int32_t round)
{
	return state.bonusFor(AlternatingHeroActionState::Action::ORDER, round);
}

inline int orderBonus(const CGHeroInstance * hero, const AlternatingHeroActionState & state, int32_t round)
{
	return effectiveBonus(hero, state, AlternatingHeroActionState::Action::ORDER, round);
}

inline int spellBonus(const AlternatingHeroActionState & state, int32_t round)
{
	return state.bonusFor(AlternatingHeroActionState::Action::SPELL, round);
}

inline int spellBonus(const CGHeroInstance * hero, const AlternatingHeroActionState & state, int32_t round)
{
	return effectiveBonus(hero, state, AlternatingHeroActionState::Action::SPELL, round);
}

/// Battle Meditation is earned only by consuming live Order-to-Spell readiness.
/// Call before the accepted spell packet changes that readiness.
inline bool battleMeditationEligible(const JsonNode & rules, const CGHeroInstance * hero,
	const AlternatingHeroActionState & state, int32_t round)
{
	return round >= 0 && enabled(rules) && hero
		&& hero->hasActivePerk("new-horizons:warcasting", "new-horizons:warcasting.battleMeditation")
		&& state.lastManaRecoveryRound != round
		&& spellBonus(state, round) > 0;
}

inline constexpr int BATTLE_MEDITATION_MANA_RECOVERY = 3;

/// Battle Meditation restores the capacity-bound Normal pool, whose storage is
/// int32_t; cap the refund at the representable amount first.
inline int battleMeditationRecoveryAmount(int32_t currentMana)
{
	const auto room = static_cast<int64_t>(std::numeric_limits<int32_t>::max()) - currentMana;
	return static_cast<int>(std::clamp<int64_t>(room, 0, BATTLE_MEDITATION_MANA_RECOVERY));
}
}
