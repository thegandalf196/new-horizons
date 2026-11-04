/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleActionsController.h"
#include "BattleInterface.h"
#include "BattleStacksController.h"
#include "../GameEngine.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/PossiblePlayerBattleAction.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/NewHorizonsDemonicGating.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

std::string BattleActionsController::demonicGateStatus(const BattleHex & targetHex) const
{
	const auto * source = owner.stacksController->getActiveStack();
	if(!source)
		return "Open Gate";
	const auto * hero = source ? owner.getBattle()->battleGetFightingHero(source->unitSide()) : nullptr;
	if(newHorizonsDemonicGating::usesMobileGate(hero)
		&& !demonicGatingMovement.isValid())
		return "Move up to half Speed, then place the Gate.";
	if(newHorizonsDemonicGating::usesTemporaryGating(hero))
	{
		const auto count = newHorizonsDemonicGating::summonedCount(*hero, source->getCount());
		std::string description = "Gate: " + std::to_string(count) + " "
			+ (count == 1 ? source->unitType()->getNameSingularTranslated()
				: source->unitType()->getNamePluralTranslated());
		if(const auto preview = newHorizonsDemonicGating::plan(*owner.getBattle(),
			prepareDemonicGateAction(*source, targetHex)))
		{
			description += preview->atEndOfRound ? "; arrives at round end" : "; arrives next round";
			if(preview->freeActivation)
				description += "; activation retained";
		}
		return description;
	}
	const auto & reserve = owner.getBattle()->getBattle()->getDemonicReserve(
		source->unitSide());
	const auto found = reserve.find(demonicGatingCreature);
	if(found == reserve.end())
		return "Open Gate";
	return "Open Gate for " + std::to_string(found->second) + " "
		+ (found->second == 1 ? demonicGatingCreature.toCreature()->getNameSingularTranslated()
			: demonicGatingCreature.toCreature()->getNamePluralTranslated());
}

std::string BattleActionsController::demonicGateBlockedStatus() const
{
	const auto * source = owner.stacksController->getActiveStack();
	const auto * hero = source ? owner.getBattle()->battleGetFightingHero(source->unitSide()) : nullptr;
	if(newHorizonsDemonicGating::usesMobileGate(hero)
		&& !demonicGatingMovement.isValid())
		return "Choose a reachable movement destination within half Speed.";
	return demonicGatingMovement.isValid()
		? "A Gate must be opened on an empty hex within range of the selected destination."
		: "A Gate must be opened on an empty hex within range.";
}

bool BattleActionsController::demonicGateIsLegal(const BattleHex & targetHex) const
{
	const auto * source = owner.stacksController->getActiveStack();
	if(!source || !demonicGatingCreature.hasValue())
		return false;
	const auto * creature = demonicGatingCreature.toCreature();
	if(!creature)
		return false;
	const auto * hero = owner.getBattle()->battleGetFightingHero(source->unitSide());
	const bool mobileGate = newHorizonsDemonicGating::usesMobileGate(hero);
	const bool temporary = newHorizonsDemonicGating::usesTemporaryGating(hero);
	if(temporary && !newHorizonsDemonicGating::eligibleOriginalStack(*owner.getBattle(), *source, source->unitSide()))
		return false;
	if(mobileGate && !demonicGatingMovement.isValid())
	{
		if(targetHex == source->getPosition())
			return true;
		const auto movement = owner.getBattle()->toWhichHexMove(source, targetHex);
		if(!movement.isValid())
			return false;
		const auto [path, distance] = owner.getBattle()->getPath(source->getPosition(), movement, source);
		return !path.empty() && distance >= 0
			&& distance <= static_cast<int>(source->getMovementRange(0) / 2);
	}
	if(temporary)
		return newHorizonsDemonicGating::plan(*owner.getBattle(),
			prepareDemonicGateAction(*source, targetHex)).has_value();
	const int placementRange = hero && hero->hasActivePerk(
		"new-horizons:demonicGating", "new-horizons:demonicGating.wideGate") ? 5 : 3;
	if(!source || !creature)
		return false;
	const BattleHex sourcePosition = demonicGatingMovement.isValid()
		? demonicGatingMovement : source->getPosition();
	const BattleHex occupiedTail = source->doubleWide()
		? source->occupiedHex(sourcePosition) : BattleHex::INVALID;
	const BattleHex gatedTail = battle::Unit::occupiedHex(
		targetHex, creature->isDoubleWide(), source->unitSide());
	if(!targetHex.isAvailable()
		|| targetHex == sourcePosition || targetHex == occupiedTail
		|| (gatedTail.isValid() && (gatedTail == sourcePosition || gatedTail == occupiedTail))
		|| BattleHex::getDistance(sourcePosition, targetHex) > placementRange
		|| owner.getBattle()->battleGetUnitByPos(targetHex, true)
		|| !owner.getBattle()->battleGetAllObstaclesOnPos(targetHex, false).empty())
		return false;
	const auto & reserve = owner.getBattle()->getBattle()->getDemonicReserve(source->unitSide());
	const auto found = reserve.find(demonicGatingCreature);
	return found != reserve.end() && found->second > 0
		&& owner.getBattle()->getAccessibility().accessible(targetHex, creature->isDoubleWide(), source->unitSide());
}

void BattleActionsController::demonicGateClicked(const BattleHex & targetHex)
{
	const auto * active = owner.stacksController->getActiveStack();
	if(!active || !demonicGatingCreature.hasValue() || !demonicGatingCreature.toCreature())
		return;
	const auto * hero = owner.getBattle()->battleGetFightingHero(active->unitSide());
	const bool mobileGate = newHorizonsDemonicGating::usesMobileGate(hero);
	if(mobileGate && !demonicGatingMovement.isValid())
	{
		demonicGatingMovement = targetHex == active->getPosition()
			? active->getPosition() : owner.getBattle()->toWhichHexMove(active, targetHex);
		ENGINE->fakeMouseMove();
		return;
	}
	const auto command = prepareDemonicGateAction(*active, targetHex);
	owner.sendCommand(command, active);
	return;
}

void BattleActionsController::selectDemonicGatingCreature(CreatureID creature)
{
	if(!creature.hasValue() || !creature.toCreature())
		return;

	invalidateChainLightningPreview();
	demonicGatingCreature = creature;
	demonicGatingMovement = BattleHex::INVALID;
	possibleActions = {PossiblePlayerBattleAction::DEMONIC_GATE};
	ENGINE->fakeMouseMove();
}

BattleAction BattleActionsController::prepareDemonicGateAction(const CStack & source, const BattleHex & targetHex) const
{
	BattleAction command;
	command.actionType = EActionType::DEMONIC_GATING;
	command.side = source.unitSide();
	command.stackNumber = source.unitId();
	command.gatingCreature = demonicGatingCreature;
	if(demonicGatingMovement.isValid() && demonicGatingMovement != source.getPosition())
		command.aimToHex(demonicGatingMovement);
	command.aimToHex(targetHex);
	return command;
}
