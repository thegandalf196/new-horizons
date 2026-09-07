/*
 * HeroGrowthWindow.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "CWindowObject.h"
#include "HeroDevelopmentNavigation.h"
#include "../../lib/constants/EntityIdentifiers.h"

class CGHeroInstance;
class CButton;
class CTextBox;

/// Read-only display of an actual hero's independent saved development views.
/// The caller must find at least one nonempty view; never use a hero-type preview
/// or use this window to activate new rules on an old hero.
class HeroGrowthWindow : public CWindowObject
{
	ObjectInstanceID heroID;
	std::vector<std::shared_ptr<CIntObject>> elements;
	std::shared_ptr<CButton> closeButton;
	HeroDevelopmentNavigation navigation;
	std::array<std::string, HeroDevelopmentNavigation::SECTION_COUNT> sectionTexts;
	std::array<std::shared_ptr<CButton>, HeroDevelopmentNavigation::SECTION_COUNT> sectionButtons;
	std::shared_ptr<CTextBox> sectionText;

	void selectSection(HeroDevelopmentSection section);
	void updateSectionButtons();

public:
	explicit HeroGrowthWindow(const CGHeroInstance & hero);
	void refresh(const CGHeroInstance & hero);
};
