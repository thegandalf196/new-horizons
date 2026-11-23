/*
 * HeroClassLeanIndicator.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroClassLeanIndicator.h"

#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../gui/TextAlignment.h"
#include "../render/Colors.h"
#include "../render/IFont.h"
#include "../render/IRenderHandler.h"
#include "../widgets/Images.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/TextControls.h"
#include "../../lib/filesystem/Filesystem.h"
#include "../../lib/texts/MetaString.h"

namespace
{
constexpr auto MIGHT_ICON = "NH_hero_lean_might";
constexpr auto MAGIC_ICON = "NH_hero_lean_magic";
constexpr int ICON_SIZE = 16;
constexpr int ICON_GAP = 2;
constexpr int SEPARATOR_GAP = 5;

bool iconAvailable(const char * name)
{
	const auto path = ImagePath::builtin(name);
	const auto * resources = CResourceHandler::get();
	return resources->existsResource(path.addPrefix("SPRITES/"))
		|| resources->existsResource(path.addPrefix("DATA/"));
}

std::string leanText(const char * textID, const heroClassLean::Lean & lean, const std::string & classTextID)
{
	MetaString text;
	text.appendTextID(textID);
	text.replaceTokenNumber("%MIGHT%", lean.mightPercent);
	text.replaceTokenNumber("%MAGIC%", lean.magicPercent);
	if(!classTextID.empty())
		text.replaceTokenTextID("%CLASS%", classTextID);
	return text.toString(&GAME->translator());
}
}

HeroClassLeanIndicator::HeroClassLeanIndicator()
	: CIntObject(0)
{
	pos.w = WIDTH;
	pos.h = HEIGHT;
}

void HeroClassLeanIndicator::setHero(const CGHeroInstance & hero, const CIntObject & classLine)
{
	// A centred label's position is its text centre (its box has no size).
	moveTo(classLine.pos.center() + Point(-WIDTH / 2, TITLE_OFFSET - HEIGHT / 2));
	const auto lean = heroClassLean::forHero(hero);
	if(!lean)
	{
		disable();
		return;
	}
	enable();
	const auto classTextID = hero.getClassNameTextID();
	// The lean is class data; refreshes for the same class must not relabel the row.
	if(shownLean == lean && shownClassTextID == classTextID)
		return;
	rebuild(*lean, classTextID);
}

void HeroClassLeanIndicator::rebuild(const heroClassLean::Lean & lean, const std::string & classTextID)
{
	OBJECT_CONSTRUCTION;
	shownLean = lean;
	shownClassTextID = classTextID;
	mightIcon.reset();
	magicIcon.reset();

	// Without the provisional glyphs the row names both sides in words.
	const bool icons = iconAvailable(MIGHT_ICON) && iconAvailable(MAGIC_ICON);
	const std::string mightText = icons ? std::to_string(lean.mightPercent) + "%"
		: leanText("new-horizons.hero.classLean.might", lean, classTextID);
	const std::string magicText = icons ? std::to_string(lean.magicPercent) + "%"
		: leanText("new-horizons.hero.classLean.magic", lean, classTextID);
	const std::string separatorText = "|";

	const auto font = ENGINE->renderHandler().loadFont(FONT_SMALL);
	const int iconWidth = icons ? ICON_SIZE + ICON_GAP : 0;
	const int mightWidth = static_cast<int>(font->getStringWidth(mightText));
	const int separatorWidth = static_cast<int>(font->getStringWidth(separatorText));
	const int magicWidth = static_cast<int>(font->getStringWidth(magicText));
	const int total = iconWidth + mightWidth + SEPARATOR_GAP * 2 + separatorWidth + iconWidth + magicWidth;
	const int textY = (HEIGHT - static_cast<int>(font->getLineHeight())) / 2;
	int x = (WIDTH - total) / 2;

	const int start = x;
	if(icons)
		mightIcon = std::make_shared<CPicture>(ImagePath::builtin(MIGHT_ICON), Point(x, 0));
	x += iconWidth;
	mightValue = std::make_shared<CLabel>(x, textY, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, mightText);
	x += mightWidth + SEPARATOR_GAP;
	separator = std::make_shared<CLabel>(x, textY, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::YELLOW, separatorText);
	x += separatorWidth + SEPARATOR_GAP;
	if(icons)
		magicIcon = std::make_shared<CPicture>(ImagePath::builtin(MAGIC_ICON), Point(x, 0));
	x += iconWidth;
	magicValue = std::make_shared<CLabel>(x, textY, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, magicText);

	// One help region spans the visible row; decorative icons never take input.
	helpArea = std::make_shared<LRClickableAreaWText>(Rect(start, 0, total, HEIGHT),
		leanText("new-horizons.hero.classLean.hover", lean, classTextID),
		leanText("new-horizons.hero.classLean.description", lean, classTextID));
}
