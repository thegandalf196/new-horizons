/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ConsecratedArmsPrayerFlow.h"
#include "BattleActionsController.h"
#include "BattleHeroActionWindow.h"
#include "BattleInterface.h"
#include "ConsecratedArmsPrayerTargeting.h"

#include "../CPlayerInterface.h"
#include "../GameEngine.h"
#include "../gui/CursorHandler.h"
#include "../gui/WindowHandler.h"
#include "../windows/CSpellWindow.h"
#include "../windows/SpellbookPrayers.h"

#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpell.h"

namespace
{
using consecratedArms::Prayer;
using consecratedArmsPrayerChoice::withoutPrayer;

/// The Order or spell waiting for its optional prayer. It is sent exactly once,
/// with or without the prayer, unless the battle no longer accepts it.
struct PendingPrayerAction
{
	std::weak_ptr<BattleInterface> battle;
	BattleAction action;
	consecratedArmsPrayerUI::HeroSpellSender send;
};

std::shared_ptr<BattleInterface> prayerFlowLiveBattle(const PendingPrayerAction & pending)
{
	auto result = pending.battle.lock();
	if(!result || CPlayerInterface::battleInt != result || !result->curInt || !result->getBattle())
		return {};
	return result;
}

/// Same gate as the Orders window: the local player still commands this battle.
/// The authority validates the request again.
bool prayerFlowCommanding(BattleInterface & owner)
{
	return owner.curInt && owner.curInt->cb && owner.makingTurn() && !owner.curInt->isAutoFightOn
		&& !owner.isInTacticsMode() && owner.currentHero();
}

std::string prayerFlowPurpose(const BattleAction & action)
{
	if(action.actionType == EActionType::HERO_SPELL)
	{
		const auto * spell = action.spell.toSpell();
		return spell ? spell->getNameTranslated() : std::string("the spell");
	}
	return HeroCommandUI::name(action.command);
}

/// Returns the live battle when `pending` may still be sent; otherwise reports why not.
std::shared_ptr<BattleInterface> prayerFlowSendable(const PendingPrayerAction & pending)
{
	const auto owner = prayerFlowLiveBattle(pending);
	if(owner && prayerFlowCommanding(*owner))
		return owner;
	logGlobal->warn("Chosen Prayers: %s was not sent because the battle changed", prayerFlowPurpose(pending.action));
	if(owner && owner->curInt)
		owner->curInt->showInfoDialog("The battle changed while the prayer was being chosen, so "
			+ prayerFlowPurpose(pending.action) + " was not used. Its Hero Action was not spent.");
	return {};
}

/// Closing the book skips the prayer: the Order or spell is still sent.
void prayerFlowSkip(const PendingPrayerAction & pending)
{
	if(prayerFlowSendable(pending))
		pending.send(withoutPrayer(pending.action));
}

void prayerFlowChosen(const PendingPrayerAction & pending, const std::vector<Prayer> & prayers)
{
	const auto owner = prayerFlowSendable(pending);
	if(!owner)
		return;
	const auto request = consecratedArms::choiceRequest(*owner->getBattle(), withoutPrayer(pending.action));
	consecratedArmsPrayerChoice::Selection selection(request.prayers, request.recipients);
	for(const auto prayer : prayers)
		selection.togglePrayer(prayer);
	if(!selection.prayersComplete())
	{
		// No stack can receive the prayer any more.
		pending.send(withoutPrayer(pending.action));
		return;
	}
	if(selection.chosenRecipient())
	{
		// A single possible recipient needs no battlefield click.
		auto completed = withoutPrayer(pending.action);
		pending.send(selection.apply(completed, request) ? completed : withoutPrayer(pending.action));
		return;
	}
	auto targeting = std::make_shared<PrayerRecipientTargeting>(*owner, pending.action, std::move(selection),
		pending.send);
	if(!owner->actionsController->beginPrayerRecipientTargeting(targeting))
		targeting->deliver(targeting->conclude());
}

void prayerFlowOpenBook(const std::shared_ptr<const PendingPrayerAction> & pending, size_t required)
{
	const auto owner = prayerFlowSendable(*pending);
	if(!owner)
		return;
	SpellbookPrayerChoice choice;
	choice.required = required;
	choice.purpose = prayerFlowPurpose(pending->action);
	choice.spell = pending->action.actionType == EActionType::HERO_SPELL;
	choice.chosen = [pending](std::vector<consecratedArms::Prayer> prayers) { prayerFlowChosen(*pending, prayers); };
	choice.skipped = [pending] { prayerFlowSkip(*pending); };
	const auto * hero = owner->currentHero();
	auto book = hero && SpellbookPrayers::availableFor(*hero)
		? std::make_shared<CSpellWindow>(hero, owner->curInt.get()) : nullptr;
	if(!book || !book->choosePrayers(std::move(choice)))
	{
		logGlobal->warn("Chosen Prayers: no Prayers bookmark; %s is sent without a prayer", prayerFlowPurpose(pending->action));
		prayerFlowSkip(*pending);
		return;
	}
	ENGINE->cursor().set(Cursor::Map::POINTER);
	ENGINE->windows().pushWindow(book);
}

void prayerFlowSubmit(const std::shared_ptr<BattleInterface> & owner, const BattleAction & action,
	consecratedArmsPrayerUI::HeroSpellSender send)
{
	const auto request = owner && owner->getBattle()
		? consecratedArms::choiceRequest(*owner->getBattle(), action) : consecratedArms::ChoiceRequest{};
	if(!request.prayers)
	{
		send(action);
		return;
	}
	auto pending = std::make_shared<const PendingPrayerAction>(PendingPrayerAction{owner, action, std::move(send)});
	// Open the book after the current click: a confirming window that
	// submitted this action may still close itself, and only the top window may close.
	ENGINE->dispatchMainThread([pending, required = request.prayers] { prayerFlowOpenBook(pending, required); });
}
}

namespace consecratedArmsPrayerUI
{
void submitOrder(const std::shared_ptr<BattleInterface> & owner, const BattleAction & order)
{
	if(!owner || !owner->curInt || !owner->curInt->cb)
		return;
	const std::weak_ptr<BattleInterface> weak = owner;
	prayerFlowSubmit(owner, order, [weak](const BattleAction & action)
	{
		const auto battle = weak.lock();
		if(battle && battle->curInt && battle->curInt->cb)
			battle->curInt->cb->battleMakeSpellAction(battle->getBattleID(), action);
	});
}

void submitHeroSpell(const std::shared_ptr<BattleInterface> & owner, const BattleAction & spell, HeroSpellSender send)
{
	prayerFlowSubmit(owner, spell, std::move(send));
}
}
