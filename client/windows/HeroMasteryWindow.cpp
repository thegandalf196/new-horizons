/*
 * HeroMasteryWindow.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroMasteryWindow.h"

#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../CPlayerInterface.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../gui/Shortcut.h"
#include "../gui/WindowHandler.h"
#include "../render/Colors.h"
#include "../widgets/Buttons.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/Images.h"
#include "../widgets/TextControls.h"

HeroMasteryWindow::HeroMasteryWindow(const newHorizonsHeroes::MasteryOffer & savedOffer, QueryID queryID, std::function<int(int)> callback)
	: CWindowObject(BORDERED), offer(savedOffer), state(queryID.getNum()), submit(std::move(callback))
{
	OBJECT_CONSTRUCTION;
	pos = Rect(0, 0, 700, 470);
	elements.push_back(std::make_shared<TransparentFilledRectangle>(Rect(0, 0, 700, 470), ColorRGBA(39, 35, 41), ColorRGBA(180, 154, 98), 2));
	elements.push_back(std::make_shared<CLabel>(350, 25, FONT_BIG, ETextAlignment::CENTER, Colors::YELLOW, "Choose a mastery"));
	const auto * hero = GAME->interface()->cb->getHero(offer.hero);
	const std::string heroName = hero ? GAME->translator().translate(hero->getNameTextID()) : "Hero " + std::to_string(offer.hero.getNum());
	elements.push_back(std::make_shared<CLabel>(350, 58, FONT_MEDIUM, ETextAlignment::CENTER, Colors::WHITE,
		heroName + " - " + offer.skill.toEntity(LIBRARY)->getNameTranslated() + " - Level " + std::to_string(offer.level), 656));
	elements.push_back(std::make_shared<CLabel>(350, 82, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, "Select one offered mastery, then confirm. This is not another skill rank."));
	const std::array<EShortcut, 3> shortcuts = {EShortcut::SELECT_INDEX_1, EShortcut::SELECT_INDEX_2, EShortcut::SELECT_INDEX_3};
	for(size_t i = 0; i < offer.options.size(); ++i)
	{
		const int index = static_cast<int>(i);
		const int x = 22 + index * 224;
		const auto & option = offer.options[i];
		const auto name = GAME->translator().translate(option.nameTextId);
		const auto description = newHorizonsHeroes::formatMasteryDescription(option, GAME->translator().translate(option.descriptionTextId));
		elements.push_back(std::make_shared<TransparentFilledRectangle>(Rect(x, 104, 208, 262), ColorRGBA(52, 46, 43), ColorRGBA(180, 154, 98)));
		elements.push_back(std::make_shared<CPicture>(ImagePath::builtin(option.iconKey + "_64"), x + 72, 113));
		elements.push_back(std::make_shared<CMultiLineLabel>(Rect(x + 10, 184, 188, 42), FONT_MEDIUM, ETextAlignment::CENTER, Colors::YELLOW, name));
		elements.push_back(std::make_shared<CTextBox>(description, Rect(x + 10, 233, 188, 79), 0, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE));
		choices[i] = std::make_shared<CButton>(Point(x + 9, 326), AnimationPath::builtin("settingsWindow/button190"), CButton::tooltip(name, description), [this, index] { select(index); }, shortcuts[i]);
		choices[i]->setHoverable(true);
	}
	status = std::make_shared<CLabel>(350, 388, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, "Choose explicitly using a button or keys 1, 2, 3.", 656);
	confirm = std::make_shared<CButton>(Point(255, 414), AnimationPath::builtin("settingsWindow/button190"), CButton::tooltip("Confirm mastery", "Send the selected offered choice for authoritative validation. No cancellation or automatic choice."), [this] { confirmSelection(); }, EShortcut::GLOBAL_ACCEPT);
	confirm->setTextOverlay("Confirm mastery", FONT_SMALL, Colors::YELLOW);
	confirm->setHoverable(true);
	refreshControls();
	updateShadow();
	center();
}

void HeroMasteryWindow::refreshControls()
{
	for(size_t i = 0; i < choices.size(); ++i)
	{
		const bool chosen = state.selected() == static_cast<int>(i);
		choices[i]->setTextOverlay(chosen ? "Selected" : "Select", FONT_SMALL, chosen ? Colors::YELLOW : Colors::WHITE);
		choices[i]->setBorderColor(chosen ? std::make_optional(Colors::YELLOW) : std::nullopt);
		choices[i]->block(state.isWaiting());
	}
	confirm->block(state.isWaiting() || state.selected() < 0);
}

void HeroMasteryWindow::select(int index)
{
	if(!state.select(index))
		return;
	status->setText("Selected: " + GAME->translator().translate(offer.options[index].nameTextId));
	refreshControls();
}

void HeroMasteryWindow::confirmSelection()
{
	if(!state.beginSubmission())
		return;
	status->setText("Waiting for authoritative acceptance...");
	refreshControls();
	const int submitted = submit(state.selected());
	state.submitted(submitted);
	if(submitted < 0)
	{
		status->setText("Not submitted. Select an offered mastery and try again.");
		refreshControls();
	}
}

void HeroMasteryWindow::requestApplied(uint32_t requestID, bool success)
{
	if(!state.requestApplied(requestID, success))
		return;
	status->setText("Choice rejected. Select an offered mastery and try again.");
	refreshControls();
}

void HeroMasteryWindow::queryResolved(QueryID queryID)
{
	state.queryResolved(queryID.getNum());
	close();
}

void HeroMasteryWindow::close()
{
	// A help popup may cover us when the server resolves the query. Keep the
	// resolution latched, but never pop an unrelated top window. The regular
	// pending-dialog pump retries closure after the covering window is gone.
	if(state.canClose(ENGINE->windows().isTopWindow(this)))
		CWindowObject::close();
}
