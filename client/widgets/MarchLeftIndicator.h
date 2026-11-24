/*
 * Part of VCMI / New Horizons.
 * Authors: listed in AUTHORS. GPL-2.0-or-later; see license.txt.
 */
#pragma once

#include "../gui/CIntObject.h"

class CAnimImage;
class CStackInstance;

/// Day's March marker on a garrison slot: the hero list's movement bar on the slot's
/// left edge, showing the share of today's march the stack has left. Hidden while the
/// stack still has its whole day.
class MarchLeftIndicator : public CIntObject
{
	std::shared_ptr<CAnimImage> bar;

public:
	explicit MarchLeftIndicator(Point slotSize);
	/// Hidden for an empty slot, a fresh stack or another day.
	void update(const CStackInstance * stack, int day);
};
