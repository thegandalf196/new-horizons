/*
 * NewHorizonsFrozen.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "NewHorizonsFrozen.h"
#include "CUnitState.h"
#include "PhysicalAffliction.h"
#include "NewHorizonsCombatSkills.h"
#include "BattleAttackInfo.h"
#include "../CCreatureHandler.h"
#include "../spells/NewHorizonsMagic.h"
#include "../bonuses/BonusParameters.h"
#include "../bonuses/BonusList.h"
#include <limits>

namespace newHorizonsFrozen
{
namespace
{
const JsonNode * creatureAbilities(const JsonNode & rules)
{
	if(!rules.isStruct() || rules["rulesetVersion"].getType() != JsonNode::JsonType::DATA_INTEGER
		|| rules["rulesetVersion"].Integer() != newHorizonsMagic::CURRENT_RULESET_VERSION
		|| rules["schemaVersion"].getType() != JsonNode::JsonType::DATA_INTEGER
		|| rules["schemaVersion"].Integer() != 1)
		return nullptr;
	const auto & data = rules["creatureAbilities"];
	if(!data.isStruct() || data.Struct().size() != 3
		|| data["rulesetVersion"].getType() != JsonNode::JsonType::DATA_INTEGER
		|| data["rulesetVersion"].Integer() != 1)
		return nullptr;
	for(const auto * key : {"freezingTouchChancePercent", "shatterBonusPercent"})
		if(data[key].getType() != JsonNode::JsonType::DATA_INTEGER
			|| data[key].Integer() < 0 || data[key].Integer() > 100)
			return nullptr;
	return &data;
}

bool isMarker(const Bonus * bonus)
{
	if(!bonus || bonus->type != BonusType::PHYSICAL_AFFLICTION
		|| bonus->source != BonusSource::CREATURE_ABILITY)
		return false;
	const auto metadata = physicalAfflictions::markerMetadata(*bonus);
	return metadata && metadata->kind == "frozen";
}
}

bool enabled(const JsonNode & rules)
{
	return creatureAbilities(rules) != nullptr;
}

int chancePercent(const JsonNode & rules)
{
	const auto * data = creatureAbilities(rules);
	return data ? static_cast<int>((*data)["freezingTouchChancePercent"].Integer()) : 0;
}

int shatterBonusPercent(const JsonNode & rules)
{
	const auto * data = creatureAbilities(rules);
	return data ? static_cast<int>((*data)["shatterBonusPercent"].Integer()) : 0;
}

bool isFreezingTouchAttacker(const battle::Unit & unit, const JsonNode & rules)
{
	const auto * creature = unit.creatureId().toCreature();
	return chancePercent(rules) > 0 && creature && creature->getJsonKey() == "core:iceElemental"
		&& unit.alive() && !unit.isGhost() && !unit.isTimeStopped() && !isFrozen(unit)
		&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(&unit)
		&& unit.unitSlot() != SlotID::WAR_MACHINES_SLOT;
}

bool qualifiesForShatter(const BattleAttackInfo & attack, const JsonNode & rules)
{
	return creatureAbilities(rules) && attack.defender && isFrozen(*attack.defender)
		&& newHorizonsCombatSkills::isPhysicalCreatureAttack(attack.attacker, attack.physicalDamage);
}

Bonus marker(BonusSourceID sourceID, int32_t applicationRound)
{
	if(applicationRound < 0)
		throw std::invalid_argument("Frozen application round must be nonnegative");
	Bonus result(BonusDuration::ONE_BATTLE, BonusType::PHYSICAL_AFFLICTION,
		BonusSource::CREATURE_ABILITY, 0, sourceID);
	JsonNode parameters;
	parameters["kind"].String() = "frozen";
	parameters["applicationOrder"].Integer() = 0;
	parameters["applicationRound"].Integer() = applicationRound;
	result.parameters = std::make_shared<BonusParameters>(parameters);
	result.statusTags = {BonusStatusTag::DEBUFF};
	result.statusIdentity = "frozen";
	return result;
}

Bonus makeFrozenMarker(BonusSourceID sourceID, int32_t applicationRound)
{
	return marker(sourceID, applicationRound);
}

std::shared_ptr<battle::CUnitState> prepareApplication(const battle::Unit & unit,
	const std::vector<Bonus> & bonuses)
{
	std::optional<int32_t> round;
	for(const auto & bonus : bonuses)
	{
		const auto applicationRound = markerApplicationRound(bonus);
		if(!applicationRound)
			continue;
		if(round)
			throw std::invalid_argument("Invalid or duplicate Frozen marker");
		round = applicationRound;
	}
	if(!round)
		return {};
	auto result = unit.acquireState();
	result->recordFrozenApplication(*round);
	return result;
}

std::optional<int32_t> markerApplicationRound(const Bonus & bonus)
{
	const auto metadata = physicalAfflictions::markerMetadata(bonus);
	if(!metadata || metadata->kind != "frozen")
		return {};
	if(bonus.source != BonusSource::CREATURE_ABILITY || bonus.duration != BonusDuration::ONE_BATTLE
		|| bonus.statusIdentity != "frozen"
		|| bonus.statusTags != std::vector<BonusStatusTag>{BonusStatusTag::DEBUFF})
		throw std::invalid_argument("Invalid Frozen marker provenance or status");
	const auto & value = bonus.parameters->toCustom<JsonNode>()["applicationRound"];
	if(value.getType() != JsonNode::JsonType::DATA_INTEGER
		|| value.Integer() < 0 || value.Integer() > std::numeric_limits<int32_t>::max())
		throw std::invalid_argument("Frozen marker requires a valid application round");
	return static_cast<int32_t>(value.Integer());
}

bool isFrozen(const battle::Unit & unit)
{
	return unit.hasBonus(CSelector(isMarker));
}

bool canApply(const battle::CUnitState & unit, int32_t round)
{
	return round >= 0 && unit.alive() && !unit.isGhost() && !unit.isTimeStopped()
		&& unit.getPosition().isAvailable() && !unit.isInvincible() && !unit.isTurret() && !isFrozen(unit)
		&& unit.frozenLastAppliedRound() < round;
}

std::vector<Bonus> removalPlan(const battle::Unit & unit)
{
	std::vector<Bonus> result;
	for(const auto & bonus : *unit.getAllBonuses(CSelector(isMarker)))
		result.push_back(*bonus);
	return result;
}

bool forfeitsNormalActivation(const battle::Unit & unit, BattleUnitTurnReason reason)
{
	return reason == BattleUnitTurnReason::TURN_QUEUE && unit.alive()
		&& !unit.isTimeStopped() && isFrozen(unit);
}
}
