/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "NewHorizonsAerialSupremacy.h"
#include "CBattleInfoCallback.h"
#include "CUnitState.h"
#include "IBattleState.h"
#include "ReachabilityInfo.h"
#include "NewHorizonsCombatSkills.h"
#include "NewHorizonsCreatureAbilitySuppression.h"
#include "../entities/hero/NewHorizonsAerialSupremacy.h"
#include "../json/JsonNode.h"
#include <limits>
#include <stdexcept>

namespace newHorizonsAerialSupremacy
{
void Excursion::validateShape() const
{
	if(actor == opponent || actor > INT32_MAX || opponent > INT32_MAX || round < 0
		|| !origin.isAvailable() || !landing.isAvailable() || !target.isAvailable())
		throw std::runtime_error("Invalid Aerial Supremacy excursion");
}

bool hasRoundState(const JsonNode & snapshot)
{
	if(!snapshot.isStruct() || !snapshot.Struct().contains("state"))
		return false;
	const auto & state = snapshot["state"];
	if(!state.isStruct())
		throw std::runtime_error("Invalid Aerial Supremacy unit state");
	if(!state.Struct().contains("aerialRetaliationRound"))
		return false;
	const auto & value = state["aerialRetaliationRound"];
	if(value.getType() != JsonNode::JsonType::DATA_INTEGER
		|| value.Integer() < -1 || value.Integer() > INT32_MAX)
		throw std::runtime_error("Invalid Aerial Supremacy round marker");
	return value.Integer() >= 0;
}

std::optional<Excursion> prepare(const CBattleInfoCallback & battle,
	const battle::Unit * actor, const battle::Unit * opponent)
{
	if(!battle.getBattle() || !actor || !opponent || !actor->alive() || !opponent->alive()
		|| actor->isGhost() || opponent->isGhost() || actor->isTimeStopped() || opponent->isTimeStopped()
		|| !actor->getPosition().isAvailable() || !opponent->getPosition().isAvailable()
		|| !actor->ableToRetaliate() || opponent->isInvincible()
		|| !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(actor)
		|| !actor->hasBonusOfType(BonusType::FLYING)
		|| actor->hasBonusOfType(BonusType::NOT_ACTIVE)
		|| opponent->hasBonusOfType(BonusType::BLOCKS_RANGED_RETALIATION)
		|| newHorizonsCreatureAbilitySuppression::suppressionLevel(*actor) > 0
		|| !battle.battleMatchActionController(actor, opponent)
		|| !battle.battleMatchActionController(opponent, actor))
		return std::nullopt;
	const auto griffin = CreatureID(CreatureID::decode("core:griffin"));
	const auto royal = CreatureID(CreatureID::decode("core:royalGriffin"));
	if(actor->creatureId() != griffin && actor->creatureId() != royal)
		return std::nullopt;
	const auto side = battle.playerToSide(battle.battleGetOwner(actor));
	if(!newHorizonsHeroes::aerialSupremacyEnabled(battle.battleGetFightingHero(side)))
		return std::nullopt;
	const auto * state = dynamic_cast<const battle::CUnitState *>(actor);
	if(!state || (actor->creatureId() == griffin
		&& state->aerialRetaliationRound == battle.battleGetRound()))
		return std::nullopt;
	const auto allowance = actor->getMovementRange(0);
	uint32_t shooterDistance = ReachabilityInfo::INFINITE_DIST;
	for(const auto & actorHex : actor->getHexes())
		for(const auto & targetHex : opponent->getHexes())
			shooterDistance = std::min<uint32_t>(shooterDistance, BattleHex::getDistance(actorHex, targetHex));
	if(shooterDistance > allowance)
		return std::nullopt;
	const auto reach = battle.getReachability(actor);
	BattleHex selected;
	uint32_t best = ReachabilityInfo::INFINITE_DIST;
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex hex(index);
		const auto distance = reach.distances[index];
		if(!hex.isAvailable() || !reach.isReachable(hex) || distance > allowance || distance >= best
			|| !reach.accessibility.accessibleForMovementEndpoint(hex, actor->doubleWide(), actor->unitSide())
			|| !battle.isMeleeAttackPossible(actor, opponent, hex))
			continue;
		selected = hex;
		best = distance;
	}
	if(!selected.isValid())
		return std::nullopt;
	return Excursion{actor->unitId(), opponent->unitId(), actor->getPosition(), selected,
		opponent->getPosition(), battle.battleGetRound()};
}

bool matches(const CBattleInfoCallback & battle, const Excursion & excursion)
{
	excursion.validateShape();
	const auto actual = prepare(battle, battle.battleGetUnitByID(excursion.actor),
		battle.battleGetUnitByID(excursion.opponent));
	return actual && actual->origin == excursion.origin && actual->landing == excursion.landing
		&& actual->target == excursion.target && actual->round == excursion.round;
}

void validate(const CBattleInfoCallback & battle, const Excursion & excursion)
{
	if(!matches(battle, excursion))
		throw std::runtime_error("Stale Aerial Supremacy excursion");
}
}
