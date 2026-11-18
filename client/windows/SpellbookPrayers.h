/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once

#include "../gui/CIntObject.h"
#include <memory>
#include <string>
#include <vector>

class CGHeroInstance;
class CGStatusBar;

/// Informational Order prayers, kept outside spell acquisition and casting.
class SpellbookPrayers final : public CIntObject
{
	std::vector<std::shared_ptr<CIntObject>> elements;
	std::vector<std::shared_ptr<CIntObject>> entries;
	std::vector<std::string> entryNames;
public:
	static bool availableFor(const CGHeroInstance & hero);
	SpellbookPrayers(Rect bounds, int rightColumn, bool large,
		const CGHeroInstance & hero, std::shared_ptr<CGStatusBar> status);
	void filter(const std::string & search);
};
