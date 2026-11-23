/*
 * HeroStatRowLayout.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include <array>
#include <cstddef>

/// New Horizons hero window: one row of five compact cells (Leadership, Mana,
/// Regen, Movement, Vision) under the Specialty/Experience row. The generated
/// background and the window's widgets share these coordinates.
namespace heroStatRow
{
enum Cell : std::size_t { LEADERSHIP, MANA, REGEN, MOVEMENT, VISION, COUNT };

struct Geometry
{
	int x;
	int width;
	int iconWidth; // icon drawn ICON_HEIGHT high; text column starts after it
};

constexpr int LEFT = 12;
constexpr int TOP = 136;
constexpr int WIDTH = 420;
constexpr int HEIGHT = 38;
constexpr int ICON_HEIGHT = 22;
constexpr int LABEL_Y = TOP + 2;
constexpr int VALUE_Y = TOP + 18;
/// The skills panel starts right below this single row.
constexpr int SKILLS_TOP = TOP + HEIGHT + 6;

// Widths fit the widest usual label/value in the original small font
// ("Leadership", "200 / 200 +1"); longer values fall back to the tiny font.
constexpr std::array<Geometry, COUNT> CELLS = {{
	{LEFT, 90, 22}, {102, 77, 25}, {179, 80, 26}, {259, 101, 22}, {360, 72, 29}}};

constexpr int iconLeft(Cell cell) { return CELLS[cell].x + 3; }
constexpr int iconTop() { return TOP + (HEIGHT - ICON_HEIGHT) / 2; }
constexpr int textLeft(Cell cell) { return iconLeft(cell) + CELLS[cell].iconWidth + 1; }
constexpr int textWidth(Cell cell) { return CELLS[cell].x + CELLS[cell].width - 1 - textLeft(cell); }
constexpr int textCenter(Cell cell) { return textLeft(cell) + textWidth(cell) / 2; }

static_assert(CELLS[VISION].x + CELLS[VISION].width == LEFT + WIDTH, "Cells span the row");
}
