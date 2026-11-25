/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CFourOfferLevelWindow.h"
#include "../widgets/Buttons.h"
#include "../widgets/CComponent.h"
#include "../widgets/TextControls.h"

void CFourOfferLevelWindow::selectSlot(size_t slot)
{
	if(waiting || resolved || !slotAvailable(slot))
		return;
	selectedSlot = slot;
	for(size_t i = 0; i < slotBoxes.size(); ++i)
		if(i != slot && slotBoxes[i])
			slotBoxes[i]->clearSelection();
	refreshControls();
}

void CFourOfferLevelWindow::selectPerk(size_t index)
{
	if(waiting || resolved)
		return;
	selectedPerk = index;
	instruction->setText("Choose one perk, or return to the offers. Right-click a perk for details.");
	refreshControls();
}

void CFourOfferLevelWindow::showOffers()
{
	if(waiting || resolved)
		return;
	openGroup.reset();
	selectedPerk.reset();
	createChoices();
}

void CFourOfferLevelWindow::changePage(int direction)
{
	if(waiting || resolved || !openGroup)
		return;
	const size_t pages = (perkGroups[*openGroup].size() + 3) / 4;
	page = direction > 0 ? (page + 1) % pages : (page + pages - 1) % pages;
	selectedPerk.reset();
	createChoices();
}

void CFourOfferLevelWindow::confirmSelection()
{
	if(waiting || resolved)
		return;
	// External instruction can change legal choices without rerolling a parent.
	// A changed readback requires a fresh confirmation, never a redirected click.
	if(refreshEligibility())
		return;
	if(legalReplies.empty())
	{
		submitReply(0);
		return;
	}
	if(!openGroup)
	{
		if(!selectedSlot)
			return;
		if(*selectedSlot < 2)
			submitReply(static_cast<int>(*selectedSlot));
		else
		{
			openGroup = *selectedSlot - 2;
			page = 0;
			selectedPerk.reset();
			createChoices();
		}
		return;
	}
	if(!selectedPerk)
		return;
	submitReply(perkReply(*openGroup, *selectedPerk));
}

void CFourOfferLevelWindow::submitReply(int reply)
{
	// Latch before dispatch; repeated clicks cannot submit another advancement.
	waiting = true;
	refreshControls();
	const int sent = submit(reply);
	if(sent >= 0)
		request = static_cast<uint32_t>(sent);
	else
	{
		waiting = false;
		instruction->setText("The choice could not be sent. Please try again.");
		refreshControls();
	}
}

void CFourOfferLevelWindow::requestReroll(size_t slot)
{
	if(waiting || resolved || !rerollable[slot] || !reroll)
		return;
	waiting = true;
	refreshControls();
	const int sent = reroll(slot);
	if(sent >= 0)
		request = static_cast<uint32_t>(sent);
	else
	{
		waiting = false;
		refreshControls();
	}
}

void CFourOfferLevelWindow::refreshControls()
{
	const bool blocked = waiting || resolved;
	for(size_t slot = 0; slot < slotBoxes.size(); ++slot)
		if(slotBoxes[slot])
			slotBoxes[slot]->setInputEnabled(!blocked && slotAvailable(slot));
	for(size_t slot = 0; slot < rerollButtons.size(); ++slot)
	{
		rerollButtons[slot]->setEnabled(!openGroup && rerollable[slot]);
		rerollButtons[slot]->block(blocked);
	}
	if(perkBox)
		perkBox->setInputEnabled(!blocked);
	if(confirm)
		confirm->block(blocked || (!legalReplies.empty() && !(openGroup ? selectedPerk.has_value() : selectedSlot.has_value())));
	back->setEnabled(openGroup.has_value());
	const bool paged = openGroup && perkGroups[*openGroup].size() > 4;
	previousPage->setEnabled(paged);
	nextPage->setEnabled(paged);
	for(const auto & button : {back, previousPage, nextPage})
		button->block(blocked);
}
