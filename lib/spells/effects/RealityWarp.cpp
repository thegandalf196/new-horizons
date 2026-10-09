/*
 * RealityWarp.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "RealityWarp.h"
#include "../ISpellMechanics.h"
#include "../NewHorizonsRealityWarp.h"
#include "../NewHorizonsSpellAvailability.h"
#include "../../battle/CBattleInfoCallback.h"
#include "../../battle/Unit.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../networkPacks/SetStackEffect.h"

#include <vcmi/ServerCallback.h>
#include <vcmi/spells/Spell.h>

#include <stdexcept>

namespace spells::effects
{
namespace
{
bool enabled(const Mechanics * mechanics)
{
	return mechanics && mechanics->battle() && mechanics->usesNewHorizonsMagicV3()
		&& mechanics->getSpell() && mechanics->getSpell()->getJsonKey() == newHorizonsRealityWarp::SPELL_KEY
		&& newHorizonsMagic::spellAllowedByBattleRoster(*mechanics->battle(), mechanics->getSpellId());
}
}

bool RealityWarpEffect::validPair(const Mechanics * mechanics, const Target & target) const
{
	if(!enabled(mechanics) || !mechanics->getHeroCaster()
		|| target.size() != 2 || !target[0].unitValue || !target[1].unitValue
		|| target[0].unitValue->unitId() == target[1].unitValue->unitId())
		return false;
	const auto * callback = mechanics->battle();
	for(const auto & selected : target)
	{
		const auto * current = callback->battleGetUnitByID(selected.unitValue->unitId());
		if(current != selected.unitValue || !current->alive() || !current->isValidTarget(false)
			|| current->isGhost() || current->isTurret() || !mechanics->isReceptive(current))
			return false;
	}
	const auto * hero = mechanics->getHeroCaster();
	const bool realityBreaker = hero->hasActivePerk("new-horizons:chaosMagic", "new-horizons:chaosMagic.realityBreaker");
	if(!realityBreaker)
	{
		const bool firstFriendly = callback->battleGetOwner(target[0].unitValue) == mechanics->getCasterColor();
		const bool secondFriendly = callback->battleGetOwner(target[1].unitValue) == mechanics->getCasterColor();
		if(firstFriendly == secondFriendly)
			return false;
	}
	return newHorizonsRealityWarp::prepareExchange(*callback,
		target[0].unitValue->unitId(), target[1].unitValue->unitId()).exchange.has_value();
}

void RealityWarpEffect::adjustTargetTypes(std::vector<TargetType> & types, const Mechanics *) const
{
	types = {AimType::CREATURE, AimType::CREATURE};
}

void RealityWarpEffect::adjustAffectedHexes(BattleHexArray & hexes, const Mechanics *, const Target & target) const
{
	for(const auto & selected : target)
		if(selected.unitValue)
			hexes.insert(selected.unitValue->getPosition());
}

bool RealityWarpEffect::applicableGeneral(Problem & problem, const Mechanics * mechanics) const
{
	if(enabled(mechanics))
	{
		const auto units = mechanics->battle()->battleGetAllUnits(false);
		for(size_t first = 0; first < units.size(); ++first)
			for(size_t second = first + 1; second < units.size(); ++second)
				if(validPair(mechanics, {Destination(units[first]), Destination(units[second])}))
					return true;
	}
	if(mechanics)
		mechanics->adaptProblem(ESpellCastProblem::NO_APPROPRIATE_TARGET, problem);
	return false;
}

bool RealityWarpEffect::applicableTarget(Problem & problem, const Mechanics * mechanics, const Target & target) const
{
	if(validPair(mechanics, target))
		return true;
	if(mechanics)
		mechanics->adaptProblem(ESpellCastProblem::NO_APPROPRIATE_TARGET, problem);
	return false;
}

void RealityWarpEffect::apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const
{
	if(!server || !validPair(mechanics, target))
		return;
	const auto prepared = newHorizonsRealityWarp::prepareExchange(*mechanics->battle(),
		target[0].unitValue->unitId(), target[1].unitValue->unitId());
	if(!prepared.exchange)
		return;
	SetStackEffect packet;
	packet.battleID = mechanics->getBattleID();
	packet.exchange = prepared.exchange;
	server->apply(packet);
}

Target RealityWarpEffect::filterTarget(const Mechanics * mechanics, const Target & target) const
{
	return validPair(mechanics, target) ? target : Target{};
}

Target RealityWarpEffect::transformTarget(const Mechanics * mechanics, const Target & aimPoint, const Target &) const
{
	Target selected;
	if(!mechanics || !mechanics->battle() || aimPoint.size() != 2)
		return selected;
	for(const auto & destination : aimPoint)
	{
		const auto * unit = destination.unitValue
			? mechanics->battle()->battleGetUnitByID(destination.unitValue->unitId())
			: mechanics->battle()->battleGetUnitByPos(destination.hexValue, true);
		if(!unit)
			return {};
		selected.emplace_back(unit);
	}
	return filterTarget(mechanics, selected);
}

void RealityWarpEffect::initImpl(JsonNode data)
{
	if(!data.isStruct() || !data["type"].isString() || data["type"].String() != "core:realityWarp")
		throw std::runtime_error("Reality Warp effect requires type 'core:realityWarp'");
	for(const auto & [key, value] : data.Struct())
		if(key != "type" && key != "indirect" && key != "optional")
			throw std::runtime_error("Unknown Reality Warp effect parameter: " + key);
}
}
