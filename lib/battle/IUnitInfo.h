/*
 * IUnitInfo.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#pragma once

#include <optional>
#include <utility>

#include "../GameConstants.h"
#include "BattleSide.h"
#include "NewHorizonsNecromancyCombat.h"

class CCreature;

namespace battle
{

class Unit;
class UnitCreatureResolver;

class DLL_LINKAGE IUnitEnvironment
{
public:
	virtual bool unitHasAmmoCart(const Unit * unit) const = 0; //todo: handle ammo cart with bonus system

	virtual PlayerColor unitEffectiveOwner(const Unit * unit) const = 0;
	virtual std::optional<int> unitMagicResistance(const Unit *) const { return std::nullopt; }
	virtual int unitSylvanTacticsSpeed(const Unit *, int = 0) const { return 0; }
	virtual int unitSylvanTacticsInitiativePercent(const Unit *) const { return 0; }
	virtual int unitFortuneSpeed(const Unit *) const { return 0; }
	virtual int unitSpeedBonus(const Unit *) const { return 0; }
	virtual bool unitSlowAffectsSpeed(const Unit *) const { return false; }
	virtual newHorizonsNecromancy::CombatPassives unitNecromancyPassives(const Unit *) const { return {}; }
	virtual int unitAdditionalRetaliations(const Unit *) const { return 0; }
	virtual int unitBloodragePainIncrement(const Unit *) const { return 0; }
	virtual bool unitHasVeteranCohesion(const Unit *) const { return false; }
	/// Saved-context Morale range for this unit, or nullopt to use legacy engine limits.
	virtual std::optional<std::pair<int32_t, int32_t>> unitMoraleLimits(const Unit *) const { return std::nullopt; }
};

/// Explicit copied preparation values. Engaged zero is a fact, not a fallback sentinel.
struct UnitPreparationHealthBasis
{
	int64_t battleStartMaximumHP;
	int64_t rebirthOriginalHP;
};

class DLL_LINKAGE IUnitInfo
{
public:
	virtual int32_t unitBaseAmount() const = 0;
	/// Legacy providers remain absent. An owned frame must supply both nonnegative values.
	virtual std::optional<UnitPreparationHealthBasis> unitPreparationHealthBasis() const { return std::nullopt; }

	virtual uint32_t unitId() const = 0;
	virtual BattleSide unitSide() const = 0;
	virtual PlayerColor unitOwner() const = 0;
	virtual SlotID unitSlot() const = 0;

	virtual const CCreature * unitType() const = 0;
	/// Null retains the exact native entity/identifier paths; owned providers bind a complete table.
	virtual const UnitCreatureResolver * unitCreatureResolver() const { return nullptr; }
};

}
