/*
 * HypotheticOrderActions.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/HeroOrderEffects.h"
#include "../../lib/battle/OrderActionTransition.h"
#include "../../lib/battle/NewHorizonsDivineMandate.h"
#include "../../lib/battle/NewHorizonsWarcasting.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

bool HypotheticBattle::projectAcceptedHeroOrder(BattleSide side, const ProjectedOrderAllowance & prepared)
{
	return projectAcceptedHeroOrder(side, HeroCommand::NONE, {}, prepared);
}

bool HypotheticBattle::projectAcceptedHeroOrder(BattleSide side, HeroCommand command,
	const std::vector<uint32_t> & commandTargets, const ProjectedOrderAllowance & prepared)
{
	if(command == HeroCommand::NONE)
	{
		if((side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
			&& (begunProjectedOrderEffects.at(side) || begunProjectedLegacyOrders.at(side)))
			return false; // A bound canonical action cannot be downgraded to accounting only.
		return commitProjectedHeroOrder(side, nullptr, prepared);
	}
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return false;
	if(!heroCommands::isCanonicalRules(getHeroCommandRules()))
	{
		const auto & legacy = begunProjectedLegacyOrders.at(side);
		return legacy && legacy->owner == this && legacy->command == command
			&& legacy->targets == commandTargets
			&& commitProjectedHeroOrder(side, nullptr, prepared, command);
	}
	const auto & bound = begunProjectedOrderEffects.at(side);
	return bound && bound->owner == this && bound->effects.order.command == command
		&& bound->targets == commandTargets && projectAcceptedHeroOrder(side, bound->effects, prepared);
}

std::optional<heroCommands::PreparedOrderEffects> HypotheticBattle::beginProjectedHeroOrder(
	BattleSide side, HeroCommand command, const std::vector<uint32_t> & targets,
	const ProjectedOrderAllowance & prepared)
{
	if(!isCurrentPreparedOrderAction(side, prepared, false))
		return {};
	const auto effects = heroCommands::prepareOrderEffects(*this, side, command, targets);
	if(!effects)
		return {};
	// Canonical preparation validates both shapes before action-boundary cleanup.
	// Bind an owned copy, not the openly mutable value returned to the caller.
	auto bound = BoundProjectedOrderEffects{this, *effects, targets};
	if(!beginProjectedHeroAction(side, prepared))
		return {};
	begunProjectedOrderEffects.at(side) = std::move(bound);
	return effects;
}

bool HypotheticBattle::projectAcceptedHeroOrder(BattleSide side,
	const heroCommands::PreparedOrderEffects & effects, const ProjectedOrderAllowance & prepared)
{
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| !isCurrentPreparedOrderAction(side, prepared, true))
		return false;
	const auto & bound = begunProjectedOrderEffects.at(side);
	if(!bound || bound->owner != this || bound->effects != effects)
		return false;
	return commitProjectedHeroOrder(side, &effects, prepared);
}

bool HypotheticBattle::commitProjectedHeroOrder(BattleSide side,
	const heroCommands::PreparedOrderEffects * effects, const ProjectedOrderAllowance & prepared,
	HeroCommand legacyCommand)
{
	const auto command = effects ? effects->order.command : legacyCommand;
	const auto & action = prepared.action;
	const bool crisisOrder = crisisCommand.choice();
	if(crisisOrder && (crisisCommand.chooser() != side
		|| action.receipt.source != HeroActionAllowanceState::GrantSource::CRISIS_COMMAND
		|| action.receipt.grantId != crisisCommand.returns.back().grant)) return false;
	if(action.receipt.action != HeroActionAllowanceState::ActionKind::ORDER
		|| !isCurrentPreparedOrderAction(side, prepared, true))
		return false;
	const auto mandateStatus = battleGetDivineMandateStatus(side);
	const bool projectingCommand = command != HeroCommand::NONE;
	if(projectingCommand && !heroCommands::isActive(command))
		return false;
	auto & doubleCommand = doubleCommandStates.at(side);
	auto & preCombatOrder = preCombatOrderStates.at(side);
	const bool resolvingDoubleCommand = doubleCommand.orderPending();
	const bool resolvingPreCombatOrder = preCombatOrder.orderPending();
	if(resolvingDoubleCommand && resolvingPreCombatOrder)
		return false;
	if(resolvingPreCombatOrder)
	{
		const auto * active = activeUnitId >= 0
			? battleGetUnitByID(static_cast<uint32_t>(activeUnitId)) : nullptr;
		if(!projectingCommand || !prepared.action.typedLedger
			|| action.receipt.source != HeroActionAllowanceState::GrantSource::BATTLE_PLAN
			|| !active || !active->alive() || active->isGhost() || active->isTurret()
			|| active->hasBonusOfType(BonusType::SIEGE_WEAPON)
			|| active->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER
			|| active->unitSlot() == SlotID::WAR_MACHINES_SLOT
			|| preCombatOrder.issuedRound != projectedRound || projectedRound != 1
			|| preCombatOrder.anchorStackId != active->unitId()
			|| battleGetOwner(active) != sideToPlayer(side))
			return false;
		auto completed = preCombatOrder;
		completed.complete();
		completed.validateTransitionFrom(preCombatOrder);
	}
	else if(action.receipt.source == HeroActionAllowanceState::GrantSource::BATTLE_PLAN)
		return false;
	if(resolvingDoubleCommand)
	{
		const auto * active = activeUnitId >= 0
			? battleGetUnitByID(static_cast<uint32_t>(activeUnitId)) : nullptr;
		if(!projectingCommand || !prepared.action.typedLedger
			|| action.receipt.source != HeroActionAllowanceState::GrantSource::DOUBLE_COMMAND
			|| !active || !active->alive() || active->isGhost()
			|| doubleCommand.issuedRound != projectedRound
			|| doubleCommand.anchorStackId != active->unitId()
			|| battleGetOwner(active) != sideToPlayer(side)
			|| command == doubleCommand.firstOrder)
			return false;
	}
	else if(projectingCommand && action.receipt.source == HeroActionAllowanceState::GrantSource::DOUBLE_COMMAND)
		return false;
	// Legacy effects historically follow begin cleanup and precede accounting.
	if(legacyCommand != HeroCommand::NONE)
		applyProjectedLegacyHeroOrder(side);
	const auto * enchantedOrder = effects ? &effects->order : nullptr;
	std::vector<uint32_t> sharedPurposeOverlap;
	if(action.typedLedger)
	{
		auto nextLedger = prepared.allowancesAfter;
		const auto recipients = enchantedOrder
			&& newHorizonsDivineMandate::needsRecipientCapture(getSideHero(side))
			? newHorizonsDivineMandate::sharedPurposeOrderRecipients(*this, side, *enchantedOrder)
			: std::vector<uint32_t>{};
		sharedPurposeOverlap = DivineMandateTransition::applyAcceptedAction(nextLedger, action.receipt,
			projectedRound, false, mandateStatus.maximumPairs, recipients);
		heroActionAllowances.at(side) = std::move(nextLedger);
	}
	if(projectingCommand && action.isHeroAction())
		setSeizeInitiativeState(newHorizonsSeizeInitiative::capturePaidOrder(*this, side));
	if(action.isHeroAction() && newHorizonsWarcasting::enabled(getMagicRules()))
	{
		const auto * hero = getSideHero(side);
		warcastingStates.at(side).recordAcceptedAction(AlternatingHeroActionState::Action::ORDER,
			projectedRound, newHorizonsWarcasting::empowerment(hero, AlternatingHeroActionState::Action::ORDER),
			newHorizonsWarcasting::readinessLifetimeRounds(hero));
	}
	if(effects)
	{
		setHeroOrderState(side, effects->order);
		if(effects->focusFire)
			setFocusFireState(side, *effects->focusFire);
	}
	newHorizonsDivineMandate::applySharedPurpose(*this, *this, side, sharedPurposeOverlap);
	if(effects)
		heroCommands::applyIssuedOrderGrants(*this, heroCommands::prepareIssuedOrderGrants(*this, side, effects->order));
	if(resolvingPreCombatOrder)
		preCombatOrder.complete();
	else if(resolvingDoubleCommand)
		doubleCommand = orderActionTransition::completeDoubleCommand(doubleCommand, command);
	else if(projectingCommand && action.typedLedger && action.isHeroAction())
	{
		const auto * hero = battleGetFightingHero(side);
		const auto * active = activeUnitId >= 0
			? battleGetUnitByID(static_cast<uint32_t>(activeUnitId)) : nullptr;
		const auto anchor = active && active->alive() && !active->isGhost()
			&& battleGetOwner(active) == sideToPlayer(side)
			? active->unitId() : DoubleCommandState::INVALID_UNIT_ID;
		const uint32_t deferredTarget = command == HeroCommand::SECOND_WIND && effects
			? effects->order.primaryTargetUnitId : DoubleCommandState::INVALID_UNIT_ID;
		const auto nextDoubleCommand = orderActionTransition::preparePrimaryDoubleCommand(
			doubleCommand, action.receipt, heroCommands::hasDoubleCommand(hero), command, projectedRound, anchor, deferredTarget);
		if(nextDoubleCommand)
		{
			doubleCommand = *nextDoubleCommand;
			auto & allowances = heroActionAllowances.at(side);
			allowances.grantAllowance(HeroActionAllowanceState::AllowanceKind::ORDER,
				HeroActionAllowanceState::GrantSource::DOUBLE_COMMAND, projectedRound);

			// Match the authoritative no-alternative path: do not leave an orphaned
			// grant or a pending phase when the primary Order exhausted every other
			// legal Order for this battle state.
			const bool hasDistinctAlternative = std::any_of(heroCommands::CANONICAL_COMMANDS.begin(),
				heroCommands::CANONICAL_COMMANDS.end(), [this, side, command](HeroCommand candidate)
				{
					return candidate != command && battleCanBeginHeroCommand(side, candidate);
				});
			if(!hasDistinctAlternative)
			{
				orderActionTransition::exhaustDoubleCommand(doubleCommand, allowances, projectedRound);
			}
		}
	}
	finishProjectedHeroAction(side, prepared);
	if(crisisOrder)
	{
		if(command == HeroCommand::SECOND_WIND)
			crisisCommand.returns.back().phase = newHorizonsCrisisCommand::Phase::GRANTED_EXTRA;
		else
		{
			auto returned = crisisCommand;
			returned.returns.pop_back();
			setCrisisCommandState(returned);
		}
	}
	return true;
}
