/*
 * HypotheticOrderContinuation.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/NewHorizonsBulwark.h"
#include "../../lib/battle/NewHorizonsHeroicSpirit.h"
#include "../../lib/battle/NewHorizonsSwiftRebirth.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

HypotheticBattle::ProjectedOrderContinuation HypotheticBattle::projectOrderContinuation(
	BattleSide side, const ProjectedOrderAllowance & prepared,
	const heroCommands::PreparedOrderEffects & effects)
{
	using Kind = ProjectedOrderContinuation::Kind;
	const auto * hero = battleGetFightingHero(side);
	if(effects.side != side || effects.order.issuedRound != projectedRound || !hero
		|| prepared.action.side != side
		|| prepared.action.epoch + 1 != projectedActionEpochs.at(side)
		|| getHeroOrderState(side, effects.order.command) != std::optional<HeroOrderState>(effects.order))
		return {};
	// Native Order RNG stays authoritative. This is the deterministic mainline,
	// with stochastic outcomes explicitly unvalued, not their expected utility.
	const bool heroicSpiritUnvalued = newHorizonsHeroicSpirit::canRollOrderMorale(hero, prepared.action.receipt);
	const bool randomOutcomesUnvalued = newHorizonsHeroLuck::hasLuckyDay(hero) || heroicSpiritUnvalued;
	auto continuation = getDoubleCommandState(side);
	// The shared spell/Order model values one accepted Hero Action followed by
	// a unit exchange. Future Hero choices are unvalued and remain untouched.
	const bool futureHeroChoices = continuation.orderPending()
		|| prepared.action.receipt.source == HeroActionAllowanceState::GrantSource::BATTLE_PLAN
		|| heroicSpiritUnvalued;
	std::optional<uint32_t> recipient;
	if(continuation.secondWindReady())
		recipient = continuation.deferredSecondWindTargetUnitId;
	else if(effects.order.command == HeroCommand::SECOND_WIND && !continuation.orderPending())
		recipient = effects.order.primaryTargetUnitId;
	if(recipient)
	{
		auto order = getHeroOrderState(side, HeroCommand::SECOND_WIND);
		const auto * target = battleGetUnitByID(*recipient);
		// Reuse the same existing approximate activation as spell/unit exchange.
		// Native obstacle/Rebirth outcomes remain explicit unvalued knowledge.
		if(!order || order->primaryTargetUnitId != *recipient || !target || !target->alive()
			|| target->isGhost()
			|| newHorizonsSwiftRebirth::blocksAdditionalActivation(*target, projectedRound))
			return {};
		const bool activationEffectsUnvalued = !battleGetAllObstacles().empty()
			|| (newHorizonsBulwark::physicalPoisonTickDamage(target->acquireState().get()) > 0 && hasReadyNativeRebirth(target));
		if(continuation.secondWindReady())
		{
			if(continuation.consumeSecondWindContinuation() != recipient)
				return {};
			setDoubleCommandState(side, continuation);
		}
		order->secondWindActive = true;
		setHeroOrderState(side, *order);
		nextTurn(*recipient, BattleUnitTurnReason::HERO_COMMAND);
		// A known poison death is part of this branch, not a refusal of the Order.
		return {Kind::SECOND_WIND_ACTOR, recipient, futureHeroChoices, randomOutcomesUnvalued,
			activationEffectsUnvalued};
	}
	const auto * active = activeUnitId >= 0 ? battleGetUnitByID(static_cast<uint32_t>(activeUnitId)) : nullptr;
	if(!active || !active->alive() || active->isGhost())
		return {};
	return {Kind::CURRENT_ACTOR, active->unitId(), futureHeroChoices, randomOutcomesUnvalued};
}
