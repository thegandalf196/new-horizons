/*
 * ProjectedDurationChange.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "CBonusSystemNode.h"

/// One actual decrement, and its immediately following removal if expired.
/// Pointees are immutable once recorded. Native source identity is borrowed
/// under the arena's REAL immutable source interval, not owned by this receipt.
struct ProjectedDurationChange
{
	const CBonusSystemNode * source;
	std::shared_ptr<Bonus> before;
	std::shared_ptr<Bonus> after;
	bool removed;
};
