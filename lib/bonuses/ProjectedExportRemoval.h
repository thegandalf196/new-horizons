/*
 * ProjectedExportRemoval.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include <memory>
class CBonusSystemNode;
struct Bonus;
/// Owned record identity and borrowed native source, not an effective recipient.
/// The original graph/configuration lease remains required through replay.
struct ProjectedExportRemoval
{
	const CBonusSystemNode * source;
	std::shared_ptr<Bonus> record;
};
