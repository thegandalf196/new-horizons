/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "NativeEventCommon.h"
#include "../../GameLibrary.h"
#include "../../battle/AccessibilityInfo.h"
#include "../../entities/building/CBuilding.h"
#include "../../entities/faction/CTown.h"
#include "../../mapObjects/CGTownInstance.h"
#include "../../spells/CSpell.h"
#include <cmath>

namespace nativeEvents
{
namespace
{
void enchanted(const Context & context)
{
	const auto name = string(context.parameters, "spell");
	const auto * sourceSpell = spell(name);
	if(!sourceSpell) throw std::runtime_error("Unable to apply enchantment - unknown spell " + name);
	battle::Units targets;
	if(truthy(context.parameters["massive"]))
		targets = context.battle.battleGetUnitsIf([&](const battle::Unit * other)
			{ return other->unitSide() == context.self->unitSide() && other->isValidTarget(false); });
	else targets.push_back(context.self);
	applyEffects(context, *context.self, *sourceSpell, {targets.begin(), targets.end()},
		static_cast<int>(number(context.parameters, "level")), static_cast<int>(number(context.parameters, "duration", 50)), true);
}

void arrowTower(const Context & context)
{
	const auto & unit = *context.self;
	if(!unit.getPosition().isTower()) return;
	const auto * town = context.battle.battleGetDefendedTown();
	if(!town) return;
	int level = 0;
	for(const auto id : town->getBuildings())
	{
		const auto & building = *town->getTown()->buildings.at(id);
		if(building.bid == BuildingID::VILLAGE_HALL || building.bid == BuildingID::FORT) continue;
		if(building.bid == BuildingID::TOWN_HALL || building.upgrade == BuildingID::NONE) ++level;
	}
	const double perBuilding = number(context.parameters, "perBuilding", 2);
	const double minimum = unit.getPosition() == BattleHex::CASTLE_CENTRAL_TOWER
		? number(context.parameters, "keepBase", 10) + perBuilding * level
		: number(context.parameters, "towerBase", 6) + perBuilding * std::floor(level / 2.0);
	const Bonus low(BonusDuration::ONE_BATTLE, BonusType::CREATURE_DAMAGE, BonusSource::CREATURE_ABILITY,
		static_cast<int>(minimum), BonusSourceID(unit.creatureId()), BonusSubtypeID(BonusCustomSubtype::creatureDamageMin));
	const Bonus high(BonusDuration::ONE_BATTLE, BonusType::CREATURE_DAMAGE, BonusSource::CREATURE_ABILITY,
		static_cast<int>(minimum * 2), BonusSourceID(unit.creatureId()), BonusSubtypeID(BonusCustomSubtype::creatureDamageMax));
	addBonus(context, unit, low, false);
	addBonus(context, unit, high, false);
}

std::vector<BattleHex> guardianHexes(BattleHex position, BattleSide side, bool targetIsTwoHex)
{
	std::vector<BattleHex> hexes;
	const auto push = [&](BattleHex hex)
	{
		if(hex.isAvailable() && std::find(hexes.begin(), hexes.end(), hex) == hexes.end()) hexes.push_back(hex);
	};
	const int x = position.getX(), y = position.getY(), width = GameConstants::BFIELD_WIDTH;
	const bool attacker = side == BattleSide::ATTACKER;
	push(attacker ? position.copyToEast().copyToEast() : position.copyToWest().copyToWest());
	if(attacker && (y % 2 == 0 || x > 1))
	{
		if(targetIsTwoHex && y % 2 == 1 && x == 2)
		{
			push(position.copyToNorthEast());
			push(position.copyToSouthEast());
		}
		else
		{
			push(targetIsTwoHex ? position.copyToNorthWest() : position.copyToNorthEast());
			push(targetIsTwoHex ? position.copyToSouthWest() : position.copyToSouthEast());
			if(!targetIsTwoHex && x > 2) push(position.copyToWest());
			else if(targetIsTwoHex)
			{
				push(position.copyToEast().copyToNorthEast());
				push(position.copyToEast().copyToSouthEast());
				if(x > 3) push(position.copyToWest().copyToWest());
			}
		}
	}
	else if(!attacker && (y % 2 == 1 || x < width - 2))
	{
		if(targetIsTwoHex && y % 2 == 0 && x == width - 3)
		{
			push(position.copyToNorthWest());
			push(position.copyToSouthWest());
		}
		else
		{
			push(targetIsTwoHex ? position.copyToNorthEast() : position.copyToNorthWest());
			push(targetIsTwoHex ? position.copyToSouthEast() : position.copyToSouthWest());
			if(!targetIsTwoHex && x < width - 3) push(position.copyToEast());
			else if(targetIsTwoHex)
			{
				push(position.copyToWest().copyToNorthWest());
				push(position.copyToWest().copyToSouthWest());
				if(x < width - 4) push(position.copyToEast().copyToEast());
			}
		}
	}
	else if(!attacker && y % 2 == 0)
	{
		push(position.copyToWest().copyToNorthWest());
		push(position.copyToWest().copyToSouthWest());
	}
	else if(attacker && y % 2 == 1)
	{
		push(position.copyToEast().copyToNorthEast());
		push(position.copyToEast().copyToSouthEast());
	}
	return hexes;
}

void summonGuardians(const Context & context)
{
	const auto name = string(context.parameters, "creature");
	const auto * guardian = creature(name);
	if(!guardian) throw std::runtime_error("Unable to summon guardians - unknown creature " + name);
	const auto & unit = *context.self;
	const auto side = unit.unitSide();
	std::vector<BattleHex> hexes;
	if(guardian->isDoubleWide())
		hexes = guardianHexes(unit.getPosition(), side, unit.creatureId().toEntity(LIBRARY)->isDoubleWide());
	else
	{
		const auto & surrounding = unit.getSurroundingHexes();
		hexes.assign(surrounding.begin(), surrounding.end());
	}
	const auto count = static_cast<TQuantity>(std::max(1.0, std::floor(unit.getCount() * number(context.parameters, "val") / 100)));
	bool summoned = false;
	for(const auto hex : hexes)
		if(context.battle.getAccessibility().accessible(hex, guardian->isDoubleWide(), side))
		{
			battle::UnitInfo info;
			info.count = count;
			info.type = guardian->getId();
			info.side = side;
			info.position = hex;
			info.summoned = true;
			addUnit(context, info);
			summoned = true;
		}
	if(summoned) refreshUnits(context);
}
}

std::shared_ptr<ICombatEventScript> setupEvent(const std::string & path)
{
	if(path == "combat/enchanted") return event(enchanted, {CombatEventType::BATTLE_START, CombatEventType::ROUND_START});
	if(path == "combat/arrowTowerDamage") return event(arrowTower, {CombatEventType::BATTLE_SETUP});
	if(path == "combat/summonGuardians") return event(summonGuardians, {CombatEventType::BATTLE_START});
	return nullptr;
}
}
