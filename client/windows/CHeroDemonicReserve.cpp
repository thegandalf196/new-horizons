/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CHeroWindow.h"
#include "GUIClasses.h"
#include "../CPlayerInterface.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../gui/WindowHandler.h"
#include "../widgets/Buttons.h"
#include "../widgets/CGarrisonInt.h"
#include "../../lib/battle/NewHorizonsDemonicGating.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

void CHeroWindow::refreshDemonicReserveVisibility()
{
	if(!demonicReserveButton)
		return;
	// A captured old reserve remains accessible. Current Gating creates
	// temporary reinforcements; any stranded old reserve permits withdrawal only.
	const bool temporary = curHero && newHorizonsDemonicGating::usesTemporaryGating(curHero);
	const bool hasReserve = curHero && std::any_of(curHero->getDemonicReserve().begin(),
		curHero->getDemonicReserve().end(), [](const auto & entry) { return entry.second > 0; });
	const bool available = curHero && (temporary ? hasReserve
		: curHero->getPerkSkillRank("new-horizons:demonicGating") > 0);
	demonicReserveButton->setHelp(CButton::tooltip("Demonic Reserve", temporary
		? "Return creatures from the Demonic Reserve to the active army."
		: "Select an Inferno army stack to store it, or clear the selection to withdraw reserve troops."));
	if(available)
		demonicReserveButton->enable();
	else
		demonicReserveButton->disable();
	demonicReserveButton->block(available && curHero->getOwner() != GAME->interface()->playerID);
}

void CHeroWindow::demonicReserveClicked()
{
	if(!curHero || curHero->getOwner() != GAME->interface()->playerID)
		return;

	const bool temporary = newHorizonsDemonicGating::usesTemporaryGating(curHero);
	if(!temporary && curHero->getPerkSkillRank("new-horizons:demonicGating") <= 0)
		return;
	const auto * selected = !temporary && garr ? garr->getSelection() : nullptr;
	if(selected && selected->getObj() == curHero && curHero->hasStackAtSlot(selected->getSlot()))
	{
		const SlotID slot = selected->getSlot();
		const CreatureID creature = curHero->getCreature(slot)->getId();
		const TQuantity count = curHero->getStackCount(slot);
		if(creature.toCreature()->getFactionID() != FactionID::INFERNO)
		{
			GAME->interface()->showInfoDialog("Only Inferno creatures may enter the Demonic Reserve.");
			return;
		}
		const int minimumActive = curHero->stacksCount() == 1 ? 1 : 0;
		ENGINE->windows().createAndPushWindow<CSplitWindow>(creature.toCreature(),
			[this, slot, creature](int, int reserveAmount)
			{
				if(reserveAmount > 0)
					GAME->interface()->cb->arrangeDemonicReserve(curHero, slot, creature, reserveAmount, true);
			}, minimumActive, 0, count, 0,
			CSplitWindowOwner{curHero, "Hero: " + curHero->getObjectName().toString(&GAME->translator())},
			CSplitWindowOwner{nullptr, "Demonic Reserve"});
		return;
	}

	std::vector<CreatureID> creatures;
	std::vector<std::string> entries;
	for(const auto & [creature, count] : curHero->getDemonicReserve())
	{
		if(count <= 0)
			continue;
		creatures.push_back(creature);
		entries.push_back((count == 1 ? creature.toCreature()->getNameSingularTranslated()
			: creature.toCreature()->getNamePluralTranslated()) + "  " + std::to_string(count));
	}
	if(entries.empty())
	{
		if(!temporary)
			GAME->interface()->showInfoDialog("The Demonic Reserve is empty. Select an Inferno stack first.");
		return;
	}
	ENGINE->windows().pushWindow(std::make_shared<CObjectListWindow>(entries, nullptr,
		"Demonic Reserve", "Choose creatures to return to the active army.",
		[this, creatures](const int index)
		{
			if(index < 0 || static_cast<size_t>(index) >= creatures.size())
				return;
			const CreatureID creature = creatures[index];
			const TQuantity count = curHero->getDemonicReserveCount(creature);
			if(count <= 0)
				return;
			ENGINE->windows().createAndPushWindow<CSplitWindow>(creature.toCreature(),
				[this, creature](int, int activeAmount)
				{
					if(activeAmount > 0)
						GAME->interface()->cb->arrangeDemonicReserve(curHero, SlotID(), creature, activeAmount, false);
				}, 0, 0, count, 0,
				CSplitWindowOwner{nullptr, "Demonic Reserve"},
				CSplitWindowOwner{curHero, "Hero: " + curHero->getObjectName().toString(&GAME->translator())});
		}));
}
