/*
 * HeroDevelopmentNavigation.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include <array>
#include <cstddef>
#include <optional>

enum class HeroDevelopmentSection
{
	GROWTH,
	LEADERSHIP,
	SIEGE,
	MASTERIES,
	COUNT
};

/// Presentation state only. Availability comes from actual saved optional views;
/// selecting a section never chooses a mastery or changes the hero's rules.
class HeroDevelopmentNavigation
{
public:
	static constexpr std::size_t SECTION_COUNT = static_cast<std::size_t>(HeroDevelopmentSection::COUNT);
	using Availability = std::array<bool, SECTION_COUNT>;

private:
	Availability available{};
	std::optional<HeroDevelopmentSection> current;

public:
	bool isAvailable(HeroDevelopmentSection section) const
	{
		const auto index = static_cast<std::size_t>(section);
		return index < available.size() && available[index];
	}

	void refresh(const Availability & updated)
	{
		available = updated;
		if(current && isAvailable(*current))
			return;

		current.reset();
		for(std::size_t index = 0; index < available.size(); ++index)
			if(available[index])
			{
				current = static_cast<HeroDevelopmentSection>(index);
				break;
			}
	}

	bool select(HeroDevelopmentSection section)
	{
		if(!isAvailable(section))
			return false;
		current = section;
		return true;
	}

	std::optional<HeroDevelopmentSection> selected() const
	{
		return current;
	}
};
