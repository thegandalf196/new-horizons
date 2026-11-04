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
#include "../../lib/bonuses/BonusCustomTypes.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/constants/EntityIdentifiers.h"
#include "../../lib/mapObjectConstructors/CObjectClassesHandler.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../../lib/texts/MetaString.h"

namespace
{
std::string effectText(const Bonus & bonus)
{
	if(bonus.valType == BonusValueType::ADDITIVE_VALUE
		&& (bonus.type == BonusType::LUCK || bonus.type == BonusType::MORALE))
	{
		MetaString text = MetaString::createFromRawString("%s %+d");
		text.replaceTextID(bonus.type == BonusType::LUCK ? "core.genrltxt.385" : "core.genrltxt.384");
		text.replacePositiveNumber(bonus.val);
		return text.toString(&GAME->translator());
	}
	if(bonus.type == BonusType::MOVEMENT && bonus.valType == BonusValueType::ADDITIVE_VALUE
		&& bonus.subtype == BonusSubtypeID(BonusCustomSubtype::heroMovementLand))
		return (bonus.val >= 0 ? "+" : "") + std::to_string(bonus.val) + " land movement points";
	// Preserve catalogue formatting and hidden-marker suppression for every other kind.
	return LIBRARY->bth->bonusToString(std::make_shared<Bonus>(bonus));
}

std::string effectSource(const Bonus & bonus)
{
	if(bonus.source == BonusSource::OBJECT_TYPE && bonus.sid == BonusSourceID(MapObjectID(Obj::STABLES))
		&& bonus.description.empty() && bonus.stacking == "stables"
		&& bonus.type == BonusType::MOVEMENT && bonus.valType == BonusValueType::ADDITIVE_VALUE
		&& bonus.subtype == BonusSubtypeID(BonusCustomSubtype::heroMovementLand)
		&& bonus.duration == BonusDuration::ONE_WEEK)
		return LIBRARY->generaltexth->translate(LIBRARY->objtypeh->getObjectGroupNameTextID(Obj::STABLES));
	return bonus.Description(GAME->interface()->cb.get());
}

std::string effectList(const CGHeroInstance & hero)
{
	std::string result;
	const auto bonuses = hero.getBonuses(Selector::all);
	for(const auto & bonus : *bonuses)
	{
		if(!heroTemporaryEffects::visibleDuration(bonus->duration, bonus->turnsRemain))
			continue;
		const auto effect = effectText(*bonus);
		if(effect.empty())
			continue;
		const auto source = effectSource(*bonus);
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
