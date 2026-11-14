/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CursorHandler.h"
#include "../render/ReachCursor.h"
#include "../GameEngine.h"
#include "render/IScreenHandler.h"

namespace
{
std::optional<reachCursor::Direction> reachDirection(Cursor::Combat cursor)
{
	using Direction = reachCursor::Direction;
	switch(cursor)
	{
		case Cursor::Combat::HIT_NORTHEAST: return Direction::NORTH_EAST;
		case Cursor::Combat::HIT_EAST: return Direction::EAST;
		case Cursor::Combat::HIT_SOUTHEAST: return Direction::SOUTH_EAST;
		case Cursor::Combat::HIT_SOUTHWEST: return Direction::SOUTH_WEST;
		case Cursor::Combat::HIT_WEST: return Direction::WEST;
		case Cursor::Combat::HIT_NORTHWEST: return Direction::NORTH_WEST;
		case Cursor::Combat::HIT_NORTH: return Direction::NORTH;
		case Cursor::Combat::HIT_SOUTH: return Direction::SOUTH;
		default: return std::nullopt;
	}
}
}

bool CursorHandler::setReach(Cursor::Combat direction, int64_t maximumReach)
{
	assert(dndObject == nullptr);
	const auto orientation = reachDirection(direction);
	if(!orientation || maximumReach <= 1)
		return false;
	const auto scaling = ENGINE->screenHandler().getScalingFactor();
	if(reachCursorRasterScaling != scaling)
	{
		reachCursorFrames->clear();
		currentReachCursor.reset();
		reachCursorRasterScaling = scaling;
	}
	const auto selection = std::pair(direction, maximumReach);
	if(currentReachCursor == selection)
		return true;
	const auto * frame = reachCursorFrames->get({*orientation, maximumReach}, reachCursor::composeImage);
	if(!frame || !frame->image)
	{
		currentCursorID.clear();
		return false;
	}
	cursorImage = frame->image;
	reachCursorPivot = Point(frame->hotspot.x, frame->hotspot.y);
	currentReachCursor = selection;
	frameTime = 0;
	applyCursorImage();
	return true;
}
