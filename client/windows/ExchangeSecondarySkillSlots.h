/*
 * ExchangeSecondarySkillSlots.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include <cstddef>

inline constexpr std::size_t EXCHANGE_SECONDARY_SKILL_SLOTS = 8;

/// Refresh every persistent widget, including empty slots. Overflow replaces the
/// last skill with the separate overflow indicator, without changing widget count.
template<typename Skills, typename Refresh>
bool refreshExchangeSecondarySkillSlots(const Skills & skills, Refresh refresh)
{
	const bool overflow = skills.size() > EXCHANGE_SECONDARY_SKILL_SLOTS;
	for(std::size_t index = 0; index < EXCHANGE_SECONDARY_SKILL_SLOTS; ++index)
	{
		const auto * skill = index < skills.size()
			&& !(overflow && index == EXCHANGE_SECONDARY_SKILL_SLOTS - 1)
			? &skills[index] : nullptr;
		refresh(index, skill);
	}
	return overflow;
}
