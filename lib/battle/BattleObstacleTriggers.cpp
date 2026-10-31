/* Part of VCMI. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "CBattleInfoCallback.h"
#include "CObstacleInstance.h"
#include "Unit.h"
#include "IBattleState.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../spells/ObstacleCasterProxy.h"
#include "../spells/NewHorizonsMagic.h"
#include "../spells/NewHorizonsFireWall.h"
#include "../spells/ISpellMechanics.h"
#include "../spells/Problem.h"
#include "../spells/CSpell.h"
#include "../networkPacks/PacksForClientBattle.h"

bool CBattleInfoCallback::handleObstacleTriggersForUnit(SpellCastEnvironment & spellEnv, const battle::Unit & unit, const BattleHexArray & passed) const
{
	return handleObstacleTriggersForUnitWithObstacles(spellEnv, &spellEnv, unit, getAllAffectedObstaclesByStack(&unit, passed));
}

bool CBattleInfoCallback::handleObstacleTriggersForUnitAtPositions(SpellCastEnvironment & spellEnv,
	const battle::Unit & unit, const BattleHexArray & positions, const BattleHexArray & passed) const
{
	// Crossed footprints are transit positions, not committed landings. The
	// normal flying movement path calls the grounded helper only at its end.
	return handleObstacleTriggersForUnitWithObstacles(spellEnv, &spellEnv, unit,
		getAffectedObstaclesAtPositions(&unit, positions, passed), unit.hasBonusOfType(BonusType::FLYING));
}

bool CBattleInfoCallback::handleObstacleTriggersForUnitWithObstacles(ServerCallback & server, SpellCastEnvironment * nativeEnvironment,
	const battle::Unit & unit, const std::vector<std::shared_ptr<const CObstacleInstance>> & obstacles, bool airborneTransit) const
{
	if(!unit.alive() || unit.isTimeStopped())
		return false;
	bool movementStopped = false;
	for(const auto & obstacle : obstacles)
	{
		//helper info
		const SpellCreatedObstacle * spellObstacle = dynamic_cast<const SpellCreatedObstacle *>(obstacle.get());

		if(spellObstacle)
		{
			// Canonical NH Land Mine is a hidden, hostile-ground-only trap.  Do
			// this gate before visibility/reveal/removal so allied or airborne
			// units neither consume the mine nor leak its position.
			const bool canonicalLandMine = newHorizonsMagic::rulesActive(getBattle()->getMagicRules())
				&& newHorizonsMagic::isLandMine(SpellID(spellObstacle->ID));
			const bool canonicalFireWall = newHorizonsMagic::rulesActive(getBattle()->getMagicRules())
				&& newHorizonsMagic::isFireWall(SpellID(spellObstacle->ID));
			const bool alreadyTriggeredThisActivation = canonicalFireWall
				&& spellObstacle->lastTriggerUnit == static_cast<si32>(unit.unitId())
				&& spellObstacle->lastTriggerActivation == getBattle()->getActivationSerial();
			if(canonicalLandMine
				&& (unit.hasBonusOfType(BonusType::FLYING)
					|| battleGetOwner(&unit) == getBattle()->getSidePlayer(spellObstacle->casterSide)))
				continue;
			const bool flyingImmune = unit.hasBonusOfType(BonusType::FLYING)
				&& (airborneTransit || !newHorizonsFireWall::groundedFlyersBurn(getBattle()->getMagicRules()));
			if(canonicalFireWall && (flyingImmune || alreadyTriggeredThisActivation))
				continue;

			auto revealObstacles = [&](const SpellCreatedObstacle & spellObstacle) -> void
			{
				// For the hidden spell created obstacles, e.g. QuickSand, it should be revealed after taking damage
				auto operation = ObstacleChanges::EOperation::UPDATE;
				if (spellObstacle.removeOnTrigger)
					operation = ObstacleChanges::EOperation::REMOVE;

				SpellCreatedObstacle changedObstacle = spellObstacle;
				changedObstacle.uniqueID = spellObstacle.uniqueID;
				changedObstacle.revealed = true;
				if(canonicalFireWall)
				{
					changedObstacle.lastTriggerUnit = static_cast<si32>(unit.unitId());
					changedObstacle.lastTriggerActivation = getBattle()->getActivationSerial();
				}

				BattleObstaclesChanged bocp;
				bocp.battleID = getBattle()->getBattleID();
				bocp.change = ObstacleChanges(spellObstacle.uniqueID, operation);
				changedObstacle.toInfo(bocp.change, operation);
				server.apply(bocp);
			};
			const auto side = unit.unitSide();
			auto shouldReveal = !spellObstacle->hidden || !battleIsObstacleVisibleForSide(*obstacle, side);
			// A neutral obstacle (casterSide == NONE) belongs to no side and must impede units of both sides;
			// treat it as hostile to whichever unit triggered it so its effect still applies (and to avoid an invalid side lookup)
			const bool neutralObstacle = spellObstacle->casterSide != BattleSide::ATTACKER && spellObstacle->casterSide != BattleSide::DEFENDER;
			const auto casterSide = neutralObstacle ? otherSide(side) : spellObstacle->casterSide;
			const auto * hero = neutralObstacle ? nullptr : battleGetFightingHero(casterSide);
			auto caster = spells::ObstacleCasterProxy(getBattle()->getSidePlayer(casterSide), hero, *spellObstacle);

			if(obstacle->triggersEffects() && obstacle->getTrigger().hasValue())
			{
				const auto * sp = obstacle->getTrigger().toSpell();
				auto cast = spells::BattleCast(this, &caster, spells::Mode::PASSIVE, sp);
				if(canonicalFireWall)
					cast.setForceNonSmartTargeting(true);
				spells::detail::ProblemImpl ignored;
				auto target = spells::Target(1, spells::Destination(&unit));
				if(sp->battleMechanics(&cast)->canBeCastAt(target, ignored)) // Obstacles should not be revealed by immune creatures
				{
					if(shouldReveal) { //hidden obstacle triggers effects after revealed
						revealObstacles(*spellObstacle);
						if(nativeEnvironment)
							cast.cast(nativeEnvironment, target);
						else
							cast.castEval(&server, target);
					}
				}
			}
			else if(shouldReveal)
				revealObstacles(*spellObstacle);
		}

		if(!unit.alive())
			return false;

		if(obstacle->stopsMovement() && !unit.hasBonusOfType(BonusType::FLYING))
			movementStopped = true;
	}

	return unit.alive() && !movementStopped;
}

bool CBattleInfoCallback::evaluateLandingObstacleTriggersForUnit(ServerCallback & server, const battle::Unit & unit) const
{
	return handleObstacleTriggersForUnitWithObstacles(server, nullptr, unit, getAllAffectedObstaclesByStack(&unit, {}));
}
