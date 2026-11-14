/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../constants/EntityIdentifiers.h"
#include <functional>
#include <set>
#include <vector>

namespace newHorizonsHeroes
{
/// The supplied chooser retains the authoritative per-hero weighted RNG.
/// Only pool priority and without-replacement slot layout are decided here.
DLL_LINKAGE std::vector<SecondarySkill> splitSkillOffers(std::set<SecondarySkill> upgrades,
	std::set<SecondarySkill> newSkills, int maximum,
	const std::function<SecondarySkill(const std::set<SecondarySkill> &)> & choose);
}
