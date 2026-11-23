/*
 * HeroWindowStatRow.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "CHeroWindow.h"
#include "HeroStatRowLayout.h"
#include "HeroWindowLabelText.h"

#include "../GameEngine.h"
#include "../gui/TextAlignment.h"
#include "../render/IFont.h"
#include "../render/IRenderHandler.h"
#include "../widgets/Images.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/TextControls.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/texts/CGeneralTextHandler.h"

using heroStatRow::Cell;

namespace
{
Rect cellRect(Cell cell)
{
	return Rect(heroStatRow::CELLS[cell].x, heroStatRow::TOP, heroStatRow::CELLS[cell].width, heroStatRow::HEIGHT);
}

Rect iconRect(Cell cell)
{
	return Rect(heroStatRow::iconLeft(cell), heroStatRow::iconTop(), heroStatRow::CELLS[cell].iconWidth,
		heroStatRow::ICON_HEIGHT);
}

std::shared_ptr<CLabel> statLabel(Cell cell, int y, const ColorRGBA & color, const std::string & text = "")
{
	return std::make_shared<CLabel>(heroStatRow::textCenter(cell), y, FONT_SMALL, ETextAlignment::TOPCENTER, color, text);
}

std::shared_ptr<CPicture> statPicture(const char * image, Cell cell)
{
	const auto rect = iconRect(cell);
	auto picture = std::make_shared<CPicture>(ImagePath::builtin(image), rect.topLeft());
	picture->scaleTo(Point(rect.w, rect.h));
	return picture;
}
}

void CHeroWindow::configureStatRow()
{
	const auto move = [this](const auto & widget, Point point)
	{
		widget->moveTo(pos.topLeft() + point);
	};
	const auto place = [&move](const auto & widget, const Rect & rect)
	{
		move(widget, rect.topLeft());
		widget->pos.w = rect.w;
		widget->pos.h = rect.h;
	};
	leadershipImage = std::make_shared<CAnimImage>(AnimationPath::builtin("NH_hero_leadership_44"), 0, iconRect(Cell::LEADERSHIP));
	capabilityIcons.push_back(leadershipImage);
	commandArea = std::make_shared<LRClickableAreaWText>(cellRect(Cell::LEADERSHIP), LIBRARY->generaltexth->translate("new-horizons.command.description"));
	leadershipLabel = statLabel(Cell::LEADERSHIP, heroStatRow::LABEL_Y, Colors::YELLOW, "Leadership");
	commandValue = statLabel(Cell::LEADERSHIP, heroStatRow::VALUE_Y, Colors::WHITE);

	manaOrbImage = statPicture("heroWindow/manaOrb", Cell::MANA);
	place(spellPointsArea, cellRect(Cell::MANA));
	labels[6]->alignment = ETextAlignment::TOPCENTER;
	move(labels[6], Point(heroStatRow::textCenter(Cell::MANA), heroStatRow::LABEL_Y));
	labels[6]->pos.w = 0;
	labels[6]->pos.h = 0;
	labels[6]->setMaxWidth(0);
	labels[6]->setText("Mana");
	manaValue = statLabel(Cell::MANA, heroStatRow::VALUE_Y, Colors::WHITE);

	manaRegenImage = statPicture("heroWindow/manaRegen", Cell::REGEN);
	manaRegenArea = std::make_shared<LRClickableAreaWText>(cellRect(Cell::REGEN), "");
	manaRegenLabel = statLabel(Cell::REGEN, heroStatRow::LABEL_Y, Colors::YELLOW,
		LIBRARY->generaltexth->translate("new-horizons.hero.manaRegen.short"));
	manaRegenValue = statLabel(Cell::REGEN, heroStatRow::VALUE_Y, Colors::WHITE);

	movementDynamicImage = std::make_shared<CAnimImage>(AnimationPath::builtin("NH_hero_movement_44"), 0, iconRect(Cell::MOVEMENT));
	capabilityIcons.push_back(movementDynamicImage);
	movementArea = std::make_shared<LRClickableAreaWText>(cellRect(Cell::MOVEMENT), "Movement points");
	movementDynamicLabel = statLabel(Cell::MOVEMENT, heroStatRow::LABEL_Y, Colors::YELLOW, "Movement");
	movementValue = statLabel(Cell::MOVEMENT, heroStatRow::VALUE_Y, Colors::WHITE);
	movementGrowthValue = statLabel(Cell::MOVEMENT, heroStatRow::VALUE_Y, Colors::GREEN);

	visionImage = statPicture("heroWindow/visionTelescope", Cell::VISION);
	visionArea = std::make_shared<LRClickableAreaWText>(cellRect(Cell::VISION), "Vision radius");
	visionLabel = statLabel(Cell::VISION, heroStatRow::LABEL_Y, Colors::YELLOW, "Vision");
	visionValue = statLabel(Cell::VISION, heroStatRow::VALUE_Y, Colors::WHITE);
}

void CHeroWindow::fitStatRow()
{
	// Long labels or values ("Sea Movement", four-digit pools) use the tiny
	// font instead of spilling across the cell dividers.
	const auto fit = [](const std::shared_ptr<CLabel> & label, Cell cell)
	{
		if(label)
			heroWindowLabelText::setFittingText(*label, label->getText(), heroStatRow::textWidth(cell), {FONT_SMALL, FONT_TINY});
	};
	fit(leadershipLabel, Cell::LEADERSHIP);
	fit(commandValue, Cell::LEADERSHIP);
	fit(labels[6], Cell::MANA);
	fit(manaValue, Cell::MANA);
	fit(manaRegenLabel, Cell::REGEN);
	fit(manaRegenValue, Cell::REGEN);
	fit(movementDynamicLabel, Cell::MOVEMENT);
	fit(visionLabel, Cell::VISION);
	fit(visionValue, Cell::VISION);
	if(!movementValue || !movementGrowthValue)
		return;
	// The value and its green growth share one line and one font.
	const auto width = [](EFonts font, const std::shared_ptr<CLabel> & label)
	{
		return static_cast<int>(ENGINE->renderHandler().loadFont(font)->getStringWidth(label->getText()));
	};
	const bool growth = !movementGrowthValue->getText().empty();
	const int small = width(FONT_SMALL, movementValue) + (growth ? 4 + width(FONT_SMALL, movementGrowthValue) : 0);
	const auto font = small <= heroStatRow::textWidth(Cell::MOVEMENT) ? FONT_SMALL : FONT_TINY;
	for(const auto & label : {movementValue, movementGrowthValue})
		if(label->font != font)
		{
			label->font = font;
			label->setText(label->getText());
		}
}
