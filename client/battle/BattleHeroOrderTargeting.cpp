/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleActionsController.h"
#include "BattleInterface.h"
#include "ConsecratedArmsPrayerFlow.h"
#include "ConsecratedArmsPrayerTargeting.h"

#include "../CPlayerInterface.h"
#include "../GameEngine.h"
#include "../gui/CursorHandler.h"
#include "../gui/WindowHandler.h"
#include "../gui/CIntObject.h"

#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/texts/CGeneralTextHandler.h"

// Battlefield selection for targeted Orders and for the optional prayer
// recipient of a committed Chosen Prayers Order (BattleActionsController routes here).
namespace
{
const char * heroOrderTargetName(HeroCommand command)
{
	switch(command)
	{
	case HeroCommand::FOCUS_FIRE: return "Focus Fire";
	case HeroCommand::PROTECT: return "Protect";
	case HeroCommand::FLANK: return "Flank";
	case HeroCommand::SECOND_WIND: return "Second Wind";
	default: return "Order";
	}
}

bool isHeroOrderPair(HeroCommand command)
{
	return command == HeroCommand::PROTECT;
}
}

bool BattleActionsController::heroOrderTargetingContextIsCurrent() const
{
	if(!selectedHeroOrderCommand || !owner.curInt || !owner.curInt->cb || !owner.getBattle()
		|| !owner.currentHero() || !owner.makingTurn() || owner.curInt->isAutoFightOn
		|| owner.isInTacticsMode() || heroSpellcastingModeActive())
		return false;

	const auto side = owner.getBattle()->battleGetMySide();
	return (side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
		&& owner.getBattleID() == heroOrderTargetingBattleID
		&& side == heroOrderTargetingSide
		&& owner.getBattle()->battleGetRound() == heroOrderTargetingRound
		&& owner.currentHero()->id == heroOrderTargetingHeroID
		&& owner.curInt->playerID == heroOrderTargetingPlayer;
}

std::vector<uint32_t> BattleActionsController::heroOrderTargetIds() const
{
	if(!heroOrderTargetingContextIsCurrent())
		return {};

	const auto side = owner.getBattle()->battleGetMySide();
	const auto command = *selectedHeroOrderCommand;
	const auto candidates = owner.getBattle()->battleGetHeroCommandTargets(side, command);
	if(!isHeroOrderPair(command))
		return candidates;

	std::vector<uint32_t> result;
	if(!heroOrderTargetingFirst)
	{
		for(const auto firstId : candidates)
		{
			const auto hasWard = std::ranges::any_of(candidates, [this, side, command, firstId](const auto secondId)
			{
				return firstId != secondId && owner.getBattle()->battlePrepareHeroOrderState(
					side, command, {firstId, secondId}).has_value();
			});
			if(hasWard)
				result.push_back(firstId);
		}
		return result;
	}

	for(const auto id : candidates)
	{
		if(id != *heroOrderTargetingFirst && heroOrderTargetIdIsLegal(id))
			result.push_back(id);
	}
	return result;
}

bool BattleActionsController::heroOrderTargetIdIsLegal(uint32_t unitId) const
{
	if(!heroOrderTargetingContextIsCurrent())
		return false;

	const auto side = owner.getBattle()->battleGetMySide();
	const auto command = *selectedHeroOrderCommand;
	const auto * target = owner.getBattle()->battleGetUnitByID(unitId);
	if(!target || !target->alive() || target->isGhost())
		return false;

	if(isHeroOrderPair(command))
	{
		if(!heroOrderTargetingFirst || *heroOrderTargetingFirst == unitId)
		{
			const auto candidates = heroOrderTargetIds();
			return !heroOrderTargetingFirst && std::ranges::find(candidates, unitId) != candidates.end();
		}
		return owner.getBattle()->battlePrepareHeroOrderState(side, command,
			{*heroOrderTargetingFirst, unitId}).has_value();
	}

	if(command == HeroCommand::FOCUS_FIRE
		&& !heroCommands::isCanonicalRules(owner.getBattle()->getBattle()->getHeroCommandRules()))
		return owner.getBattle()->battlePrepareFocusFireState(side, unitId).has_value();

	return owner.getBattle()->battlePrepareHeroOrderState(side, command, {unitId}).has_value();
}

bool BattleActionsController::heroOrderTargetingHexIsLegal(const BattleHex & hex) const
{
	if(prayerRecipientTargeting)
		return prayerRecipientTargeting->hexIsLegal(hex);
	if(!hex.isValid())
		return false;
	const auto * stack = owner.getBattle()->battleGetStackByPos(hex, true);
	const auto targetIds = heroOrderTargetIds();
	return stack && std::ranges::find(targetIds, stack->unitId()) != targetIds.end();
}

BattleHexArray BattleActionsController::getHeroOrderTargetingLegalHexes() const
{
	if(prayerRecipientTargeting)
		return prayerRecipientTargeting->legalHexes();
	BattleHexArray result;
	for(const auto id : heroOrderTargetIds())
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

BattleHexArray BattleActionsController::getHeroOrderTargetingSelectedHexes() const
{
	// Prayer recipients stand out from the active stack's movement shading.
	if(prayerRecipientTargeting)
		return prayerRecipientTargeting->legalHexes();
	BattleHexArray result;
	if(!heroOrderTargetingFirst || !heroOrderTargetingContextIsCurrent())
		return result;

	const auto * stack = owner.getBattle()->battleGetStackByID(*heroOrderTargetingFirst, true);
	if(!stack)
		return result;
	result.insert(stack->getPosition());
	if(stack->doubleWide())
		result.insert(stack->occupiedHex());
	return result;
}

std::string BattleActionsController::heroOrderTargetRejectionText(heroCommands::TargetRejection reason) const
{
	using Reason = heroCommands::TargetRejection;
	const char * key = "generic";
	switch(reason)
	{
		case Reason::UNAVAILABLE: key = "unavailable"; break;
		case Reason::TARGET_COUNT: key = "targetCount"; break;
		case Reason::NO_STACK: key = "noStack"; break;
		case Reason::NOT_LIVING: key = "notLiving"; break;
		case Reason::TARGET_UNAVAILABLE: key = "targetUnavailable"; break;
		case Reason::FRIENDLY_REQUIRED: key = "friendlyRequired"; break;
		case Reason::ENEMY_REQUIRED: key = "enemyRequired"; break;
		case Reason::ORDINARY_REQUIRED: key = "ordinaryRequired"; break;
		case Reason::INVULNERABLE: key = "invulnerable"; break;
		case Reason::NO_FOCUS_RECIPIENT: key = "noFocusRecipient"; break;
		case Reason::NO_MELEE_RECIPIENT: key = "noMeleeRecipient"; break;
		case Reason::SAME_STACK: key = "sameStack"; break;
		case Reason::NOT_ADJACENT: key = "notAdjacent"; break;
		case Reason::NO_ADJACENT_WARD: key = "noAdjacentWard"; break;
		case Reason::ACTIVATION_UNSPENT: key = "activationUnspent"; break;
		case Reason::NO_RECIPIENT: key = "noRecipient"; break;
		case Reason::NONE: break;
	}
	return LIBRARY->generaltexth->translate(std::string("new-horizons.combat.orders.targetRejection.") + key);
}

void BattleActionsController::updateHeroOrderTargetingStatus(const BattleHex & hoveredHex)
{
	if(prayerRecipientTargeting)
	{
		if(!prayerRecipientTargeting->contextIsCurrent())
		{
			cancelHeroOrderTargeting();
			return;
		}
		auto prayerMessage = prayerRecipientTargeting->status(hoveredHex);
		if(!currentConsoleMsg.empty())
			ENGINE->statusbar()->clearIfMatching(currentConsoleMsg);
		ENGINE->statusbar()->write(prayerMessage);
		currentConsoleMsg = std::move(prayerMessage);
		return;
	}
	if(!selectedHeroOrderCommand)
		return;

	if(!heroOrderTargetingContextIsCurrent())
	{
		cancelHeroOrderTargeting();
		return;
	}

	std::string message = std::string(heroOrderTargetName(*selectedHeroOrderCommand)) + ": ";
	if(isHeroOrderPair(*selectedHeroOrderCommand) && heroOrderTargetingFirst)
		message += "select an adjacent Ward on the battlefield.";
	else
		message += "select a legal stack on the battlefield.";

	if(hoveredHex.isValid())
	{
		const auto * stack = owner.getBattle()->battleGetStackByPos(hoveredHex, true);
		if(stack && heroOrderTargetIdIsLegal(stack->unitId()))
			message += " Click to confirm.";
		else
		{
			auto reason = heroCommands::TargetRejection::NO_STACK;
			if(stack)
			{
				std::vector<uint32_t> targets;
				if(heroOrderTargetingFirst)
					targets.push_back(*heroOrderTargetingFirst);
				targets.push_back(stack->unitId());
				reason = owner.getBattle()->battleGetHeroOrderTargetRejection(heroOrderTargetingSide,
					*selectedHeroOrderCommand, targets,
					isHeroOrderPair(*selectedHeroOrderCommand) && !heroOrderTargetingFirst);
			}
			message += " " + heroOrderTargetRejectionText(reason);
		}
	}

	if(!currentConsoleMsg.empty())
		ENGINE->statusbar()->clearIfMatching(currentConsoleMsg);
	ENGINE->statusbar()->write(message);
	currentConsoleMsg = std::move(message);
}

bool BattleActionsController::beginHeroOrderTargeting(HeroCommand command)
{
	invalidateChainLightningPreview();
	cancelHeroOrderTargeting();
	if(command != HeroCommand::FOCUS_FIRE && command != HeroCommand::PROTECT
		&& command != HeroCommand::FLANK && command != HeroCommand::SECOND_WIND)
		return false;

	if(!owner.curInt || !owner.curInt->cb || !owner.getBattle() || !owner.currentHero())
		return false;
	selectedHeroOrderCommand = command;
	heroOrderTargetingBattleID = owner.getBattleID();
	heroOrderTargetingSide = owner.getBattle()->battleGetMySide();
	heroOrderTargetingRound = owner.getBattle()->battleGetRound();
	heroOrderTargetingHeroID = owner.currentHero()->id;
	heroOrderTargetingPlayer = owner.curInt->playerID;
	if(!heroOrderTargetingContextIsCurrent()
		|| !owner.getBattle()->battleCanBeginHeroCommand(owner.getBattle()->battleGetMySide(), command))
	{
		cancelHeroOrderTargeting();
		return false;
	}

	ENGINE->fakeMouseMove();
	updateHeroOrderTargetingStatus(BattleHex::INVALID);
	ENGINE->windows().totalRedraw();
	return true;
}

bool BattleActionsController::beginPrayerRecipientTargeting(std::shared_ptr<PrayerRecipientTargeting> targeting)
{
	invalidateChainLightningPreview();
	cancelHeroOrderTargeting();
	if(!targeting || !targeting->contextIsCurrent())
		return false;
	prayerRecipientTargeting = std::move(targeting);
	ENGINE->fakeMouseMove();
	updateHeroOrderTargetingStatus(BattleHex::INVALID);
	ENGINE->windows().totalRedraw();
	return true;
}

bool BattleActionsController::heroOrderTargetingModeActive() const
{
	return selectedHeroOrderCommand.has_value() || prayerRecipientTargeting != nullptr;
}

std::optional<newHorizonsProtectLink::Link> BattleActionsController::getProposedProtectLink(const BattleHex & hoveredHex) const
{
	if(!heroOrderTargetingContextIsCurrent() || *selectedHeroOrderCommand != HeroCommand::PROTECT
		|| !heroOrderTargetingFirst || !hoveredHex.isValid())
		return std::nullopt;
	const auto * ward = owner.getBattle()->battleGetStackByPos(hoveredHex, true);
	if(!ward)
		return std::nullopt;
	return newHorizonsProtectLink::proposedLink(*owner.getBattle(), heroOrderTargetingSide,
		*heroOrderTargetingFirst, ward->unitId());
}

void BattleActionsController::cancelHeroOrderTargeting()
{
	heroOrderTargetingSide = BattleSide::NONE;
	heroOrderTargetingRound = -1;
	heroOrderTargetingHeroID = ObjectInstanceID::NONE;
	heroOrderTargetingPlayer.reset();
	// Leaving the prayer phase never cancels its Order: the plain action is sent.
	const auto prayer = std::exchange(prayerRecipientTargeting, nullptr);
	if(!selectedHeroOrderCommand && !prayer)
		return;
	selectedHeroOrderCommand.reset();
	heroOrderTargetingFirst.reset();
	if(!currentConsoleMsg.empty())
		ENGINE->statusbar()->clearIfMatching(currentConsoleMsg);
	currentConsoleMsg.clear();
	ENGINE->cursor().set(Cursor::Combat::POINTER);
	ENGINE->windows().totalRedraw();
	// Cleared first, so a sender that re-enters this controller finds no selector.
	if(prayer)
		prayer->deliver(prayer->conclude());
}

void BattleActionsController::selectHeroOrderTarget(const BattleHex & clickedHex)
{
	if(prayerRecipientTargeting)
	{
		if(prayerRecipientTargeting->click(clickedHex) == PrayerRecipientTargeting::Outcome::WAITING)
			updateHeroOrderTargetingStatus(clickedHex);
		else
			cancelHeroOrderTargeting(); // Delivers the decided click.
		return;
	}
	if(!heroOrderTargetingModeActive())
		return;

	if(!heroOrderTargetingContextIsCurrent())
	{
		cancelHeroOrderTargeting();
		return;
	}

	const auto * target = owner.getBattle()->battleGetStackByPos(clickedHex, true);
	if(!target || !heroOrderTargetIdIsLegal(target->unitId()))
	{
		updateHeroOrderTargetingStatus(clickedHex);
		return;
	}

	if(isHeroOrderPair(*selectedHeroOrderCommand) && !heroOrderTargetingFirst)
	{
		heroOrderTargetingFirst = target->unitId();
		updateHeroOrderTargetingStatus(clickedHex);
		ENGINE->windows().totalRedraw();
		return;
	}

	const auto side = owner.getBattle()->battleGetMySide();
	const auto command = *selectedHeroOrderCommand;
	const auto first = heroOrderTargetingFirst;
	const auto prepared = command == HeroCommand::FOCUS_FIRE
		&& !heroCommands::isCanonicalRules(owner.getBattle()->getBattle()->getHeroCommandRules())
		? owner.getBattle()->battlePrepareFocusFireState(side, target->unitId()).has_value()
		: owner.getBattle()->battlePrepareHeroOrderState(side, command,
			isHeroOrderPair(command) ? std::vector<uint32_t>{*first, target->unitId()}
				: std::vector<uint32_t>{target->unitId()}).has_value();
	if(!prepared || (isHeroOrderPair(command) && !first))
	{
		updateHeroOrderTargetingStatus(clickedHex);
		return;
	}

	const auto action = isHeroOrderPair(command)
		? BattleAction::makePairedHeroCommand(side, command, *first, target->unitId())
		: BattleAction::makeTargetedHeroCommand(side, command, target->unitId());
	cancelHeroOrderTargeting();
	consecratedArmsPrayerUI::submitOrder(CPlayerInterface::battleInt, action);
}
