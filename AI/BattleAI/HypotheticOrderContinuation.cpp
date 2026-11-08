/*
 * HypotheticOrderContinuation.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/NewHorizonsBulwark.h"
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
	// Only the ordinary accepted Order remains. Independent Lucky Day outcomes
	// retain their established conservative forecast; Heroic Spirit is deterministic.
	const bool randomOutcomesUnvalued = newHorizonsHeroLuck::hasLuckyDay(hero);
	const bool futureHeroChoices = false;
	std::optional<uint32_t> recipient;
	if(effects.order.command == HeroCommand::SECOND_WIND)
		recipient = effects.order.primaryTargetUnitId;
	if(recipient)
	{
		auto order = getHeroOrderState(side, HeroCommand::SECOND_WIND);
		const auto * target = battleGetUnitByID(*recipient);
		// Reuse the same existing approximate activation as spell/unit exchange.
		// Native obstacle/Rebirth outcomes remain explicit unvalued knowledge.
		if(!order || order->primaryTargetUnitId != *recipient || !target || !target->alive()
			|| target->isGhost())
			return {};
		const bool activationEffectsUnvalued = !battleGetAllObstacles().empty()
			|| (newHorizonsBulwark::physicalPoisonTickDamage(target->acquireState().get()) > 0 && hasReadyNativeRebirth(target));
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
