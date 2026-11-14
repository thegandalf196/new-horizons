/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ReachCursor.h"
#include "ICursor.h"
#include "../../lib/Point.h"

namespace reachCursor
{
void Frame::apply(ICursor & cursor, int rasterScaling) const
{
	// AUTO canvases already contain rasterScaling pixels per logical pixel.
	// Both cursor backends consume that physical surface; public UI coordinates
	// and the stored hotspot remain logical.
	cursor.setImage(image, Point(hotspot.x * rasterScaling, hotspot.y * rasterScaling));
}
}
