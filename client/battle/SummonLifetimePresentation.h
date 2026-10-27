/*
 * SummonLifetimePresentation.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include <cstdint>
#include <string>

namespace summonLifetimePresentation
{
inline std::string compact(int32_t rounds)
{
	return "Temporary summon: " + std::to_string(rounds) + (rounds == 1 ? " round remaining" : " rounds remaining");
}

inline std::string tooltip(int32_t rounds)
{
	return compact(rounds) + ". The current round counts toward its lifetime. "
		"The stack disappears when its remaining lifetime expires. Stasis pauses this countdown.";
}
}
