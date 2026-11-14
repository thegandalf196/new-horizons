/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include <array>
#include <cstddef>

namespace creatureStatLayout
{
enum class Stat : size_t
{
	ATTACK,
	DEFENCE,
	SHOTS,
	DAMAGE,
	HEALTH,
	HEALTH_LEFT,
	SPEED,
	INITIATIVE,
	RETALIATIONS,
	PHYSICAL_RESISTANCE,
	MAGICAL_RESISTANCE,
	MANA,
	LEADERSHIP,
	RANK,
	AFTER_LAST
};

inline constexpr size_t STAT_COUNT = static_cast<size_t>(Stat::AFTER_LAST);
inline constexpr int ROW_HEIGHT = 19;
inline constexpr int BONUS_ROW_HEIGHT = 59;
inline constexpr int BUTTON_PANEL_HEIGHT = 43;

constexpr size_t row(Stat stat, bool newHorizons)
{
	const auto index = static_cast<size_t>(stat);
	return !newHorizons && index >= static_cast<size_t>(Stat::MANA) ? index - 4 : index;
}

constexpr int iconY(Stat stat, bool newHorizons)
{
	constexpr std::array<int, 7> originalRows{31, 49, 69, 88, 107, 126, 144};
	const auto index = static_cast<size_t>(stat);
	return index < originalRows.size() ? originalRows[index]
		: 164 + (static_cast<int>(row(stat, newHorizons)) - 7) * ROW_HEIGHT;
}

constexpr int panelRows(bool newHorizons, bool rank)
{
	return (newHorizons ? 13 : 9) + (rank ? 1 : 0);
}

constexpr int panelHeight(bool newHorizons, bool rank)
{
	return 35 + panelRows(newHorizons, rank) * ROW_HEIGHT;
}
}
