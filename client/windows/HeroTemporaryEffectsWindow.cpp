/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroTemporaryEffectsWindow.h"
#include "HeroTemporaryEffects.h"

#include "../CPlayerInterface.h"
#include "../GameInstance.h"
#include "../gui/Shortcut.h"
#include "../render/Colors.h"
#include "../widgets/Buttons.h"
#include "../widgets/Images.h"
#include "../widgets/Slider.h"
#include "../widgets/TextControls.h"
#include "../../lib/CBonusTypeHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

namespace
{
std::string effectList(const CGHeroInstance & hero)
{
	std::string result;
	const auto bonuses = hero.getBonuses(Selector::all);
	for(const auto & bonus : *bonuses)
	{
		if(!heroTemporaryEffects::visibleDuration(bonus->duration, bonus->turnsRemain))
			continue;
		// The bonus catalogue hides internal markers and formats actual values,
		// subtypes and scripted effects using the same rules as creature help.
		const auto effect = LIBRARY->bth->bonusToString(std::make_shared<Bonus>(*bonus));
		if(effect.empty())
			continue;
		const auto source = bonus->Description(GAME->interface()->cb.get());
		result += (result.empty() ? "" : "\n\n") + std::string("{") + source + "}\n"
			+ effect + "\n" + heroTemporaryEffects::expiry(bonus->duration, bonus->turnsRemain);
	}
	return result.empty() ? "No temporary effects are active." : result;
}
}

HeroTemporaryEffectsWindow::HeroTemporaryEffectsWindow(const CGHeroInstance & hero)
	: CWindowObject(BORDERED)
{
	OBJECT_CONSTRUCTION;
	pos = Rect(0, 0, 480, 382);
	elements.push_back(std::make_shared<CFilledTexture>(ImagePath::builtin("DiBoxBck"), Rect(0, 0, pos.w, pos.h)));
	elements.push_back(std::make_shared<CLabel>(pos.w / 2, 24, FONT_BIG, ETextAlignment::CENTER,
		Colors::YELLOW, "Temporary effects"));
	const auto effects = std::make_shared<CTextBox>(effectList(hero), Rect(20, 52, pos.w - 40, 264),
		0, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE);
	if(effects->slider)
		effects->slider->addUsedEvents(WHEEL | KEYBOARD);
	elements.push_back(effects);
	elements.push_back(std::make_shared<CButton>(Point(pos.w / 2 - 32, 334), AnimationPath::builtin("IOKAY"),
		CButton::tooltip("Close", "Return to the hero screen."), [this]() { close(); }, EShortcut::GLOBAL_CANCEL));
	updateShadow();
	center();
}
