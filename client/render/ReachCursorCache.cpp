/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "ReachCursor.h"

#include <tuple>
#include <utility>

namespace reachCursor
{
bool Key::operator<(const Key & other) const
{
	return std::tie(direction, maximumReach) < std::tie(other.direction, other.maximumReach);
}

bool valid(const Key & key)
{
	return static_cast<unsigned>(key.direction) <= static_cast<unsigned>(Direction::SOUTH)
		&& key.maximumReach > 1;
}

const Frame * Cache::get(const Key & key, Composer compose)
{
	if(!valid(key) || !compose)
		return nullptr;
	if(const auto found = frames.find(key); found != frames.end())
		return &found->second;
	auto frame = compose(key);
	if(frames.size() >= CAPACITY)
		frames.clear();
	return &frames.emplace(key, std::move(frame)).first->second;
}

void Cache::clear()
{
	frames.clear();
}

size_t Cache::size() const
{
	return frames.size();
}
}
