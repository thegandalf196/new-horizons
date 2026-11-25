/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once

#include "../gui/CIntObject.h"
#include "../../lib/battle/ConsecratedArms.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

class CGHeroInstance;
class CGStatusBar;
class CMultiLineLabel;

/// Choosing the prayers of a committed Chosen Prayers Order (or Sermon spell)
/// on the Prayers bookmark. Closing the book without completing the choice
/// skips the prayer; it never cancels the Order or spell.
struct SpellbookPrayerChoice
{
	size_t required = 1; ///< Two different prayers with Antiphonal Prayer.
	std::string purpose; ///< The Order or spell the prayer accompanies.
	bool spell = false;  ///< Loynis's Sermon: the prayer accompanies a Light spell.
	std::function<void(std::vector<consecratedArms::Prayer>)> chosen;
	std::function<void()> skipped;
};

/// Order prayers, kept outside spell acquisition and casting: a read-only
/// catalogue, or the choice of a committed Order's prayers.
class SpellbookPrayers final : public CIntObject
{
	std::vector<std::shared_ptr<CIntObject>> elements;
	std::vector<std::shared_ptr<CIntObject>> entries;
	std::vector<std::string> entryNames;
	std::shared_ptr<CMultiLineLabel> hintLabel;
	/// Choice mode only; zero required choices is the read-only catalogue.
	size_t requiredChoices = 0;
	std::vector<consecratedArms::Prayer> chosenPrayers;
	std::function<void(std::vector<consecratedArms::Prayer>)> onComplete;

	void toggle(consecratedArms::Prayer prayer);
	void refreshChoice();
public:
	static bool availableFor(const CGHeroInstance & hero);
	SpellbookPrayers(Rect bounds, int rightColumn, bool large,
		const CGHeroInstance & hero, std::shared_ptr<CGStatusBar> status);
	void filter(const std::string & search);
	/// Left-click then chooses a prayer instead of explaining it; right-click
	/// still explains. `complete` receives the finished choice once.
	void enableChoice(const SpellbookPrayerChoice & choice,
		std::function<void(std::vector<consecratedArms::Prayer>)> complete);
};
