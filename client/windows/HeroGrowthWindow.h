/*
 * HeroGrowthWindow.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "CWindowObject.h"

class CGHeroInstance;
class CButton;
namespace newHorizonsHeroes
{
struct PrimaryGrowthView;
}

/// Read-only display of an actual hero's authoritative growth snapshot.
/// The caller must obtain a nonempty saved-hero view; never construct one from
/// a hero-type preview or use this window to activate new rules on an old hero.
class HeroGrowthWindow : public CWindowObject
{
	std::vector<std::shared_ptr<CIntObject>> elements;
	std::shared_ptr<CButton> closeButton;

public:
	HeroGrowthWindow(const CGHeroInstance & hero, const newHorizonsHeroes::PrimaryGrowthView & growth);
};
