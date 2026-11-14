/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CLevelWindow.h"
#include "../CPlayerInterface.h"
#include "../GameInstance.h"
#include "../widgets/Buttons.h"
#include "../widgets/CComponent.h"
#include "../widgets/TextControls.h"
#include "../render/Colors.h"

void CLevelWindow::configureReroll(QueryID queryID, bool available, std::function<int(size_t)> callback)
{
	OBJECT_CONSTRUCTION;
	levelQuery = queryID;
	rerollAvailable = available && queryID >= 0 && !skills.empty();
	rerollPending = false;
	rerollRequest.reset();
	rerollCallback = std::move(callback);
	rerollButtons.clear();
	createSkillBox();
	refreshRerollControls();
}

bool CLevelWindow::matchesQuery(QueryID queryID) const
{
	return levelQuery == queryID;
}

bool CLevelWindow::matchesHero(const CGHeroInstance * heroInstance) const
{
	return hero == heroInstance;
}

void CLevelWindow::refreshRerollControls()
{
	const bool waiting = rerollPending || selectionSubmitted;
	for(const auto & [index, button] : rerollButtons)
		button->block(waiting || !rerollAvailable || index >= skills.size());
	if(ok)
		ok->block(waiting);
	if(buttonLeft)
		buttonLeft->block(waiting);
	if(buttonRight)
		buttonRight->block(waiting);
	if(skillChoiceBox)
		skillChoiceBox->setInputEnabled(!waiting);
	if(perkChoiceBox)
		perkChoiceBox->setInputEnabled(!waiting);
}

void CLevelWindow::requestReroll(size_t skillIndex)
{
	if(!rerollAvailable || skillIndex >= skills.size() || rerollPending || selectionSubmitted || !rerollCallback)
		return;
	// Latch before sending: repeated clicks and queued Accept cannot select
	// the superseded offer while the authoritative replacement is pending.
	rerollPending = true;
	refreshRerollControls();
	const int request = rerollCallback(skillIndex);
	if(request >= 0)
		rerollRequest = static_cast<uint32_t>(request);
	else
	{
		rerollPending = false;
		refreshRerollControls();
	}
}

void CLevelWindow::rerollRequestApplied(uint32_t requestID, bool success)
{
	if(!rerollPending || !rerollRequest || *rerollRequest != requestID || success)
		return;
	// A rejection keeps the original offer. A successful ACK alone cannot
	// unlock choices: wait for the matching authoritative offer refresh.
	rerollPending = false;
	rerollRequest.reset();
	refreshRerollControls();
}

void CLevelWindow::createRerollControls()
{
	OBJECT_CONSTRUCTION;
	rerollButtons.clear();
	if(!rerollAvailable || !skillChoiceBox)
		return;
	std::vector<Rect> skillCardBounds;
	for(const auto * child : skillChoiceBox->children)
		if(dynamic_cast<const CSelectableComponent *>(child))
			skillCardBounds.push_back(child->pos);
	size_t cardIndex = 0;
	for(const auto originalIndex : displayedChoiceOrder)
	{
		if(originalIndex >= skills.size())
			continue;
		if(cardIndex >= skillCardBounds.size())
			break;
		const auto & card = skillCardBounds[cardIndex++];
		auto button = std::make_shared<CButton>(Point(),
			AnimationPath::builtin("settingsWindow/button80"),
			CButton::tooltip("Reroll", "Reroll this skill once. The other skill offer remains unchanged."),
			[this, originalIndex] { requestReroll(originalIndex); });
		// Keep every perk preview clear. A lone card leaves space for central OK.
		const int buttonX = skillCardBounds.size() == 1 ? card.x + 10 : card.x + (card.w - button->pos.w) / 2;
		button->moveTo(Point(buttonX, card.y + card.h + 14));
		button->setTextOverlay("Reroll", FONT_SMALL, Colors::YELLOW);
		rerollButtons.emplace_back(originalIndex, std::move(button));
	}
	refreshRerollControls();
}

std::optional<size_t> CLevelWindow::selectedOfferIndex() const
{
	const auto activeBox = perkChoiceActive ? perkChoiceBox : skillChoiceBox;
	if(!activeBox)
		return std::nullopt;
	const int selected = activeBox->selectedIndex();
	int categoryIndex = 0;
	for(const auto choice : displayedChoiceOrder)
		if((choice >= skills.size()) == perkChoiceActive && categoryIndex++ == selected)
			return choice;
	return std::nullopt;
}

void CLevelWindow::restoreOfferSelection(std::optional<size_t> selection)
{
	if(!selection)
		return;
	const bool perk = *selection >= skills.size();
	const auto activeBox = perk ? perkChoiceBox : skillChoiceBox;
	if(!activeBox)
		return;
	size_t index = 0;
	for(const auto choice : displayedChoiceOrder)
	{
		if((choice >= skills.size()) != perk)
			continue;
		if(choice == *selection)
		{
			activeBox->selectIndex(index);
			return;
		}
		++index;
	}
}
