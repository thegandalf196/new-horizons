/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ConsecratedArmsPrayerTargeting.h"
#include "BattleHeroActionWindow.h"
#include "BattleInterface.h"

#include "../CPlayerInterface.h"
#include "../GameEngine.h"
#include "../gui/WindowHandler.h"

#include "../../lib/CCreatureHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpell.h"

#include <algorithm>

using consecratedArmsPrayerChoice::RecipientClick;
using consecratedArmsPrayerChoice::withoutPrayer;

PrayerRecipientTargeting::PrayerRecipientTargeting(BattleInterface & owner_, BattleAction action_,
	consecratedArmsPrayerChoice::Selection selection_, Sender send_)
	: owner(owner_), action(withoutPrayer(std::move(action_))), selection(std::move(selection_)), send(std::move(send_)),
	  eligible(selection.recipients())
{
	if(!owner.getBattle() || !owner.currentHero() || !owner.curInt)
		return;
	battleID = owner.getBattleID();
	side = owner.getBattle()->battleGetMySide();
	round = owner.getBattle()->battleGetRound();
	heroID = owner.currentHero()->id;
	player = owner.curInt->playerID;
}

bool PrayerRecipientTargeting::contextIsCurrent() const
{
	if(decided || CPlayerInterface::battleInt.get() != &owner || !owner.curInt || !owner.curInt->cb
		|| !owner.getBattle() || !owner.currentHero() || !owner.makingTurn() || owner.curInt->isAutoFightOn
		|| owner.isInTacticsMode())
		return false;
	return (side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
		&& owner.getBattleID() == battleID
		&& owner.getBattle()->battleGetMySide() == side
		&& owner.getBattle()->battleGetRound() == round
		&& owner.currentHero()->id == heroID
		&& owner.curInt->playerID == player;
}

consecratedArms::ChoiceRequest PrayerRecipientTargeting::currentRequest() const
{
	if(!contextIsCurrent())
		return {};
	return consecratedArms::choiceRequest(*owner.getBattle(), action);
}

std::string PrayerRecipientTargeting::actionName() const
{
	if(action.actionType == EActionType::HERO_SPELL)
	{
		const auto * spell = action.spell.toSpell();
		return spell ? spell->getNameTranslated() : std::string("the spell");
	}
	return HeroCommandUI::name(action.command);
}

std::string PrayerRecipientTargeting::prayerNames() const
{
	std::string result;
	for(const auto prayer : selection.chosenPrayers())
		result += (result.empty() ? "" : " and ") + std::string(consecratedArms::name(prayer));
	return result;
}

std::string PrayerRecipientTargeting::stackName(uint32_t unitId) const
{
	const auto * stack = owner.getBattle() ? owner.getBattle()->battleGetStackByID(unitId, true) : nullptr;
	const auto * type = stack ? stack->unitType() : nullptr;
	if(!type)
		return "This stack";
	return std::to_string(stack->getCount()) + " "
		+ (stack->getCount() == 1 ? type->getNameSingularTranslated() : type->getNamePluralTranslated());
}

bool PrayerRecipientTargeting::hexIsLegal(const BattleHex & hex) const
{
	if(!hex.isValid() || !owner.getBattle())
		return false;
	const auto * stack = owner.getBattle()->battleGetStackByPos(hex, true);
	return stack && std::ranges::find(eligible, stack->unitId()) != eligible.end();
}

BattleHexArray PrayerRecipientTargeting::legalHexes() const
{
	BattleHexArray result;
	if(!owner.getBattle())
		return result;
	for(const auto id : eligible)
	{
		const auto * stack = owner.getBattle()->battleGetStackByID(id, true);
		if(!stack)
			continue;
		result.insert(stack->getPosition());
		if(stack->doubleWide())
			result.insert(stack->occupiedHex());
	}
	return result;
}

std::string PrayerRecipientTargeting::status(const BattleHex & hoveredHex)
{
	eligible = currentRequest().recipients;
	const bool spell = action.actionType == EActionType::HERO_SPELL;
	const auto * stack = hoveredHex.isValid() && owner.getBattle()
		? owner.getBattle()->battleGetStackByPos(hoveredHex, true) : nullptr;
	if(stack && std::ranges::find(eligible, stack->unitId()) != eligible.end())
		return "Grant " + prayerNames() + " to " + stackName(stack->unitId()) + " with " + actionName() + ".";
	if(eligible.empty())
		return "No stack can receive " + prayerNames() + " now. Click to " + (spell ? "cast " : "issue ")
			+ actionName() + " without a prayer.";
	std::string result = prayerNames() + ": choose the stack that receives "
		+ (selection.requiredPrayers() == 2 ? "them" : "it") + ". Right-click to "
		+ (spell ? "cast " : "issue ") + actionName() + " without a prayer.";
	if(stack)
		result = stackName(stack->unitId()) + " cannot receive it" + (spell ? ". "
			: ": not a recipient of " + actionName() + ". ") + result;
	return result;
}

PrayerRecipientTargeting::Outcome PrayerRecipientTargeting::click(const BattleHex & hex)
{
	if(decided)
		return decided->outcome;
	if(!contextIsCurrent())
	{
		decided = Resolution{Outcome::NOT_SENT, action};
		return decided->outcome;
	}
	const auto * stack = hex.isValid() ? owner.getBattle()->battleGetStackByPos(hex, true) : nullptr;
	const auto current = consecratedArms::choiceRequest(*owner.getBattle(), action);
	eligible = current.recipients;
	auto request = action;
	const auto resolution = consecratedArmsPrayerChoice::resolveRecipientClick(selection,
		stack ? stack->unitId() : BattleAction::NO_PRAYER_RECIPIENT, current, request);
	if(resolution == RecipientClick::REFUSE)
		return Outcome::WAITING;
	if(resolution == RecipientClick::SEND_WITH_PRAYER)
		decided = Resolution{Outcome::SENT_WITH_PRAYER, request};
	else
		decided = Resolution{Outcome::SENT_WITHOUT_PRAYER, action};
	return decided->outcome;
}

PrayerRecipientTargeting::Resolution PrayerRecipientTargeting::conclude()
{
	if(!decided)
		decided = Resolution{contextIsCurrent() ? Outcome::SENT_WITHOUT_PRAYER : Outcome::NOT_SENT, action};
	return *decided;
}

void PrayerRecipientTargeting::deliver(const Resolution & resolution) const
{
	const bool live = CPlayerInterface::battleInt.get() == &owner && owner.curInt;
	if(resolution.outcome == Outcome::SENT_WITH_PRAYER || resolution.outcome == Outcome::SENT_WITHOUT_PRAYER)
	{
		if(send)
			send(resolution.request);
		return;
	}
	if(resolution.outcome == Outcome::NOT_SENT)
	{
		logGlobal->warn("Chosen Prayers: %s was not sent because the battle changed", actionName());
		if(live)
			owner.curInt->showInfoDialog("The battle changed before the prayer's recipient was chosen, so "
				+ actionName() + " was not used. Its Hero Action was not spent.");
	}
}

std::string PrayerRecipientTargeting::cancelText() const
{
	if(action.actionType == EActionType::HERO_SPELL)
		return "Prayer skipped. " + actionName() + " is cast without it.";
	return "Prayer skipped. " + actionName() + " is issued without it.";
}
