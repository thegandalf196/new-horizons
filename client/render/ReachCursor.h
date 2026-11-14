/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class IImage;
class ICursor;

namespace reachCursor
{
enum class Direction : uint8_t { NORTH_EAST, EAST, SOUTH_EAST, SOUTH_WEST, WEST, NORTH_WEST, NORTH, SOUTH };

struct PixelPoint
{
	int x = 0;
	int y = 0;
	bool operator==(const PixelPoint &) const = default;
};

struct Key
{
	Direction direction;
	int64_t maximumReach;
	bool operator==(const Key &) const = default;
	bool operator<(const Key & other) const;
};

struct Color
{
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
};

struct Body
{
	static constexpr int SIZE = 32;
	std::array<uint8_t, SIZE * SIZE> pixels{};
	PixelPoint hotspot;
	PixelPoint inkTopLeft;
	PixelPoint inkBottomRight;
};

struct TextRun
{
	std::string text;
	PixelPoint position;
};

struct NumberMetrics
{
	std::array<int, 10> digitWidths;
	int lineHeight;
};

struct Layout
{
	Body body;
	PixelPoint bodyOffset;
	PixelPoint hotspot;
	PixelPoint size;
	std::vector<TextRun> number;
};

struct Frame
{
	std::shared_ptr<IImage> image;
	PixelPoint hotspot;
	void apply(ICursor & cursor, int rasterScaling) const;
};

bool valid(const Key & key);
Body makeBody(Direction direction);
std::optional<Layout> makeLayout(const Key & key, const NumberMetrics & metrics);
const std::array<Color, 11> & palette();
Frame composeImage(const Key & key);

/// Cursor-owned cache. Keys contain only the displayed direction and current
/// maximum, so a changed bonus selects a new complete cursor image immediately.
class Cache
{
	std::map<Key, Frame> frames;
public:
	static constexpr size_t CAPACITY = 64;
	using Composer = Frame (*)(const Key &);
	const Frame * get(const Key & key, Composer compose);
	void clear();
	size_t size() const;
};
}
