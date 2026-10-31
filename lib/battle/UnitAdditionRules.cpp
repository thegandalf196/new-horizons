/*
 * UnitAdditionRules.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "UnitAdditionRules.h"
#include "NewHorizonsOffense.h"
#include "UnitCreatureResolver.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../spells/NewHorizonsSorcery.h"
#include <limits>

uint32_t battle::nextUnitAdditionId(size_t size, const JsonNode & rules)
{
	if(heroCommands::supportedByRules(rules, HeroCommand::FOCUS_FIRE)
		&& size > static_cast<size_t>(std::numeric_limits<int32_t>::max()))
		throw std::runtime_error("New Horizons targeted unit identities exhausted");
	return static_cast<uint32_t>(size);
}

namespace battle
{
namespace
{
UnitInfo decodeAddition(uint32_t id, const JsonNode & data, const JsonNode & rules, size_t size,
	const UnitCreatureResolver * resolver)
{
	if(heroCommands::supportedByRules(rules, HeroCommand::FOCUS_FIRE) && id != nextUnitAdditionId(size, rules))
		throw std::runtime_error("Invalid New Horizons targeted unit allocation");
	battle::UnitInfo info;
	if(resolver) info.load(id, data, *resolver);
	else info.load(id, data);
	if(info.rebirthOriginalAggregateHP < 0)
		throw std::runtime_error("Invalid Rebirth output original HP");
	if(info.summonRoundsRemaining > 0 && info.phantomIntegrity > 0)
		throw std::runtime_error("Timed summons cannot carry a Phantom profile");
	if(info.phantomIntegrity < 0 || info.phantomDuration < 0
		|| ((info.phantomIntegrity == 0) != (info.phantomDuration == 0)))
		throw std::runtime_error("Invalid Phantom Army spawn profile");
	if(info.phantomIntegrity > 0
		&& (info.count <= 0 || !info.summoned || info.natureSummoned
			|| !newHorizonsSorcery::phantomArmyDurationSupported(info.phantomDuration)))
		throw std::runtime_error("Invalid Phantom Army spawn profile");
	if(info.rebirthOriginalAggregateHP > 0
		&& (info.count <= 0 || !info.type.hasValue() || !(resolver ? resolver->creature(info.type) : info.type.toCreature())
			|| !info.summoned || info.natureSummoned || info.phantomIntegrity > 0))
		throw std::runtime_error("Invalid Rebirth output spawn metadata");
	return info;
}

}
}
battle::UnitInfo battle::decodeUnitAddition(uint32_t id, const JsonNode & data, const JsonNode & rules, size_t size)
{
	return decodeAddition(id, data, rules, size, nullptr);
}
battle::UnitInfo battle::decodeUnitAddition(uint32_t id, const JsonNode & data, const JsonNode & rules, size_t size,
	const UnitCreatureResolver & resolver)
{
	return decodeAddition(id, data, rules, size, &resolver);
}

void battle::validateUnitAdditionCapacity(const UnitInfo & info, int32_t effectiveMaxHP)
{
	if(effectiveMaxHP <= 0
		|| info.count > std::numeric_limits<int64_t>::max() / effectiveMaxHP
		|| info.rebirthOriginalAggregateHP > static_cast<int64_t>(info.count) * effectiveMaxHP)
		throw std::runtime_error("Rebirth output original HP exceeds its aggregate capacity");
}

void battle::validateRebirthAdditionSide(const UnitInfo & info)
{
	if(info.side != BattleSide::ATTACKER && info.side != BattleSide::DEFENDER)
		throw std::runtime_error("Invalid Rebirth output side");
}

bool battle::unitAdditionNeedsVengeance(const Unit & unit, const HeroOrderState * orderState,
	int32_t round, const CGHeroInstance * hero)
{
	if(orderState && orderState->issuedRound == round
		&& hero && hero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::VENGEANCE))
		return unit.alive() && !unit.isGhost() && !unit.isTurret()
			&& !unit.hasBonusOfType(BonusType::SIEGE_WEAPON)
			&& unit.unitSlot() != SlotID::COMMANDER_SLOT_PLACEHOLDER
			&& !newHorizonsOffense::hasVengeanceRetaliationBonus(&unit);
	return false;
}
