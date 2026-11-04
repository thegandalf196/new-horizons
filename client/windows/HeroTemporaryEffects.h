/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../../lib/bonuses/BonusEnum.h"

#include <string>
#include <vector>

namespace heroTemporaryEffects
{
// A duration is read from the live bonus, never inferred from visit history.
inline bool visibleDuration(BonusDuration::Type duration, int remaining)
{
	if(duration == 0 || duration == BonusDuration::PERMANENT)
		return false;
	if((duration & (BonusDuration::N_DAYS | BonusDuration::N_TURNS)) && remaining <= 0)
		return false;
	return true;
}

inline std::string expiry(BonusDuration::Type duration, int remaining)
{
	std::vector<std::string> conditions;
	const auto add = [&](BonusDuration::Type flag, const std::string & text)
	{
		if(duration & flag)
			conditions.push_back(text);
	};
	add(BonusDuration::ONE_BATTLE, "until the end of the next battle");
	add(BonusDuration::ONE_DAY, "until the end of today");
	add(BonusDuration::ONE_WEEK, "until the end of this week");
	add(BonusDuration::N_DAYS, std::to_string(remaining) + (remaining == 1 ? " day remaining" : " days remaining"));
	add(BonusDuration::N_TURNS, std::to_string(remaining) + (remaining == 1 ? " round remaining" : " rounds remaining"));
	add(BonusDuration::UNTIL_BEING_ATTACKED, "until damaged by an attack");
	add(BonusDuration::UNTIL_ATTACK, "until an attack or retaliation");
	add(BonusDuration::UNTIL_OWN_ATTACK, "until its own attack");
	add(BonusDuration::STACK_GETS_TURN, "until its next turn");
	add(BonusDuration::COMMANDER_KILLED, "until the commander dies");
	add(BonusDuration::UNTIL_TAKING_INDIRECT_DAMAGE, "until indirect damage");
	add(BonusDuration::UNTIL_AFTER_ATTACK_SEQUENCE, "until the attack sequence ends");
	add(BonusDuration::STACK_ACTIVATION, "through its next creature activation");
	add(BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION, "until its next creature activation begins");
	std::string result;
	for(const auto & condition : conditions)
		result += (result.empty() ? "" : " or ") + condition;
	return result;
}
}
