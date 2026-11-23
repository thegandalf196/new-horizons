/*
 * HeroClassLeanIndicator.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "HeroClassLean.h"
#include "../gui/CIntObject.h"

class CGHeroInstance;
class CLabel;
class CPicture;
class LRClickableAreaWText;

/// "[swords] 30% | [book] 70%" row under the hero window's class line. The
/// values come from the hero class's growth (see HeroClassLean.h), so the row
/// only changes when the window shows a hero of another class.
class HeroClassLeanIndicator : public CIntObject
{
	std::shared_ptr<CPicture> mightIcon;
	std::shared_ptr<CPicture> magicIcon;
	std::shared_ptr<CLabel> mightValue;
	std::shared_ptr<CLabel> separator;
	std::shared_ptr<CLabel> magicValue;
	std::shared_ptr<LRClickableAreaWText> helpArea;
	std::optional<heroClassLean::Lean> shownLean;
	std::string shownClassTextID;

	void rebuild(const heroClassLean::Lean & lean, const std::string & classTextID);

public:
	static constexpr int WIDTH = 160;
	static constexpr int HEIGHT = 16;
	/// From the centre of the FONT_MEDIUM class line to the centre of this row:
	/// half its 18px line plus half this row.
	static constexpr int TITLE_OFFSET = 17;

	HeroClassLeanIndicator();

	/// Show the hero's class lean centred under the class line. The window
	/// calls this on every refresh, so it also follows legacy/New Horizons
	/// layout changes without separate coordinates.
	void setHero(const CGHeroInstance & hero, const CIntObject & classLine);
};
