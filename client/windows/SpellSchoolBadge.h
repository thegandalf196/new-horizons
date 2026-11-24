/*
 * SpellSchoolBadge.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../gui/CIntObject.h"
#include "../../lib/filesystem/ResourcePath.h"

class CPicture;

/// The school's 18x18 medallion centred on the bottom edge of a native 83x61
/// spell scroll. A two-school spell shows both medallions side by side. It is
/// composited from the spell's saved school data; there is no per-spell art,
/// and it is absent when a school has no medallion resource. The layout lives
/// in SpellSchoolBadgeLayout.h.
class SpellSchoolBadge : public CIntObject
{
	std::vector<std::shared_ptr<CPicture>> medallions;

public:
	/// Null when the spell has no New Horizons school or a medallion is missing.
	static std::shared_ptr<SpellSchoolBadge> create(SpellID spell);
	/// Space a layout must leave under the scroll for this spell's badge.
	static int overhang(SpellID spell);

	explicit SpellSchoolBadge(const std::vector<ImagePath> & medallionPaths);
};
