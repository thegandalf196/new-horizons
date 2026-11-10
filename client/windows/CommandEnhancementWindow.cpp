/*
 * CommandEnhancementWindow.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "CommandEnhancementWindow.h"
#include "CommandPresentation.h"
#include "../GameEngine.h"
#include "../gui/WindowHandler.h"
#include "../gui/Shortcut.h"
#include "../render/Colors.h"
#include "../widgets/Buttons.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/TextControls.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include <boost/algorithm/string/replace.hpp>
#include <stdexcept>

CommandEnhancementWindow::CommandEnhancementWindow(CommandEnhancementOfferView savedOffer,
	QueryID query, std::function<int(int)> callback)
	: CWindowObject(BORDERED, ImagePath::builtin("newHorizonsOrdersBackground.png")),
	offer(std::move(savedOffer)), state(query.getNum()), submit(std::move(callback))
{
	if(query < 0 || offer.hero == ObjectInstanceID::NONE || !offer.acquisitionLevel || offer.milestone < 6 || !submit
		|| offer.recipient != newHorizonsHeroes::commandMilestoneRecipient(offer.milestone))
		throw std::invalid_argument("Incomplete captured Command enhancement offer");
	OBJECT_CONSTRUCTION;
	addUsedEvents(KEYBOARD);
	elements.push_back(std::make_shared<CLabel>(320, 22, FONT_BIG, ETextAlignment::CENTER,
		Colors::YELLOW, LIBRARY->generaltexth->translate("new-horizons.command.selector.title"), 600));
	auto acquisition = LIBRARY->generaltexth->translate("new-horizons.command.selector.acquisition");
	boost::replace_all(acquisition, "%MILESTONE%", std::to_string(offer.milestone));
	boost::replace_all(acquisition, "%LEVEL%", std::to_string(offer.acquisitionLevel));
	boost::replace_all(acquisition, "%HERO%", offer.heroName);
	elements.push_back(std::make_shared<CLabel>(320, 51, FONT_SMALL, ETextAlignment::CENTER,
		Colors::WHITE, acquisition, 600));
	elements.push_back(std::make_shared<CMultiLineLabel>(Rect(20, 64, 600, 30), FONT_SMALL,
		ETextAlignment::CENTER, Colors::YELLOW, commandPresentation::recipientName(offer.recipient)));
	std::array<bool, 9> seen{};
	for(size_t index = 0; index < offer.options.size(); ++index)
	{
		const auto & option = offer.options[index];
		const auto kind = static_cast<size_t>(option.enhancement);
		if(kind >= seen.size() || seen[kind] || option.contribution < 0)
			throw std::invalid_argument("Invalid captured Command enhancement option");
		seen[kind] = true;
		const int x = 12 + static_cast<int>(index % 3) * 208;
		const int y = 98 + static_cast<int>(index / 3) * 108;
		const auto name = commandPresentation::enhancementName(option.enhancement);
		const auto description = commandPresentation::modifiers(
			newHorizonsHeroes::commandEnhancementModifiers(option.enhancement, option.contribution));
		elements.push_back(std::make_shared<TransparentFilledRectangle>(Rect(x, y, 200, 100),
			ColorRGBA(25, 16, 10, 110), ColorRGBA(154, 119, 66)));
		elements.push_back(std::make_shared<CLabel>(x + 100, y + 13, FONT_SMALL,
			ETextAlignment::CENTER, Colors::YELLOW, name, 190));
		elements.push_back(std::make_shared<CMultiLineLabel>(Rect(x + 6, y + 28, 188, 28),
			FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, description));
		auto choose = [this, index]() { select(static_cast<int>(index)); };
		choices[index] = std::make_shared<CButton>(Point(x + 5, y + 61),
			AnimationPath::builtin("settingsWindow/button190"), CButton::tooltip(name, description),
			choose,
			index < 8 ? vstd::next(EShortcut::SELECT_INDEX_1, index) : EShortcut::NONE);
	}
	status = std::make_shared<CLabel>(320, 433, FONT_SMALL, ETextAlignment::CENTER,
		Colors::WHITE, LIBRARY->generaltexth->translate("new-horizons.command.selector.status.initial"), 600);
	auto confirmChoice = [this]() { confirmSelection(); };
	confirm = std::make_shared<CButton>(Point(225, 457), AnimationPath::builtin("settingsWindow/button190"),
		CButton::tooltip(LIBRARY->generaltexth->translate("new-horizons.command.selector.confirm"),
			LIBRARY->generaltexth->translate("new-horizons.command.selector.confirm.help")),
		confirmChoice, EShortcut::GLOBAL_ACCEPT);
	confirm->setTextOverlay(LIBRARY->generaltexth->translate("new-horizons.command.selector.confirm"), FONT_SMALL, Colors::YELLOW);
	refreshControls();
	updateShadow();
	center();
}

void CommandEnhancementWindow::refreshControls()
{
	for(size_t index = 0; index < choices.size(); ++index)
	{
		const bool selected = state.selected() == static_cast<int>(index);
		choices[index]->setTextOverlay(LIBRARY->generaltexth->translate(selected
			? "new-horizons.command.selector.selected" : "new-horizons.command.selector.select"), FONT_SMALL,
			selected ? Colors::YELLOW : Colors::WHITE);
		choices[index]->setBorderColor(selected ? std::make_optional(Colors::YELLOW) : std::nullopt);
		choices[index]->block(state.isWaiting());
	}
	confirm->block(state.isWaiting() || state.selected() < 0);
}

void CommandEnhancementWindow::select(int index)
{
	if(!state.select(index))
		return;
	auto selected = LIBRARY->generaltexth->translate("new-horizons.command.selector.status.selected");
	boost::replace_all(selected, "%NAME%", commandPresentation::enhancementName(offer.options[index].enhancement));
	status->setText(selected);
	refreshControls();
}

void CommandEnhancementWindow::keyPressed(EShortcut key)
{
	int step = 0;
	if(key == EShortcut::MOVE_LEFT) step = -1;
	if(key == EShortcut::MOVE_RIGHT) step = 1;
	if(key == EShortcut::MOVE_UP) step = -3;
	if(key == EShortcut::MOVE_DOWN) step = 3;
	if(step)
		select(state.selected() < 0 ? 0 : (state.selected() + step + 9) % 9);
}

void CommandEnhancementWindow::confirmSelection()
{
	if(!state.beginSubmission())
		return;
	status->setText(LIBRARY->generaltexth->translate("new-horizons.command.selector.status.applying"));
	refreshControls();
	const int request = submit(state.selected());
	state.submitted(request);
	if(request < 0)
	{
		status->setText(LIBRARY->generaltexth->translate("new-horizons.command.selector.status.notSubmitted"));
		refreshControls();
	}
}

void CommandEnhancementWindow::requestApplied(uint32_t requestID, bool success)
{
	if(!state.requestApplied(requestID, success))
		return;
	status->setText(LIBRARY->generaltexth->translate("new-horizons.command.selector.status.rejected"));
	refreshControls();
}

void CommandEnhancementWindow::queryResolved(QueryID query)
{
	state.queryResolved(query.getNum());
	close();
}

void CommandEnhancementWindow::close()
{
	if(state.canClose(ENGINE->windows().isTopWindow(this)))
		CWindowObject::close();
}
